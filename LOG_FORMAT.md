# Log File Format

`chat_sim.log.txt` follows the format from proposal:

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

**Correctness invariant:**
For each room, ACQUIRED_LOCK and RELEASED_LOCK entries must strictly alternate.
Run `./check_log.sh` to verify this automatically.

## Log file Correctness Check

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

## What You Will See (On Run)

### Terminal output:

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

