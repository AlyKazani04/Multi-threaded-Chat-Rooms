// chat_sim.h
// ----------
// This is the MAIN header file. Every other .c file includes this.
// Do NOT split this into multiple headers, keep everything here.
//
// What needs to go in here:
//
// 1. All the #include statements we need globally
//    - pthread.h, semaphore.h, stdbool.h, stdatomic.h, stdio.h etc.
//
// 2. Constants / #defines
//    - MAX_WORKERS 10       (max threads in pool)
//    - MIN_WORKERS 5
//    - MAX_CLIENTS 50       (semaphore cap -- this is the 50 limit from the proposal)
//    - NUM_ROOMS 3
//    - BUFFER_SLOTS 50      (circular buffer size per room)
//    - MAX_MSG_LEN 256
//    - MAX_LOG_ENTRIES 4096
//    - PM_INBOX_SLOTS 32    (for private messaging, additional feature)
//    - DEFAULT_RATE_LIMIT 20
//    - LOG_FILENAME "chat_sim.log.txt"
//
// 3. Enums
//    - ThreadState { THREAD_IDLE, THREAD_ACTIVE, THREAD_WAITING }
//      -- used by GUI to colour code threads green/yellow/grey
//    - LogEventType { LOG_THREAD_CREATE, LOG_ACQUIRED_LOCK, LOG_RELEASED_LOCK,
//                     LOG_SEM_WAIT, LOG_SEM_POST, LOG_MSG_BROADCAST,
//                     LOG_THREAD_EXIT, LOG_BUFFER_FULL, LOG_BUFFER_WRAP,
//                     LOG_PRIVATE_MSG, LOG_RATE_LIMITED }
//
// 4. Structs (in this order please)
//    - ChatMessage    --> one message slot in the circular buffer
//                        fields: sender[32], text[256], timestamp_us, thread_id
//
//    - PrivateMessage --> for the §9.2 additional feature (direct thread-to-thread)
//                        fields: from_thread, to_thread, from_name[32], text[256], timestamp_us
//
//    - ChatRoom       --> one room (we have 3)
//                        fields: name, label, buffer[BUFFER_SLOTS], head, tail, count,
//                                total_written, pthread_mutex_t, cond_not_full, cond_not_empty
//                        NOTE: mutex + 2 condition variables -- this is the
//                        producer-consumer pattern for the bounded buffer problem
//
//    - Task           --> one unit of work in the task queue
//                        fields: room_id, sender[32], message[256], client_id
//                        ALSO needs: is_private (bool), to_thread_id (int) for §9.2
//
//    - TaskQueue      --> the shared FIFO between client generator and workers
//                        fields: tasks[512], head, tail, count,
//                                pthread_mutex_t mutex, sem_t tasks_available
//
//    - WorkerThread   --> per-thread info (visible to GUI)
//                        fields: pthread_t tid, index, state, room_assigned,
//                                atomic_int msgs_sent, current_sender[32], last_active_us
//                        ALSO needs PM inbox fields for §9.2:
//                                pm_inbox[PM_INBOX_SLOTS], pm_head, pm_tail,
//                                pm_count, pm_total, pthread_mutex_t pm_mutex
//                        ALSO needs rate limiting fields for §9.2:
//                                rate_last_us, rate_tokens
//
//    - LogEntry       --> one structured log record
//                        fields: thread_id, timestamp_us, event_type, room (char),
//                                sem_value, msg_len, msgs_sent, detail[64]
//
//    - SimConfig      --> CLI config (parsed from args)
//                        fields: num_threads, num_clients, num_rooms,
//                                duration_sec, arrival_rate, rate_limit_per_sec
//
//    - SimState       --> the BIG global struct, holds everything
//                        fields: rooms[3], workers[10], queue,
//                                atomic counters (total_messages, total_pm,
//                                active_clients, blocked_clients, sem_value,
//                                rate_limited_count),
//                                sem_t admission_sem,
//                                atomic_bool running, paused,
//                                speed_multiplier,
//                                log_ring[4096], log_head, log_count,
//                                pthread_mutex_t log_mutex,
//                                config, start_time_us,
//                                pthread_mutex_t state_mutex
//
// 5. extern declaration
//    - extern SimState g_sim;   (defined in main.c, used everywhere)
//
// 6. Function prototypes for ALL functions across all files
//    Group them by file so its easy to find:
//    -- threadpool.c: threadpool_init, threadpool_shutdown, taskqueue_push, taskqueue_pop
//    -- rooms.c:      rooms_init, room_write, room_read_latest
//    -- logger.c:     logger_init, logger_log, logger_export, now_us
//    -- client_gen.c: client_generator_thread
//    -- gui.c:        gui_run
//    -- utils.c:      parse_args, thread_state_name, log_event_name
//    -- privmsg.c:    privmsg_init, privmsg_send, privmsg_read, privmsg_unread
//    -- ratelimit.c:  ratelimit_check