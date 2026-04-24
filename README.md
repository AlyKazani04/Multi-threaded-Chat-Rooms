# OS-Level Chat Server Using Thread Pools

**CS2006 Operating Systems — Spring 2026**
Aly Kazani (24K-0512) · Sana Alam (24K-0573) · Adeena Asif (24K-0628)

A single-process, multi-threaded chat server simulation in C demonstrating POSIX thread pools, bounded-buffer producer-consumer synchronisation, mutex/condition variable critical sections, and semaphore-based admission control. Visualised in real time via raylib/raygui.

---

## Table of Contents

1. [Requirements](#requirements)
2. [Build](#build)
3. [Run](#run)
4. [CLI Flags](#cli-flags)
5. [File Structure](#file-structure)
6. [Architecture](#architecture)
7. [Concurrency Primitives](#concurrency-primitives)
8. [Logging](#logging)
9. [GUI](#gui)
10. [Testing & Verification](#testing--verification)
11. [Benchmark](#benchmark)

---

## Requirements

- Linux (Ubuntu 22.04 LTS or equivalent)
- GCC with C11 support
- raylib (system-installed)
- POSIX threads (`-lpthread`)

Install raylib:
```bash
sudo apt install libraylib-dev
```

---

## Build

```bash
make
```

Build flags used: `-std=c11 -Wall -Wextra -O2 -g -D_POSIX_C_SOURCE=200809L`

Valgrind targets:
```bash
make helgrind    # race condition check
make memcheck    # memory leak check
```

---

## Run

```bash
./chat_sim --threads 8 --clients 30 --duration 60
```

A raygui window opens showing live thread states, room buffers, and system metrics. The simulation ends after `--duration` seconds or when the window is closed.

---

## CLI Flags

| Flag | Default | Valid Range | Description |
|------|---------|-------------|-------------|
| `--threads N` | 8 | [5, 10] | Worker thread count; clamped and warned if out of range |
| `--clients N` | 30 | [1, 50] | Simulated client count; hard cap at MAX_CLIENTS=50 |
| `--duration N` | 60 | [5, 3600] | Simulation duration in seconds |
| `--rate F` | 5.0 | [0.5, 50.0] | Client arrival rate in requests/second |
| `--ratelimit N` | 20 | [1, 1000] | Token-bucket cap in messages/second per client |

---

## File Structure

```
chat_sim/
├── src/
│   ├── chat_sim.h       # Central header: all structs, constants, prototypes
│   ├── main.c           # Entry point, subsystem wiring, final stats printout
│   ├── threadpool.c     # Thread pool init/shutdown, task queue, worker loop
│   ├── rooms.c          # Circular buffers, mutex, condition variables, consumer threads
│   ├── logger.c         # In-memory log ring buffer, logger_export()
│   ├── client_gen.c     # Client generator thread, throughput sampling
│   ├── gui.c            # raygui 4-panel visualiser at 30 FPS
│   ├── privmsg.c        # Per-thread private message inbox
│   ├── ratelimit.c      # Token-bucket rate limiter per worker
│   ├── utils.c          # CLI parser, enum-to-string helpers, safe_rand_range()
│   └── raygui.h         # Single-header raygui library
├── Makefile
├── benchmark.sh         # 3-run throughput benchmark (10/25/50 clients)
├── check_log.sh         # Log invariant checker
├── setup.sh
└── .gitignore
```

---

## Architecture

Four categories of thread run concurrently inside a single process:

**Main thread** — initialises all subsystems in a fixed order to prevent startup races, runs the raygui render loop, and performs final cleanup:
```
parse_args → logger_init → rooms_init → threadpool_init
→ rooms_consumer_init → pthread_create(client_generator)
→ gui_run [blocks] → threadpool_shutdown → rooms_consumer_shutdown
→ logger_export → exit
```

**Client generator thread** — produces Tasks at the configured arrival rate and pushes them to the shared task queue. Every 8th task is a private message; the rest target a random room. Samples throughput once per second.

**M worker threads** (M ∈ [5, 10], default 8) — consume tasks from the queue, enforce rate limiting, acquire admission via semaphore, write messages to room buffers or private inboxes, then release admission.

**3 consumer threads** (one per room) — drain the circular buffers at 1 message per 500 ms so messages remain visible in the GUI.

### Key Constants (chat_sim.h)

| Constant | Value | Meaning |
|----------|-------|---------|
| `MIN_WORKERS` | 5 | Minimum worker threads |
| `MAX_WORKERS` | 10 | Maximum worker threads |
| `MAX_CLIENTS` | 50 | Admission semaphore initial value |
| `NUM_ROOMS` | 3 | Chat rooms (A/General, B/Priority, C/Private) |
| `BUFFER_SLOTS` | 50 | Circular buffer slots per room |
| `MAX_MSG_LEN` | 256 | Max bytes per message |
| `PM_INBOX_SLOTS` | 32 | Private inbox capacity per worker |
| `TASK_QUEUE_CAP` | 512 | Task queue maximum depth |
| `MAX_LOG_ENTRIES` | 4096 | In-memory log ring buffer size |
| `DEFAULT_RATE_LIMIT` | 20 | Default token-bucket rate (msg/s per client) |

---

## Concurrency Primitives

### Thread Pool (threadpool.c)

`threadpool_init(n)` clamps n to [5, 10] and spawns n worker threads. Each worker loops on:

1. `sem_wait(tasks_available)` — blocks until a task is queued
2. `taskqueue_pop()` — removes task under `queue.mutex`
3. `ratelimit_check()` — token-bucket; drops task and logs `RATE_LIMITED`/`MSG_DROPPED` if no tokens
4. `sem_wait(admission_sem)` — blocks if 50 clients are already active; logs `SEM_BLOCK` before, `SEM_WAIT` after
5. Route to `room_write()` or `privmsg_send()`
6. `sem_post(admission_sem)` — releases admission slot
7. `sched_yield()` — fairness to other workers

On shutdown: `running=false` is detected after waking from `sem_wait`; the worker restores its semaphore slot and logs `THREAD_EXIT`.

### Shared Chat Rooms — Bounded-Buffer Producer-Consumer (rooms.c)

Each `ChatRoom` contains a `ChatMessage buffer[50]` circular array with `head`, `tail`, `count`, a `pthread_mutex_t mutex`, and two condition variables: `cond_not_full` and `cond_not_empty`.

**room_write() — producer:**
- Times `pthread_mutex_lock` with `clock_gettime(CLOCK_MONOTONIC)` to accumulate mutex wait statistics
- Logs `ACQUIRED_LOCK`
- Loops on `pthread_cond_wait(&cond_not_full, &mutex)` while `count >= 50` (canonical Silberschatz idiom; guards against spurious wakeups)
- Writes message, advances `tail`, increments `count` and `total_written`
- `pthread_cond_signal(&cond_not_empty)` → `pthread_mutex_unlock` → logs `RELEASED_LOCK` then `MSG_BROADCAST`

**consumer_thread() — consumer (per room):**
- Locks mutex → waits on `cond_not_empty` while `count == 0`
- Advances `head`, decrements `count`, signals `cond_not_full`
- Unlocks → `nanosleep(500ms)` → repeat

### Semaphore Admission Control

`admission_sem` is initialised to 50. Every worker acquires one slot before processing and releases it after, regardless of success or failure. Current value is mirrored in `atomic_int sem_value` for the GUI to read without calling `sem_getvalue()` from the render thread.

### Private Messaging (privmsg.c)

Each worker has a 32-slot `PrivateMessage pm_inbox[]` ring buffer protected by its own `pm_mutex`. `privmsg_send()` locks only the *recipient's* `pm_mutex`, writes to the inbox (overwriting oldest if full), and increments `total_pm` atomically. The client generator produces a private message every 8th task using `safe_rand_range()` to pick sender ≠ receiver.

### Rate Limiting (ratelimit.c)

Token-bucket per worker thread. On each `ratelimit_check()` call: compute tokens earned since last call using `gettimeofday`, cap at `rate_limit_per_sec`, consume one token to allow or return false to drop. The rate limit is readable at runtime via the GUI slider; it is read under `state_mutex` to pick up slider changes safely.

### Thread Safety Summary

| Shared Variable | Protection |
|----------------|------------|
| `rooms[r].buffer` | `rooms[r].mutex` |
| `total_messages`, `active_clients`, `blocked_clients`, `total_pm`, `total_dropped`, `rate_limited_count`, `sem_value`, `running`, `paused` | `_Atomic` / `atomic_fetch_add` |
| `workers[i].msgs_sent` | `atomic_int` |
| `workers[i].state`, `room_assigned`, `current_sender` | `state_mutex` |
| `speed_multiplier`, `arrival_rate`, `rate_limit_per_sec` | `state_mutex` |
| `workers[i].pm_inbox` | `workers[i].pm_mutex` |
| `log_ring`, `log_head`, `log_count` | `log_mutex` |
| `queue.tasks[]`, `head`, `tail`, `count` | `queue.mutex` |
| `rand()` | `rand_mutex` via `safe_rand_range()` |

---

## Logging

`logger_log()` writes to a 4096-entry in-memory ring buffer protected by `log_mutex`. At simulation end, `logger_export()` writes two files:

### chat_sim.log.txt

Human-readable structured log. Columns: `LINE`, `TIMESTAMP_MS`, `EVENT`, `TID`, `ROOM`, `SEM`, `MSGS`, `DETAIL`.

Example:
```
LINE  TIMESTAMP_MS   EVENT              TID    ROOM   SEM    MSGS   DETAIL
1          1.273     THREAD_CREATE      tid=0  room=-  sem=50  msgs=0   Alice
23         1.804     ACQUIRED_LOCK      tid=0  room=A  sem=-1  msgs=0   Alice
24         1.806     MSG_BROADCAST      tid=0  room=A  sem=49  msgs=1   Alice
25         1.807     RELEASED_LOCK      tid=0  room=A  sem=-1  msgs=1   Alice
```

Logged event types: `THREAD_CREATE`, `ACQUIRED_LOCK`, `RELEASED_LOCK`, `SEM_BLOCK`, `SEM_WAIT`, `SEM_POST`, `MSG_BROADCAST`, `PRIVATE_MSG`, `BUFFER_FULL`, `BUFFER_RESUME`, `BUFFER_WRAP`, `RATE_LIMITED`, `MSG_DROPPED`, `THREAD_EXIT`.

### throughput.csv

Per-second throughput data. Opens in **append mode** — multiple consecutive benchmark runs accumulate in one file without overwriting. Columns: `clients`, `threads`, `second`, `msgs_per_sec`, `peak_msgs_per_sec`, `avg_msgs_per_sec`.

---

## GUI

1980×1040 window at 30 FPS, four panels:

- **3 Chat Room panels** (top-left 60%) — 12 most recent messages per room via `room_read_latest()`, each prefixed with thread ID and timestamp
- **Private Messages panel** (bottom-left) — 14 most recent PMs across all worker inboxes, sorted by timestamp descending
- **Thread Status panel** (top-right 40%) — table of all M workers: ID, STATE (IDLE/ACTIVE/WAITING), LAST ROOM, MSGS, CLIENT name, PM unread count. ACTIVE rows tinted green, WAITING yellow.
- **System Monitor panel** (bottom-right) — semaphore bar (green→yellow→red as it empties), counters (Total Messages, Private Messages, Active Clients, Blocked Clients, Rate-Limited Drops, Total Dropped), elapsed/remaining time, per-room buffer occupancy, Pause/Resume toggle, speed slider (0.5–10×), arrival rate slider (0.5–50 req/s), rate limit slider, Export Log button.

GUI reads room buffers under `room.mutex`, worker states under `state_mutex`, and atomic counters with `atomic_load`. It never writes to room buffers or the log ring.

---

## Testing & Verification

### Log Invariant Check

```bash
./check_log.sh
```

Verifies five invariants against `chat_sim.log.txt`:
1. `ACQUIRED_LOCK` count == `RELEASED_LOCK` count per room
2. No two consecutive `ACQUIRED_LOCK` for the same room without an intervening `RELEASED_LOCK`
3. `SEM_WAIT` count within ±2 of `SEM_POST` count
4. `THREAD_EXIT` count >= `THREAD_CREATE` count
5. `PRIVATE_MSG` count > 0

### Stress Scenarios

**Scenario 1 — Semaphore boundary:**
```bash
./chat_sim --threads 5 --clients 50 --duration 30 --rate 50
```
Admission semaphore drops to 0; `SEM_BLOCK` entries appear; program must not crash.

**Scenario 2 — Buffer overflow / circular wrap:**
```bash
./chat_sim --threads 8 --clients 50 --duration 60 --rate 50
```
Slow consumer causes `BUFFER_FULL` waits and `BUFFER_WRAP` entries.

**Scenario 3 — Deadlock probe:**
```bash
./chat_sim --threads 5 --clients 10 --duration 120
```
Long low-load run; all `check_log.sh` invariants must pass; exercises clean shutdown path.

---

## Benchmark

```bash
./benchmark.sh
```

Runs three 30-second simulations (`--rate 50 --ratelimit 1000`) at 10, 25, and 50 clients with 8 worker threads. Results accumulate in `throughput.csv` and a summary is printed to `benchmark_summary.txt`.

**Test Run Results: (23-04-26)**

| Clients | Threads | Peak msgs/sec | Avg msgs/sec |
|---------|---------|--------------|-------------|
| 10 | 8 | 43 | 13.2 |
| 25 | 8 | 41 | 13.2 |
| 50 | 8 | 44 | 12.0 |

Throughput is bounded by the slow consumer (1 message/500ms per room = 6 total/sec system-wide) and `cond_not_full` waits once buffers fill. Peak values in the 41–44 msg/s range occur in the first two seconds. Average mutex wait across all rooms is consistently below 2 µs.