# AGENTS.md — OS-Level Chat Server Using Thread Pools

This document provides comprehensive context for AI agents (e.g., coding assistants) working on the **OS-Level Chat Server** project. It describes the architecture, concurrency model, module responsibilities, and critical implementation details to ensure safe and effective modifications.

## 1. Project Identity and Purpose

- **Name:** OS-Level Chat Server Simulation
- **Goal:** Demonstrate core operating system concepts using a single‑process, multi‑threaded C program.
- **Key OS Concepts:**
  - Thread pool (M = 5–10 workers)
  - Producer‑consumer bounded buffers (circular queues, 3 chat rooms)
  - Mutex locks and condition variables (`pthread_mutex_t`, `pthread_cond_t`)
  - Semaphore for admission control (max 50 concurrent clients)
  - Real‑time GUI visualisation with `raygui`
- **No networking:** All concurrency is intra‑process via `pthreads`.

## 2. Technology Stack

- **Language:** C11 (`-std=c11 -D_POSIX_C_SOURCE=200809L`)
- **Threading:** POSIX threads (`pthread`)
- **GUI:** [raylib](https://www.raylib.com/) + [raygui](https://github.com/raysan5/raygui) (immediate‑mode)
- **Build:** GNU Make
- **Environment:** Linux with X11 (Ubuntu 22.04+ recommended), VirtualBox with 3D acceleration if using a VM.

## 3. Architecture Overview

### 3.1 Threads and Their Roles

| Thread Type | Count | Purpose |
|-------------|-------|---------|
| **Worker threads** | `--threads` (5–10) | Process tasks from a shared FIFO task queue. Each worker pulls a task, performs the required operation (room write or private message), and then sleeps or waits for more work. |
| **Client generator** | 1 | Simulates client arrivals. Creates `Task` objects (message to a room or private message) and pushes them into the task queue. Rate and message type are controlled by CLI flags and GUI sliders. |
| **Room consumer** | 3 (one per room) | Drains messages from each room’s circular buffer. Runs continuously, sleeping 500 ms between drains, and signals `cond_not_full` to unblock writers. |
| **GUI thread** | 1 (main thread) | Runs the raygui event loop, reads shared state, and updates the display. Does **not** perform heavy work. |

### 3.2 Synchronisation Primitives

- **Admission Semaphore:** `sem_t admission_sem` initialised to 50. Workers call `sem_wait()` before processing a task and `sem_post()` after completion. This limits concurrent active tasks.
- **Task Queue Mutex & Cond Vars:** Protects the shared FIFO task queue. Condition variables `task_available` and `task_space_available` for blocking when queue is empty/full.
- **Per‑Room Mutex & Cond Vars:** Each `ChatRoom` has:
  - `pthread_mutex_t mutex` – protects buffer and counters.
  - `pthread_cond_t cond_not_full` – writers wait here if buffer is full.
  - `pthread_cond_t cond_not_empty` – consumers wait here if buffer is empty.
- **State Mutex:** `state_mutex` protects global simulation state (`g_sim`) read/written by GUI and other threads.
- **Random Number Mutex:** `rand_mutex` protects `rand()` calls to make random generation thread‑safe.

### 3.3 Data Flow (Simplified)

```
Client Generator
       │
       ▼
   Task Queue (FIFO)
       │
       ▼
   Worker Thread (sem_wait → process)
       │
       ├─ Room Message → Room Buffer (producer)
       │                      │
       │                      ▼
       │               Room Consumer Thread (drain every 500ms)
       │
       └─ Private Message → Private Inbox (per‑thread circular buffer)
```

## 4. File Structure and Module Responsibilities

```
src/
├── chat_sim.h         # Central header: all structs, constants, globals, prototypes
├── main.c             # Entry point, initialisation, shutdown order
├── threadpool.c       # Worker thread creation, task queue, worker loop
├── rooms.c            # Chat room buffers, room consumers, private message inboxes
├── logger.c           # Structured logging (ring buffer) and export to file
├── client_gen.c       # Simulated client arrival and task generation
├── gui.c              # raygui 3‑panel visualiser and event handling
├── utils.c            # CLI parsing, string helpers, thread‑safe rand
├── ratelimit.c        # Token‑bucket rate limiter (per‑thread, per‑second)
├── privmsg.c          # Private message sending/receiving
└── raygui.h           # Single‑header GUI library (downloaded by setup.sh)

Makefile                # Build rules, includes helgrind/memcheck targets
setup.sh                # Dependency installer (raylib, raygui, system packages)
check_log.sh            # Log invariant checker (mutex acquisition/release balance)
```

## 5. Important Data Structures

### 5.1 `WorkerThread` (threadpool.c)
```c
typedef struct {
    pthread_t thread;
    int id;
    ThreadState state;           // IDLE, ACTIVE, WAITING
    int room_assigned;           // Room index currently being written (if ACTIVE)
    int last_room_used;          // Last room used (for GUI display when IDLE)
    int messages_processed;
    double rate_tokens;          // Token bucket current tokens
    uint64_t rate_last_us;       // Last token refill timestamp (microseconds)
    // ... private inbox fields
} WorkerThread;
```

### 5.2 `ChatRoom` (rooms.c)
```c
typedef struct {
    char name[32];
    Message buffer[ROOM_BUFFER_SIZE];  // Circular buffer
    int head, tail;
    int count;                         // Current buffer occupancy
    int total_written;                 // Cumulative messages written
    pthread_mutex_t mutex;
    pthread_cond_t cond_not_full;
    pthread_cond_t cond_not_empty;
    pthread_t consumer_thread;         // Background drainer
    bool consumer_running;
} ChatRoom;
```

### 5.3 `Task` (threadpool.c)
```c
typedef struct {
    TaskType type;               // TASK_ROOM_MSG or TASK_PRIVATE_MSG
    int sender_id;
    int target_room;             // For room messages
    int recipient_id;            // For private messages
    char content[MAX_MSG_LEN];
    uint64_t arrival_time_us;
} Task;
```

### 5.4 `GlobalSimState` (chat_sim.h)
```c
typedef struct {
    bool running;
    bool paused;
    double speed_multiplier;
    int rate_limit_per_sec;
    // ... counters for GUI
    atomic_int total_messages;
    atomic_int active_clients;
    atomic_int blocked_clients;
    atomic_int queue_depth;
} GlobalSimState;
```

## 6. Build and Run Instructions

### 6.1 First‑Time Setup
```bash
chmod +x setup.sh check_log.sh
./setup.sh          # Installs raylib, raygui, system dependencies
make                # Builds the chat_sim executable
```

### 6.2 Running the Simulation
```bash
./chat_sim --threads 8 --clients 30 --duration 60
```
Additional flags: `--rate`, `--ratelimit`, `--rooms` (though room count is fixed at 3).

### 6.3 Validation
- **Log correctness:** `./check_log.sh chat_sim.log.txt`
- **Thread safety:** `make helgrind` (runs Valgrind Helgrind)
- **Memory leaks:** `make memcheck` (runs Valgrind Memcheck)

## 7. Logging and Validation

- Log format is fully specified in `LOG_FORMAT.md`.  
- Events include `ACQUIRED_LOCK`, `RELEASED_LOCK`, `MSG_BROADCAST`, `SEM_WAIT`, `SEM_POST`, `THREAD_CREATE`, `THREAD_EXIT`, `MSG_DROPPED`, `BUFFER_RESUME`, `SEM_BLOCK`.
- The `check_log.sh` script verifies that for each room, `ACQUIRED_LOCK` and `RELEASED_LOCK` counts match, and that semaphore waits/posts are balanced. **A mismatch indicates a synchronisation bug.**

## 8. GUI Overview

The raygui window (1200×800) contains three main panels:

1. **Room Panels (A, B, C):** Show the last few messages written to each room’s buffer.
2. **Thread Status Panel:** Lists each worker thread with state colour (IDLE=grey, ACTIVE=green, WAITING=yellow), room assignment, and message count.
3. **System Monitor Panel:** Displays admission semaphore usage, total messages, queue depth, arrival rate, and includes controls (Pause, Speed slider, Export Log).

**Important:** The GUI runs in the main thread and uses atomic variables or `state_mutex` to safely read shared state. Do **not** call blocking functions or heavy computation inside the GUI loop.

## 9. Known Issues and Fixed Behaviours

A history of critical fixes is maintained in `FIXES.md`. Below is a summary to prevent regressions.

| Issue | Symptom | Resolution | Affected Files |
|-------|---------|------------|----------------|
| Buffer deadlock | Writers blocked forever on full buffer; no consumer existed. | Added one consumer thread per room that drains every 500 ms. | `rooms.c` |
| TOCTOU thread state | Thread state displayed incorrectly around `sem_wait()`. | Set state to `WAITING` before `sem_wait()` unconditionally. | `threadpool.c` |
| Rate limiter starvation | Token bucket never refilled because timer was reset on every call. | Only update `rate_last_us` when tokens are actually added. | `ratelimit.c` |
| GUI room display stale | Thread panel showed “—” after thread went idle. | Added `last_room_used` field and updated it after message processing. | `chat_sim.h`, `threadpool.c`, `gui.c` |
| Rate limit read race | GUI writes to `rate_limit_per_sec` while workers read it unprotected. | Wrap all reads with `state_mutex`. | `ratelimit.c` |
| Speed multiplier race | Client generator reads `speed_multiplier` without synchronisation. | Read under `state_mutex`. | `client_gen.c` |
| Non‑random private messages | PMs followed round‑robin pattern. | Use `safe_rand_range()` with retry to avoid self‑messaging. | `client_gen.c`, `utils.c` |
| `rand()` not thread‑safe | Multiple threads calling `rand()` caused races. | Protect with `rand_mutex` and provide `safe_rand_range()`. | `chat_sim.h`, `utils.c`, `main.c` |
| Message counter inflated | Counters incremented even for failed/dropped messages. | Only increment when `success == true`. | `threadpool.c` |
| Missing log events | Debugging buffer/rate behaviour required additional events. | Added `LOG_MSG_DROPPED`, `LOG_BUFFER_RESUME`, `LOG_SEM_BLOCK`. | `chat_sim.h`, `logger.c`, `utils.c` |
| Rate token initialisation | Workers started with zero tokens, causing immediate drops. | Initialise `rate_tokens = rate_limit_per_sec` and set timestamp. | `threadpool.c` |
| Shutdown deadlock | Consumers stopped before workers, leaving writers blocked on `cond_not_full`. | Enforce shutdown order: join client generator → shutdown workers → shutdown consumers. | `main.c`, `rooms.c` |
| Unused variable warning | Compiler warning in `gui.c`. | Removed unused variable. | `gui.c` |

**Agent note:** When modifying synchronisation code, always re‑run `make helgrind` and `./check_log.sh` to verify no regressions.

## 10. Development Notes for AI Agents

### 10.1 Concurrency Guidelines

- **Always hold the appropriate mutex** when accessing shared data. The per‑room mutex protects `buffer`, `head`, `tail`, `count`, and `total_written`. The `state_mutex` protects global simulation settings.
- **Use condition variables correctly:** Loop on the predicate (e.g., `while (room->count == ROOM_BUFFER_SIZE)`) before calling `pthread_cond_wait`.
- **Semaphore usage:** `sem_wait(&admission_sem)` must be paired with `sem_post(&admission_sem)` in all exit paths, including error returns.
- **Thread state updates:** The `WorkerThread.state` field is only written by the owning worker thread. GUI reads it safely via atomic operations or under `state_mutex`.

### 10.2 Common Pitfalls

- **Deadlock risk:** Changing the order of mutex acquisition (e.g., locking `room->mutex` then `state_mutex` in one place but reversed elsewhere) will cause deadlocks. Follow existing pattern.
- **Spurious wakeups:** Always re‑check the condition after `pthread_cond_wait` returns.
- **Token bucket logic:** The rate limiter uses a microsecond timestamp (`clock_gettime(CLOCK_MONOTONIC, …)`). Ensure `rate_last_us` is only updated when tokens are added.
- **GUI thread safety:** The GUI reads `WorkerThread` fields. Use `state_mutex` when reading non‑atomic counters like `messages_processed`.

### 10.3 Adding Features

- **New log events:** Add to `LogEventType` enum in `chat_sim.h`, update `log_event_name()` in `utils.c`, and ensure `logger_log()` is called appropriately.
- **New CLI flags:** Extend `Config` struct and `parse_args()` in `utils.c`. Update `print_usage()`.
- **New GUI controls:** Add elements in `gui.c`. Be mindful that GUI runs at ~60 FPS; avoid blocking calls.

### 10.4 Testing Workflow

1. Build with `make clean && make`.
2. Run a short simulation: `./chat_sim --threads 5 --clients 10 --duration 10`.
3. Check log integrity: `./check_log.sh chat_sim.log.txt`.
4. Run Helgrind for a minimal test (may be slow): `make helgrind`.
5. Run Memcheck: `make memcheck`.

## 11. References to External Documentation

- **README.md** – User‑facing build/run instructions, GUI description, troubleshooting.
- **FIXES.md** – Detailed history of concurrency bugs and their resolutions.
- **LOG_FORMAT.md** – Complete specification of the structured log format.

---

*This document is intended to give AI agents a complete mental model of the codebase. Always consult the source files for the most up‑to‑date implementation details.*