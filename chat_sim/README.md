# OS-Level Chat Server Using Thread Pools
## CS2006 Operating Systems — Spring 2026

**Group Members:**
| Name | ID | Role |
|---|---|---|
| Aly Muhammad Kazani | 24K-0512 | Core Systems Lead – Thread Pool, Task Queue, Semaphore Admission, Stress Testing |
| Sana Munir Alam | 24K-0573 | Synchronisation & Logging Lead – Mutex/Cond Vars, Circular Buffer, Logger |
| Adeena Asif | 24K-0628 | Frontend & Integration Lead – raygui GUI, Integration, Documentation |

**Submitted to:** Sir Minhal Raza

---

## Project Overview

This is a single-process, multi-threaded OS-level chat server **simulation**.
It demonstrates core OS concepts from Silberschatz et al. *Operating System Concepts* (10th ed.):

- **Thread Pool** (M = 5–10 POSIX worker threads via `pthread_create`)
- **Producer-Consumer (Bounded-Buffer)** – 3 chat rooms with circular buffers
- **Mutex** (`pthread_mutex_t`) – critical section protection
- **Condition Variables** (`pthread_cond_t`) – efficient blocking (no busy-wait)
- **Semaphore** (`sem_t`) – admission control (max 50 concurrent clients)
- **raygui** real-time visualization of all the above

No networking, no sockets. All concurrency is **intra-process via pthreads**.

---

## File Structure

```
chat_sim/
├── src/
│   ├── chat_sim.h     ← Central header: all structs, constants, prototypes
│   ├── main.c         ← Entry point, wires all modules (Adeena Asif)
│   ├── threadpool.c   ← Thread pool, task queue, worker loop (Aly Kazani)
│   ├── rooms.c        ← Chat room buffers, mutex, cond vars (Sana Alam)
│   ├── logger.c       ← Structured event logger + export (Sana Alam)
│   ├── client_gen.c   ← Simulated client generator thread (Aly Kazani)
│   ├── gui.c          ← raygui 3-panel visualizer (Adeena Asif)
│   ├── utils.c        ← CLI parser, enum-to-string helpers (shared)
│   └── raygui.h       ← (downloaded by setup.sh – single-header GUI lib)
├── Makefile
├── setup.sh           ← Install all dependencies
├── check_log.sh       ← Log invariant checker (proposal §10.2)
└── README.md
```

---

## Step 1 — Install VirtualBox + Ubuntu 22.04 LTS

1. Download **Ubuntu 22.04 LTS Desktop** ISO from https://ubuntu.com/download
2. Create a VirtualBox VM:
   - RAM: **4 GB minimum** (8 GB recommended)
   - CPU: **2 cores minimum** (4 recommended for thread parallelism)
   - Storage: 20 GB dynamically allocated
   - Display: Enable 3D acceleration (needed by raylib OpenGL)
3. Boot from ISO and complete installation.
4. After boot, install Guest Additions for better display:
   ```bash
   sudo apt install virtualbox-guest-x11
   sudo reboot
   ```

---

## Step 2 — Clone / Copy the Project

Option A — copy the folder into your VM via shared folder.

Option B — create the files manually (they are all plain C/Makefile).

Navigate to the project root:
```bash
cd ~/chat_sim
```

---

## Step 3 — Run setup.sh (install dependencies)

```bash
chmod +x setup.sh check_log.sh
./setup.sh
```

**What setup.sh installs:**
- `gcc`, `make`, `git`, `curl`, `valgrind`
- OpenGL/X11 development headers (required by raylib)
- **raylib** — tries `apt` first, falls back to building from source
- **raygui.h** — downloaded from GitHub into `src/` automatically

This takes 2–5 minutes on first run. You will see:

```
=== CS2006 Chat Sim – Dependency Setup ===
[1/5] Installing system packages...
[2/5] Checking for raylib via apt...
[3/5] Building raylib 5.0 from source...       ← only if apt version missing
[4/5] Downloading raygui.h...
[5/5] Verifying installation...
  → raylib version: 5.0
=== Setup complete! ===
```

---

## Step 4 — Build

```bash
make
```

**Expected successful build output:**
```
gcc -std=c11 -Wall -Wextra -O2 -g -D_POSIX_C_SOURCE=200809L -I./src \
    src/main.c src/threadpool.c src/rooms.c src/logger.c \
    src/client_gen.c src/gui.c src/utils.c \
    -lraylib -lm -lpthread -lGL -ldl -lrt -lX11 \
    -o chat_sim

Build successful →  ./chat_sim
Run:  ./chat_sim --threads 8 --clients 30 --duration 60
```

No warnings should appear. If you see errors, see **Troubleshooting** below.

---

## Step 5 — Run

### Default run (recommended for first test):
```bash
./chat_sim --threads 8 --clients 30 --duration 60
```

### All available flags:
```
--threads   N    Worker threads in pool (5–10, default 8)
--clients   N    Simulated clients (1–50, default 30)
--duration  N    Simulation duration in seconds (default 60)
--rate      F    Initial arrival rate req/sec (default 5.0)
--ratelimit N    §9.2 Max messages per second per client (default 20)
```

### Example runs matching the proposal:
```bash
# Proposal default run
./chat_sim --threads 8 --clients 50 --rooms 3 --duration 60

# Stress test: semaphore boundary (Scenario 1 from §10.3)
./chat_sim --threads 5 --clients 50 --duration 30 --rate 20

# Minimum threads test
./chat_sim --threads 5 --clients 10 --duration 20
```

---

## What You Will See

### Terminal output (mirrors proposal §8.2 + §9.2):

```
[CONFIG] threads=8  clients=30  rooms=3  duration=60s  rate=5.0/s  ratelimit=20 msg/s

[INFO] 3 chat rooms initialised: Room A (General), Room B (Priority), Room C (Private)
[INFO] Private message inboxes initialised (10 threads × 32 slots)
[00:00.001] Thread pool initialized: 8 workers, semaphore=50

[00:00.100] Client-0 (Alice)   >> Room A:  '[C000] Alice: Hello everyone!'
[00:00.300] Client-1 (Bob)     >> Room B:  '[C001] Bob: HIGH PRIORITY: ...'
[00:00.500] Client-2 (Charlie) >> Room C:  '[C002] Charlie: Private message sent.'
[00:00.800] [PM-GEN] Client-8 (Iris) -> T1 (private)
[00:00.801] [PM] T0 (Iris) -> T1: '[DM to T1] Hey T1, this is a private message...'
[00:01.200] [RATE-LIMIT] T3 throttled (limit=20 msg/s)
...
```

### GUI Window:

A **1200 × 800 px** window opens with three panels:

```
┌──────────────────────────────────────────────────────────────────────────┐
│  OS-Level Chat Server  |  Thread Pool Visualizer    Workers:8 | Rem:45s  │
├─────────────┬──────────────┬──────────────┬──────────────────────────────┤
│ Room A      │ Room B       │ Room C       │ Thread Status                │
│ General     │ Priority     │ Private      │ ID  STATE     ROOM MSGS      │
│             │              │              │ T0  [ACTIVE]  Rm A   12      │
│ [T0] 0.10s  │ [T1] 0.30s  │ [T2] 0.50s  │ T1  [IDLE]    —      8       │
│ Alice: ...  │ Bob: ...     │ Charlie: ... │ T2  [WAITING] Rm B   5       │
│ [T3] 0.60s  │              │              │ T3  [ACTIVE]  Rm C   9       │
│ Diana: ...  │              │              │ ...                          │
│             │              │              ├──────────────────────────────┤
│             │              │              │ System Monitor               │
│             │              │              │ Admission: 47/50 [====...  ] │
│             │              │              │ Total Messages: 156          │
│             │              │              │ Active Clients: 3            │
│             │              │              │ Blocked Clients: 0           │
│             │              │              │ Queue Depth: 2               │
│             │              │              │ Elapsed: 31.2s               │
│             │              │              │ Arrival Rate: [=====] 5 r/s  │
│             │              │              │ [|| PAUSE]  [Export Log]     │
│             │              │              │ Room A: buf=12/50 total=78   │
│             │              │              │ Room B: buf=8/50  total=52   │
│             │              │              │ Room C: buf=6/50  total=26   │
└─────────────┴──────────────┴──────────────┴──────────────────────────────┘
  State legend: ■ IDLE  ■ ACTIVE  ■ WAITING
```

**Thread state colours:**
- `IDLE` → **Grey** — worker waiting for a task
- `ACTIVE` → **Green** — worker writing a message to a room buffer
- `WAITING` → **Yellow** — worker blocked on `cond_wait` (buffer full) or `sem_wait`

---

## How to Know It Is Running Correctly

### ✅ Sign 1 – Terminal output matches proposal format
Every line should look like:
```
[00:00.150] Client-1 (Alice) >> Room A: '[C001] Alice: Hello everyone!'
```

### ✅ Sign 2 – GUI shows thread states changing
- Worker threads should cycle between **GREY → GREEN → GREY**
- When the buffer is full (rare at default settings), you'll briefly see **YELLOW**
- The semaphore bar should stay mostly full (green), dropping slightly under load

### ✅ Sign 3 – Message counters increasing
- "Total Messages Sent" counter in System Monitor should increase every second
- Each room's `total=` counter should grow
- Each thread's MSGS column should increment

### ✅ Sign 4 – Final output after window close:
```
=============================================================
  SIMULATION COMPLETE
=============================================================
  Total messages sent  : 312
  Duration             : 60.03 s
  Worker threads used  : 8

  Per-thread message counts:
    Thread-0 : 41 msgs
    Thread-1 : 38 msgs
    ...

  Per-room totals:
    Room A (General)  : 108 messages written
    Room B (Priority) : 102 messages written
    Room C (Private)  : 102 messages written

  Log exported to: chat_sim.log.txt
=============================================================
```

### ✅ Sign 5 – Log file is correct

Run the correctness checker after simulation ends:
```bash
./check_log.sh chat_sim.log.txt
```

**Expected clean output:**
```
=== Log Correctness Check ===
    File: chat_sim.log.txt

[PASS] Room A: 108 ACQUIRED / 108 RELEASED – counts match
[PASS] Room B: 102 ACQUIRED / 102 RELEASED – counts match
[PASS] Room C: 102 ACQUIRED / 102 RELEASED – counts match

[PASS] Semaphore: 312 SEM_WAIT / 312 SEM_POST – balanced (diff=0)

[INFO] Thread lifecycle: 8 THREAD_CREATE / 8 THREAD_EXIT
[PASS] All created threads exited cleanly

=== Summary ===
  PASS: 4  |  FAIL: 0

  === ALL INVARIANTS HOLD – Mutex is NOT broken ===
```

If you see `[FAIL]` entries, there is a synchronisation bug — this should not happen with this codebase.

---

## GUI Controls

| Control | How to use | Effect |
|---|---|---|
| **Pause/Resume** | Click the button | Freezes client generator; workers finish current tasks |
| **Speed Slider** | Drag left/right | Adjusts arrival rate from 1 to 50 requests/second |
| **Export Log** | Click the button | Writes `chat_sim.log.txt` to the current directory |
| **Close Window** | Click × or press ESC | Stops simulation, exports log, prints final stats |

---

## Stress Testing (Proposal §10.3)

### Scenario 1 — Semaphore boundary (50 client limit)

```bash
./chat_sim --threads 5 --clients 50 --duration 30 --rate 50
```

**Expected behaviour:**
- Terminal will show lines like:
  ```
  [SEMAPHORE] Thread blocked at sem_wait – value=0
  ```
- Log file will contain `SEM_WAIT ... BLOCKING` entries
- GUI "Blocked Clients" counter goes above 0
- **Program does NOT crash** — this is the correct boundary behaviour

### Scenario 2 — Buffer overflow (circular wrap)

```bash
./chat_sim --threads 8 --clients 50 --duration 60 --rate 50
```

- Log file will contain `BUFFER_WRAP` entries as the 50-slot circular buffer wraps
- Valgrind/AddressSanitizer reports zero memory errors

### Scenario 3 — Deadlock probe

```bash
./chat_sim --threads 5 --clients 10 --duration 120
```

- Simulation should complete cleanly within the duration
- All threads exit, `check_log.sh` reports no violations

---

## Running under Valgrind

### Helgrind (race condition detector):
```bash
make helgrind
cat helgrind.log   # inspect output
```

**Expected:** `ERROR SUMMARY: 0 errors from 0 contexts`

### Memcheck (memory leak checker):
```bash
make memcheck
cat memcheck.log   # inspect output
```

**Expected:** `LEAK SUMMARY: ... definitely lost: 0 bytes in 0 blocks`

---

## Troubleshooting

### Error: `raylib.h: No such file or directory`
```bash
./setup.sh   # re-run setup; it will build raylib from source
```

### Error: `raygui.h: No such file or directory`
```bash
curl -L -o src/raygui.h \
  https://raw.githubusercontent.com/raysan5/raygui/master/src/raygui.h
```

### Error: `cannot open display` / Black window
- Ensure VirtualBox has **3D Acceleration** enabled (VM Settings → Display)
- Log out and log back in after installing Guest Additions
- Try: `export DISPLAY=:0` then re-run

### Error: `undefined reference to pthread_create`
- Ensure `-lpthread` is in `LDFLAGS` in the Makefile (it already is)

### Build fails with `-Werror` on older gcc
- Remove `-Werror` from `CFLAGS` in Makefile if present (it is not in our Makefile by default)

### `./setup.sh` hangs on raylib source build
- This means the internet is slow; wait up to 5 minutes
- Or manually download: https://github.com/raysan5/raylib/releases

---

## Log File Format

`chat_sim.log.txt` follows the format from proposal §4.5:

```
EVENT_LOG | OS-Level Chat Server | 2026-04-15 09:00:00
----------------------------------------------------------------
LINE  TIMESTAMP_MS   EVENT              TID    ROOM   SEM   MSGS  DETAIL
----------------------------------------------------------------
1     0.001          THREAD_CREATE      tid=0  room=-  sem=50  msgs=0   Alice
2     150.302        ACQUIRED_LOCK      tid=0  room=A  sem=-1  msgs=0   Alice
3     150.351        MSG_BROADCAST      tid=0  room=A  sem=49  msgs=1   Alice
4     150.352        RELEASED_LOCK      tid=0  room=A  sem=-1  msgs=1   Alice
5     200.000        SEM_WAIT           tid=50 room=-  sem=0   msgs=0   BLOCKING
6     350.100        SEM_POST           tid=0  room=-  sem=1   msgs=47  Alice
7     59900.000      THREAD_EXIT        tid=0  room=-  sem=0   msgs=47  normal exit
```

**Correctness invariant (proposal §10.2):**
For each room, ACQUIRED_LOCK and RELEASED_LOCK entries must strictly alternate.
Run `./check_log.sh` to verify this automatically.

---

## OS Concepts Checklist

| Concept | Where implemented | Source file |
|---|---|---|
| Thread pool (M workers) | `threadpool_init()` → `pthread_create` × M | `threadpool.c` |
| Task queue (bounded FIFO) | `taskqueue_push()` / `taskqueue_pop()` | `threadpool.c` |
| Semaphore admission control | `sem_wait(&admission_sem)` / `sem_post` | `threadpool.c` |
| Worker thread loop | `worker_thread()` | `threadpool.c` |
| Mutex (critical section) | `pthread_mutex_lock(&room->mutex)` | `rooms.c` |
| Condition variable (buffer_not_full) | `pthread_cond_wait(&cond_not_full, ...)` | `rooms.c` |
| Condition variable (buffer_not_empty) | `pthread_cond_signal(&cond_not_empty)` | `rooms.c` |
| Circular buffer (bounded-buffer) | `room->buffer[tail]`, head/tail wrap | `rooms.c` |
| Structured event logging | `logger_log()` ring buffer | `logger.c` |
| GUI (raygui immediate-mode) | `gui_run()` + 4 panels | `gui.c` |
| Color-coded thread states | `STATE_COLOR[]` in `draw_thread_panel()` | `gui.c` |
| Pause / Resume / Speed slider | GUI buttons + `atomic_bool paused` | `gui.c` |
| CLI configuration | `parse_args()` | `utils.c` |
| **§9.2 Private messaging** | `privmsg_send/read()` per-inbox pm_mutex | **`privmsg.c`** |
| **§9.2 Rate limiting** | `ratelimit_check()` token-bucket | **`ratelimit.c`** |
| **§9.2 Rate-limit slider** | GUI slider → `config.rate_limit_per_sec` | **`gui.c`** |
| **§9.2 PM panel** | `draw_pm_panel()` live DM feed | **`gui.c`** |
