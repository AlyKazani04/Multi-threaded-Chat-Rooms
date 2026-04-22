# Complete Summary of Fixes Made form previous branch

---

## 1. Producer-Consumer / Buffer Deadlock Fix

**Problem:** Buffer would fill up (50/50) and all producer threads would block forever on `cond_not_full` because no consumer thread ever drained messages.

**Fix:** Added one consumer thread per room that:
- Runs continuously in background
- Drains ONE message from buffer head every 500ms
- Signals `cond_not_full` after each drain
- Allows producers to wake up and write new messages

**Files changed:** `rooms.c`

---

## 2. Thread State TOCTOU Race Fix

**Problem:** Original code checked `sem_getvalue()` to decide if thread would block, then set state. Between check and `sem_wait()`, another thread could grab the last slot, causing wrong state display.

**Fix:** Set `THREAD_WAITING` BEFORE calling `sem_wait()` (unconditionally), set `THREAD_ACTIVE` AFTER `sem_wait()` returns. No guessing.

**Files changed:** `threadpool.c`

---

## 3. Rate Limiter Starvation Fix

**Problem:** Token bucket refill logic was updating `rate_last_us` on EVERY call, even when `add == 0` tokens were added. This reset the timer without adding tokens → permanent starvation.

**Fix:** Only update `rate_last_us` when `add > 0` (actual tokens added). Time accumulates correctly until enough time passes to add a token.

**Files changed:** `ratelimit.c`

---

## 4. GUI Room Display Fix

**Problem:** Thread panel showed "—" or "?" for room column because `room_assigned` was cleared immediately when thread returned to IDLE.

**Fix:** Added `last_room_used` field to `WorkerThread` struct. Updated it when thread processes a message. GUI shows last known room even when thread is idle.

**Files changed:** `chat_sim.h`, `threadpool.c`, `gui.c`

---

## 5. Rate Limit Read Race Fix

**Problem:** GUI thread modified `rate_limit_per_sec` while worker threads read it → potential torn reads (undefined behavior).

**Fix:** Wrapped all reads of `rate_limit_per_sec` with `state_mutex` lock.

**Files changed:** `ratelimit.c`

---

## 6. Speed Multiplier Read Race Fix

**Problem:** Client generator read `g_sim.speed_multiplier` without synchronization while GUI thread wrote it.

**Fix:** Read `speed_multiplier` under `state_mutex` lock.

**Files changed:** `client_gen.c`

---

## 7. Private Messaging Randomization Fix

**Problem:** Private messages always went in round-robin pattern (T0→T1, T1→T2...) not random.

**Fix:** Used `safe_rand_range()` with retry loop to prevent self-messaging. Sender and receiver both randomly selected.

**Files changed:** `client_gen.c`, `utils.c` (added `safe_rand_range()`)

---

## 8. Thread-Safe Random Numbers

**Problem:** `rand()` has global state and is NOT thread-safe. Multiple workers calling it caused race conditions.

**Fix:** Added `rand_mutex` to protect all `rand()` calls. Wrapped in `safe_rand_range()` function.

**Files changed:** `chat_sim.h`, `utils.c`, `main.c`

---

## 9. Message Counter Discrepancy Fix

**Problem:** `total_messages` was incremented for ALL tasks, including failed private messages and rate-limited drops. Counts didn't add up.

**Fix:** Only increment counters when `success == true`. Added `success` boolean flag for both `privmsg_send()` and `room_write()`.

**Files changed:** `threadpool.c`

---

## 10. Logging Event Types Added

**Problem:** Missing log events for debugging buffer behavior and rate limiting.

**Fix Added:**
- `LOG_MSG_DROPPED` - when rate limiter drops a message
- `LOG_BUFFER_RESUME` - when producer wakes from `cond_not_full`
- `LOG_SEM_BLOCK` - before `sem_wait()` (pre-block state)

**Files changed:** `chat_sim.h`, `logger.c`, `utils.c` (log_event_name)

---

## 11. Rate Token Initialization Fix

**Problem:** Worker threads started with `rate_tokens = 0`. First message had `elapsed_us == 0` → refill of 0 → immediate false drop.

**Fix:** Initialize `rate_tokens = rate_limit_per_sec` (full bucket) and `rate_last_us = now_us()` in `threadpool_init()`.

**Files changed:** `threadpool.c`

---

## 12. Shutdown Order Fix

**Problem:** If consumers shut down BEFORE workers, a worker blocked on `cond_not_full` would never wake → secondary deadlock.

**Fix:** Enforced correct shutdown order:
1. Join client generator thread
2. Shut down worker pool
3. Shut down consumer threads

**Files changed:** `main.c`, `rooms.c` (added `rooms_consumer_shutdown()` with broadcast)

---

## 13. GUI Unused Variable Warning

**Problem:** Compiler warning for unused `active` variable in `gui.c`.

**Fix:** Delete or comment out the unused variable.

**Files changed:** `gui.c`

---

## Summary Table

| # | Fix | Files |
|---|-----|-------|
| 1 | Consumer threads per room | `rooms.c` |
| 2 | TOCTOU thread state | `threadpool.c` |
| 3 | Rate limiter starvation | `ratelimit.c` |
| 4 | GUI last room display | `chat_sim.h`, `threadpool.c`, `gui.c` |
| 5 | Rate limit read race | `ratelimit.c` |
| 6 | Speed multiplier race | `client_gen.c` |
| 7 | Random PM sender/receiver | `client_gen.c`, `utils.c` |
| 8 | Thread-safe rand() | `chat_sim.h`, `utils.c`, `main.c` |
| 9 | Message counter accuracy | `threadpool.c` |
| 10 | Missing log events | `chat_sim.h`, `logger.c`, `utils.c` |
| 11 | Rate token initialization | `threadpool.c` |
| 12 | Shutdown order | `main.c`, `rooms.c` |
| 13 | GUI unused variable | `gui.c` |
