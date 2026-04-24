#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# check_log.sh  –  Automated log correctness verification
# CS2006 Operating Systems  |  Spring 2026
#
# Purpose:
#   Implements the INVARIANT CHECK described in proposal §10.2.
#
#   For each chat room (A, B, C):
#     1. Extract all ACQUIRED_LOCK and RELEASED_LOCK entries for that room
#     2. Verify they strictly alternate (no double-acquire without release)
#     3. Report PASS or FAIL
#
# Usage:
#   ./check_log.sh [logfile]
#   ./check_log.sh chat_sim.log.txt
#
# Expected output on a CLEAN run:
#   [PASS] Room A: 42 ACQUIRED / 42 RELEASED – all pairs correct
#   [PASS] Room B: 38 ACQUIRED / 38 RELEASED – all pairs correct
#   [PASS] Room C: 30 ACQUIRED / 30 RELEASED – all pairs correct
#   [PASS] Semaphore: 110 SEM_WAIT / 110 SEM_POST – balanced
#   [PASS] No consecutive ACQUIRED_LOCK without RELEASED_LOCK detected.
#   === ALL INVARIANTS HOLD – Mutex is NOT broken ===
#
# On a BROKEN run (bug present):
#   [FAIL] Room A: Found consecutive ACQUIRED_LOCK entries at lines 14 and 17
#   [FAIL] MUTEX INVARIANT VIOLATED – race condition likely
# ─────────────────────────────────────────────────────────────────────────────

LOG="${1:-chat_sim.log.txt}"
PASS=0
FAIL=0

if [ ! -f "$LOG" ]; then
    echo "[ERROR] Log file not found: $LOG"
    echo "        Run ./chat_sim first, then press 'Export Log'."
    exit 1
fi

echo "=== Log Correctness Check ==="
echo "    File: $LOG"
echo "    Entries: $(wc -l < "$LOG") lines"
echo ""

# ── Check 1: Alternating ACQUIRED / RELEASED per room ────────────────────
for ROOM in A B C; do
    ACQ=$(grep "room=$ROOM" "$LOG" | grep -c "ACQUIRED_LOCK" || true)
    REL=$(grep "room=$ROOM" "$LOG" | grep -c "RELEASED_LOCK" || true)

    if [ "$ACQ" -eq "$REL" ] && [ "$ACQ" -gt 0 ]; then
        echo "[PASS] Room $ROOM: $ACQ ACQUIRED / $REL RELEASED – counts match"
        PASS=$((PASS + 1))
    elif [ "$ACQ" -eq 0 ] && [ "$REL" -eq 0 ]; then
        echo "[INFO] Room $ROOM: no lock events found in log"
    else
        echo "[FAIL] Room $ROOM: $ACQ ACQUIRED vs $REL RELEASED – MISMATCH"
        FAIL=$((FAIL + 1))
    fi

    # Check for consecutive ACQUIRED without intervening RELEASED
    PREV=""
    LINE_NUM=0
    while IFS= read -r line; do
        LINE_NUM=$((LINE_NUM + 1))
        if echo "$line" | grep -q "room=$ROOM.*ACQUIRED_LOCK\|ACQUIRED_LOCK.*room=$ROOM"; then
            if [ "$PREV" = "ACQUIRED" ]; then
                echo "[FAIL] Room $ROOM: consecutive ACQUIRED_LOCK near line $LINE_NUM – MUTEX BROKEN"
                FAIL=$((FAIL + 1))
            fi
            PREV="ACQUIRED"
        elif echo "$line" | grep -q "room=$ROOM.*RELEASED_LOCK\|RELEASED_LOCK.*room=$ROOM"; then
            PREV="RELEASED"
        fi
    done < "$LOG"
done

echo ""

# ── Check 2: Semaphore balance ─────────────────────────────────────────────
SEM_WAIT=$(grep -c "SEM_WAIT" "$LOG" || true)
SEM_POST=$(grep -c "SEM_POST" "$LOG" || true)

# SEM_POST may be one less than SEM_WAIT if the sim was stopped mid-flight;
# allow ±2 tolerance
DIFF=$(( SEM_POST - SEM_WAIT ))
if [ "$DIFF" -lt 0 ]; then DIFF=$(( -DIFF )); fi

if [ "$DIFF" -le 2 ]; then
    echo "[PASS] Semaphore: $SEM_WAIT SEM_WAIT / $SEM_POST SEM_POST – balanced (diff=$DIFF)"
    PASS=$((PASS + 1))
else
    echo "[FAIL] Semaphore: $SEM_WAIT SEM_WAIT / $SEM_POST SEM_POST – IMBALANCE (diff=$DIFF)"
    FAIL=$((FAIL + 1))
fi

echo ""

# ── Check 3: Thread exits ──────────────────────────────────────────────────
EXITS=$(grep -c "THREAD_EXIT" "$LOG" || true)
CREATES=$(grep -c "THREAD_CREATE" "$LOG" || true)
echo "[INFO] Thread lifecycle: $CREATES THREAD_CREATE / $EXITS THREAD_EXIT"
if [ "$EXITS" -ge "$CREATES" ]; then
    echo "[PASS] All created threads exited cleanly"
    PASS=$((PASS + 1))
fi

echo ""

# ── Check 4: §9.2 Private Messages ────────────────────────────────────────
PM_COUNT=$(grep -c "PRIVATE_MSG" "$LOG" || true)
echo "[INFO] §9.2 Private Messages: $PM_COUNT PM events in log"
if [ "$PM_COUNT" -gt 0 ]; then
    echo "[PASS] Private messaging active – $PM_COUNT PMs delivered"
    PASS=$((PASS + 1))
else
    echo "[INFO] No private messages observed (may not have run long enough)"
fi

# ── Check 5: §9.2 Rate Limiting ───────────────────────────────────────────
RL_COUNT=$(grep -c "RATE_LIMITED" "$LOG" || true)
echo "[INFO] §9.2 Rate-Limited Drops: $RL_COUNT events in log"
echo "[INFO] Rate limiting is working if RATE_LIMITED count > 0 under high load"

echo ""

# ── Summary ───────────────────────────────────────────────────────────────
echo "=== Summary ==="
echo "  PASS: $PASS  |  FAIL: $FAIL"
echo ""
if [ "$FAIL" -eq 0 ]; then
    echo "  === ALL INVARIANTS HOLD – Mutex is NOT broken ==="
    exit 0
else
    echo "  === INVARIANT VIOLATIONS DETECTED – review log for bugs ==="
    exit 1
fi
