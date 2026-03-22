#!/bin/bash
# check_log.sh
# ------------
# Run after simulation ends to verify the log file is correct:
#    ./check_log.sh chat_sim.log.txt
#
# What this script needs to check:
#
# CHECK 1 -- ACQUIRED/RELEASED balance per room
#    for each room (A, B, C):
#       count lines matching "room=X.*ACQUIRED_LOCK"
#       count lines matching "room=X.*RELEASED_LOCK"
#       if counts are equal and > 0: PASS
#       if counts differ: FAIL (mutex imbalance)
#
# CHECK 2 -- No consecutive ACQUIRED without RELEASED (the real mutex test)
#    for each room:
#       scan through lines in order
#       track "last lock event" (ACQUIRED or RELEASED)
#       if you see ACQUIRED right after another ACQUIRED for same room: FAIL
#    this is the invariant from proposal section 10.2:
#       "two consecutive ACQUIRED LOCK entries for the same room without
#        an intervening RELEASED LOCK --> MUTEX BROKEN"
#
# CHECK 3 -- Semaphore balance
#    count SEM_WAIT lines, count SEM_POST lines
#    they should be roughly equal (allow ±2 tolerance for mid-flight stops)
#    if difference > 2: FAIL
#
# CHECK 4 -- Thread lifecycle
#    count THREAD_CREATE, count THREAD_EXIT
#    THREAD_EXIT should be >= THREAD_CREATE
#    if not: some threads didnt exit cleanly
#
# CHECK 5 -- §9.2 Private Messages
#    count PRIVATE_MSG lines
#    if > 0: print count (its informational, not a pass/fail)
#
# CHECK 6 -- §9.2 Rate Limiting
#    count RATE_LIMITED lines
#    print count (informational -- will be 0 at default rate, > 0 under stress)
#
# At the end:
#    print total PASS count and FAIL count
#    if FAIL==0: print "ALL INVARIANTS HOLD -- Mutex is NOT broken"
#    if FAIL>0:  print "INVARIANT VIOLATIONS DETECTED" and exit 1
#
# Use grep -c to count, || true to avoid script dying when grep finds 0 matches
# The script should work even if the log file has 0 entries (edge case)