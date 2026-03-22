// main.c
// ------
//
// Order of operations in main():
//
// 1. print the project header (group names, IDs, course)
//
// 2. call parse_args() -- reads --threads, --clients, --duration,
//    --rate, --ratelimit flags from argv
//    (parse_args is in utils.c)
//
// 3. record g_sim.start_time_us = now_us()
//    everything else timestamps relative to this
//
// 4. initialise all atomic counters to 0:
//    total_messages, total_pm, active_clients, blocked_clients,
//    sem_value, rate_limited_count, running=false, paused=false
//    also pthread_mutex_init for state_mutex
//
// 5. logger_init()     -- sets up the in-memory log ring
//
// 6. rooms_init()      -- creates the 3 chat rooms with their mutexes
//
// 7. threadpool_init() -- spawns M worker threads, inits admission semaphore
//                         NOTE: this also calls privmsg_init() internally so dont call privmsg_init separately
//
// 8. pthread_create for the client generator thread (client_generator_thread is in client_gen.c)
//
// 9. gui_run()         -- this BLOCKS until the window is closed
//
// 10. after gui_run returns:
//     - atomic_store(&g_sim.running, false)
//     - pthread_join the client generator thread
//     - threadpool_shutdown()
//     - logger_export()
//     - print final stats (total messages, PMs, rate-limited drops,
//       per-thread counts, per-room totals)
//     - pthread_mutex_destroy state_mutex and log_mutex
//
// Global variable defined here:
//    SimState g_sim;    <-- the one and only instance, extern'd in chat_sim.h
//
// NOTE: main.c should have NO synchronisation logic of its own. all locking happens inside the modules. keep this file clean.
