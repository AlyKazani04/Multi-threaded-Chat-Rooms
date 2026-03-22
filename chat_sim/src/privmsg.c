// privmsg.c
// ---------
// This is the 9.2 additional feature -- private messaging between threads.
//
// Key design decision: PMs bypass the chat room buffers ENTIRELY.
// Instead each WorkerThread has its own inbox (defined in chat_sim.h
// inside the WorkerThread struct). The inbox is a ring buffer too,
// same idea as the room buffers but per-thread.
//
// Why per-thread mutex instead of global?
//    If we used one global mutex for all PMs, sending a PM would block
//    every other thread even if theyre sending to a different inbox.
//    With a per-inbox mutex (pm_mutex inside WorkerThread), only the
//    recipient is locked. Much finer-grained. Good to mention in report.
//
// Functions to implement:
//
// --- privmsg_init() ---
//    loop over MAX_WORKERS:
//       set pm_head=0, pm_tail=0, pm_count=0, pm_total=0
//       memset pm_inbox to 0
//       pthread_mutex_init(&workers[i].pm_mutex, NULL)
//    print confirmation message
//    called from threadpool_init() before threads are spawned
//
// --- privmsg_send(int from_thread, int to_thread,
//                  const char *from_name, const char *text) ---
//    validate to_thread is in range and not == from_thread
//
//    get pointer to recipient: workers[to_thread]
//
//    pthread_mutex_lock(&recipient->pm_mutex)  <-- ONLY recipient locked
//
//    if inbox full (pm_count >= PM_INBOX_SLOTS):
//       advance pm_head by 1 (overwrite oldest, same policy as room buffer)
//       pm_count--
//
//    fill in pm_inbox[pm_tail]:
//       from_thread, to_thread, from_name, text
//       timestamp_us = now_us() - start_time_us
//
//    advance pm_tail = (pm_tail + 1) % PM_INBOX_SLOTS
//    pm_count++
//    pm_total++
//
//    pthread_mutex_unlock(&recipient->pm_mutex)
//
//    atomic_fetch_add(&g_sim.total_pm, 1)
//
//    log LOG_PRIVATE_MSG event
//
//    print "[PM] T%d (%s) -> T%d: '%s'" to console
//
//    return true
//
// --- privmsg_read(int thread_id, PrivateMessage *out, int max_msgs) ---
//    used by the GUI to display PMs in the purple PM panel
//    pthread_mutex_lock(&workers[thread_id].pm_mutex)
//    n = min(pm_count, max_msgs)
//    walk backwards from (pm_tail - 1) to get most recent first
//       src = (pm_tail - 1 - i + PM_INBOX_SLOTS) % PM_INBOX_SLOTS
//       out[i] = pm_inbox[src]
//    pthread_mutex_unlock
//    return n
//
// --- privmsg_unread(int thread_id) ---
//    just returns pm_count for that thread
//    used by GUI to show the badge number on each thread row
//    lock pm_mutex, read count, unlock, return
// 
//  NOTE: USE RANDOM LIBRARY SO THAT EACH THREAD GETS PRIVATE MESSGAES, RATHER THAN ONE FIXED THREAD