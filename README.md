# OS-Level Chat Server Using Thread Pools

## Project Overview

This is a single-process, multi-threaded OS-level chat server simulation.
It demonstrates core OS concepts from Silberschatz et al. *Operating System Concepts* (10th ed.):

- **Thread Pool** (M = 5–10 POSIX worker threads via `pthread_create`)
- **Producer-Consumer (Bounded-Buffer)** – 3 chat rooms with circular buffers
- **Mutex** (`pthread_mutex_t`) – critical section protection
- **Condition Variables** (`pthread_cond_t`) – efficient blocking (no busy-wait)
- **Semaphore** (`sem_t`) – admission control (max 50 concurrent clients)
- **raygui** real-time visualization of all the above

All concurrency is **intra-process via pthreads**.

---

## File Structure

```
Multi-threaded-chat-rooms/
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
├── check_log.sh       ← Log invariant checker (proposal 10.2)
└── README.md
```

---

> **Environment:** Requires Linux with X11 (Ubuntu 22.04+ recommended).  
> If using a VM, enable 3D acceleration and install Guest Additions.

---

## Step 1 — Clone / Copy the Project

Option A — copy the folder into your VM via shared folder.

Option B — create the files manually (they are all plain C/Makefile).

Navigate to the project root:
```bash
cd ~/chat_sim
```

---

## Step 2 — Run setup.sh (install dependencies)
> NOTE: `setup.sh` only works for Ubuntu, or if you use apt.

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

## Step 3 — Build

```bash
make
```

> No warnings should appear. If you see errors, see [TROUBLESHOOTING](TROUBLESHOOTING.md).

---

## Step 4 — Run

### Default run (recommended for first test):
`./chat_sim` \
Uses: 
`--threads 8 --clients 30 --duration 60` internally.


### All available flags:
```
--threads   N    Worker threads in pool (5–10, default 8)
--clients   N    Simulated clients (1–50, default 30)
--duration  N    Simulation duration in seconds (default 60)
--rate      F    Initial arrival rate req/sec (default 5.0)
--ratelimit N    Max messages per second per client (default 20)
```

### Example runs matching the proposal:
```bash
# Default run
./chat_sim --threads 8 --clients 50 --rooms 3 --duration 60

# Stress test: semaphore boundary (Scenario 1 from 10.3)
./chat_sim --threads 5 --clients 50 --duration 30 --rate 20

# Minimum threads test
./chat_sim --threads 5 --clients 10 --duration 20
```


## How to Know It Is Running Correctly

### Sign 1 – Terminal output matches proposal format
Every line should look like:
```
[00:00.150] Client-1 (Alice) >> Room A: '[C001] Alice: Hello everyone!'
```

### Sign 2 – GUI shows thread states changing
- Worker threads should cycle between **GREY → GREEN → GREY**
- When the buffer is full (rare at default settings), you'll briefly see **YELLOW**
- The semaphore bar should stay mostly full (green), dropping slightly under load

### Sign 3 – Message counters increasing
- "Total Messages Sent" counter in System Monitor should increase every second
- Each room's `total=` counter should grow
- Each thread's MSGS column should increment

### Sign 4 – Final output after window close:
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

If you see `[FAIL]` entries, there is a synchronisation bug — this should not happen with this codebase. Check [TROUBLESHOOTING](TROUBLESHOOTING.md) in that case.

## GUI Controls

| Control | How to use | Effect |
|---|---|---|
| **Pause/Resume** | Click the button | Freezes client generator; workers finish current tasks |
| **Speed Slider** | Drag left/right | Adjusts arrival rate from 1 to 50 requests/second |
| **Export Log** | Click the button | Writes `chat_sim.log.txt` to the current directory |
| **Close Window** | Click × or press ESC | Stops simulation, exports log, prints final stats |

---

## Stress Testing

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