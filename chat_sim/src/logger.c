// logger.c
// --------
//
// Handles all structured event logging. Every thread calls logger_log()
// to record what it did. The log is stored in memory first (ring buffer),
// then written to chat_sim.log.txt when logger_export() is called.
//
// The log format from proposal section 4.5 must be followed exactly:
//    [THREAD_ID] [TIMESTAMP_US] ACQUIRED LOCK room=A
//    [THREAD_ID] [TIMESTAMP_US] RELEASED LOCK room=A
//    [THREAD_ID] [TIMESTAMP_US] SEM_WAIT value=49
//    [THREAD_ID] [TIMESTAMP_US] MSG_BROADCAST room=A len=28
//    [THREAD_ID] [TIMESTAMP_US] THREAD_EXIT msgs=47
//
// Functions to implement:
//
// --- now_us() ---
//    returns current time in microseconds since Unix epoch
//    use gettimeofday() -- its in sys/time.h
//    formula: tv.tv_sec * 1000000LL + tv.tv_usec
//    this is used EVERYWHERE not just in the logger
//    (timestamps in ChatMessage, rate limiter, client generator all use this)
//
// --- logger_init() ---
//    set g_sim.log_head = 0, log_count = 0
//    memset the log_ring to 0
//    pthread_mutex_init(&g_sim.log_mutex, NULL)
//
// --- logger_log(int thread_id, LogEventType event, char room,
//                int sem_val, int msg_len, int msgs_sent, const char *detail) ---
//    this gets called from multiple threads simultaneously so it must be
//    thread safe -- use log_mutex
//
//    pthread_mutex_lock(&g_sim.log_mutex)
//    get pointer to log_ring[log_head]
//    fill in all fields: thread_id, timestamp, event_type, room, sem_value etc
//    strncpy detail if provided
//    advance log_head = (log_head + 1) % MAX_LOG_ENTRIES
//    if log_count < MAX_LOG_ENTRIES: log_count++
//    pthread_mutex_unlock
//    NOTE: when ring is full, oldest entry is silently overwritten (same as room buffer)
//
// --- logger_export() ---
//    called by GUI "Export Log" button and automatically at sim end
//
//    pthread_mutex_lock(&g_sim.log_mutex)
//    fopen("chat_sim.log.txt", "w")
//    write header line: "EVENT_LOG | OS-Level Chat Server | <datetime>"
//    write separator line
//    write column headers: LINE TIMESTAMP_MS EVENT TID ROOM SEM MSGS DETAIL
//
//    find oldest entry: oldest = (log_head - log_count + MAX_LOG_ENTRIES) % MAX_LOG_ENTRIES
//    loop i=0 to log_count:
//       idx = (oldest + i) % MAX_LOG_ENTRIES
//       format and fprintf each entry
//       timestamp should be in milliseconds (divide timestamp_us by 1000.0)
//
//    fclose
//    pthread_mutex_unlock
//    print how many entries were exported
//
// Correctness check (proposal section 10.2):
//    A clean run will have strictly alternating ACQUIRED_LOCK / RELEASED_LOCK
//    for every room. If two ACQUIRED appear without a RELEASED in between,
//    the mutex is broken. The check_log.sh script verifies this automatically.