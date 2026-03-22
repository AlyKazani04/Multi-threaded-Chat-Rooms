# CS2006 – OS-Level Chat Server Using Thread Pools
### Operating Systems | Spring 2026 | FAST NUCES Karachi

---

## Group Members

| Name | ID |
|---|---|
| Aly Muhammad Kazani | 24K-0512 |
| Sana Munir Alam | 24K-0573 |
| Adeena Asif | 24K-0628 | 

**Submitted to:** Sir Minhal Raza

---

## What is this project

A multi-threaded chat server simulation that runs entirely inside a single process — no networking, no sockets. Multiple simulated client threads send messages to shared chat rooms, all managed by a thread pool. The goal is to demonstrate core OS concepts like thread pools, mutex locks, semaphores, condition variables, and bounded buffers in a way that you can actually see working in real time through a GUI.

The design is based on two classical OS problems from Silberschatz — the **Producer-Consumer (Bounded Buffer) Problem** and the **Readers-Writers Problem**.

---

## How it works

When you run the program, this is what happens under the hood:

1. A **thread pool** of M worker threads (5–10) is created at startup. All threads start in IDLE state.

2. A **client generator** thread starts producing simulated client requests at a configurable rate and pushes them into a shared task queue.

3. Each worker thread **blocks on a semaphore** waiting for tasks. When a task arrives, the worker wakes up, acquires the semaphore slot (max 50 concurrent clients enforced here), and processes the message.

4. Processing a message means writing it to one of the **3 chat room circular buffers** (Room A, B, or C). Each room is a bounded buffer protected by a mutex and two condition variables — if the buffer is full the worker sleeps on `cond_not_full` until space opens up. This is the Producer-Consumer pattern.

5. Every OS-level event (lock acquired, lock released, semaphore wait, message broadcast, thread exit) is written to an **in-memory log ring buffer** and can be exported to `chat_sim.log.txt` at any time.

6. A **raygui window** renders everything live — chat panels, thread states, semaphore bar, private message feed, and controls.

There are also two additional features from the proposal:
- **Private Messaging (§9.2)** — every 8th message is delivered directly to a specific thread's inbox, bypassing the room buffers entirely. Each thread has its own inbox with its own fine-grained mutex.
- **Rate Limiting (§9.2)** — each client thread has a token bucket that limits how many messages it can send per second. Excess messages are dropped and logged.

---

## File Structure

```
chat_sim/
├── src/
│   ├── chat_sim.h       ← Central header. All structs, constants, enums, prototypes.
│   ├── main.c           ← Entry point. Wires all modules together.
│   ├── threadpool.c     ← Thread pool init, task queue, worker thread loop.
│   ├── rooms.c          ← Chat room buffers, mutex, condition variables.
│   ├── logger.c         ← Structured event logger + file export.
│   ├── client_gen.c     ← Simulated client arrival thread.
│   ├── gui.c            ← raygui 4-panel visualizer.
│   ├── utils.c          ← CLI parser, enum-to-string helpers.
│   ├── privmsg.c        ← §9.2 Private messaging between threads.
│   ├── ratelimit.c      ← §9.2 Per-client token bucket rate limiter.
│   └── raygui.h         ← Downloaded by setup.sh. Do not modify.
├── Makefile
├── setup.sh             ← Installs all dependencies (run once).
├── check_log.sh         ← Verifies log file correctness after simulation.
└── README.md
```

Each `.c` file in this repo currently contains a `.gitkeep` placeholder with comments describing exactly what needs to be implemented. Replace each placeholder with the actual implementation.

---

## OS Concepts Used

| Concept | Where |
|---|---|
| POSIX Thread Pool | `threadpool.c` — `pthread_create` × M workers |
| Semaphore (counting) | `threadpool.c` — `sem_wait/post` on `admission_sem` (cap = 50) |
| Semaphore (signalling) | `threadpool.c` — `tasks_available` wakes workers |
| Mutex | `rooms.c` — `pthread_mutex_lock` on each room buffer |
| Condition Variable (buffer_not_full) | `rooms.c` — producer sleeps when buffer full |
| Condition Variable (buffer_not_empty) | `rooms.c` — consumer sleeps when buffer empty |
| Circular Buffer (Bounded Buffer) | `rooms.c` — 50-slot ring per room |
| Structured Logging | `logger.c` — every OS event recorded with microsecond timestamp |
| Private Messaging (§9.2) | `privmsg.c` — per-thread inbox, fine-grained mutex |
| Rate Limiting (§9.2) | `ratelimit.c` — token bucket, non-blocking drop policy |
| Real-time GUI | `gui.c` — raygui immediate-mode, 4 panels, live updates |

---

## How to Run (Ubuntu 22.04 on VirtualBox)

**Step 1 — Install dependencies (run once)**
```bash
chmod +x setup.sh check_log.sh
./setup.sh
```
This installs gcc, make, valgrind, builds raylib 5.0 from source if needed, and downloads raygui.h.

**Step 2 — Build**
```bash
make
```

**Step 3 — Run**
```bash
./chat_sim --threads 8 --clients 30 --duration 60
```

**Available flags:**
```
--threads   N    Worker threads in pool (5–10, default 8)
--clients   N    Simulated clients (1–50, default 30)
--duration  N    Simulation time in seconds (default 60)
--rate      F    Client arrival rate req/sec (default 5.0)
--ratelimit N    Max messages per second per client (default 20)
```

**Step 4 — Verify correctness**
```bash
./check_log.sh chat_sim.log.txt
```
Expected output:
```
[PASS] Room A: N ACQUIRED / N RELEASED – counts match
[PASS] Room B: N ACQUIRED / N RELEASED – counts match
[PASS] Room C: N ACQUIRED / N RELEASED – counts match
[PASS] Semaphore: N SEM_WAIT / N SEM_POST – balanced
[PASS] All created threads exited cleanly
=== ALL INVARIANTS HOLD – Mutex is NOT broken ===
```

---

## What the GUI looks like

The visualizer opens a 1280×860 window with 4 panels:

- **Room A / B / C panels** — live feed of messages in each chat room, prefixed with thread ID and timestamp
- **Private Messages panel** (purple) — 9.2 direct thread-to-thread messages, bypasses room buffers
- **Thread Status table** — one row per worker thread showing ID, state (IDLE/ACTIVE/WAITING), room assigned, messages sent, and PM inbox count
- **System Monitor** — semaphore progress bar, counters, arrival rate slider, rate limit slider, Pause/Resume button, Export Log button

Thread state colours: **Green = ACTIVE**, **Yellow = WAITING**, **Grey = IDLE**

---

## Stress Tests

```bash
# Test semaphore boundary (clients 51+ should block, not crash)
./chat_sim --threads 5 --clients 50 --duration 30 --rate 50

# Test circular buffer wrap
./chat_sim --threads 8 --clients 50 --duration 60 --rate 50

# Test rate limiting (force drops)
./chat_sim --threads 8 --clients 30 --duration 30 --rate 50 --ratelimit 3
```
