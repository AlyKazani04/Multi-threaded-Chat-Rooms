/*
 * main.c  [FIXED VERSION]
 * ─────────────────────────────────────────────────────────────────────────────
 * Project  : OS-Level Chat Server Using Thread Pools
 * Course   : CS2006 Operating Systems – Spring 2026
 *
 * Integration Author : Adeena Asif [24K-0628]
 *
 * FIXES APPLIED IN THIS FILE:
 *
 *   FIX-Q  rand_mutex initialised before threadpool_init() so safe_rand_range()
 *          is usable from the moment the first worker starts.
 *          srand() seeded with time(NULL) for non-deterministic runs.
 *
 *   FIX-S  Correct shutdown ORDER is enforced:
 *            1. pthread_join(gen_tid)       → no new tasks arrive
 *            2. threadpool_shutdown()       → all workers exit, no new room_write()
 *            3. rooms_consumer_shutdown()   → drain remaining buffer, join consumers
 *
 *          If consumers were shut down BEFORE workers, any worker blocked on
 *          cond_not_full in room_write() would never wake → secondary deadlock.
 *
 *   FIX-R  total_dropped printed in final statistics so drops are visible.
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include "chat_sim.h"

SimState g_sim;

int main(int argc, char **argv)
{
    printf("=============================================================\n");
    printf("  OS-Level Chat Server Using Thread Pools\n");
    printf("  CS2006 Operating Systems  |  Spring 2026\n");
    printf("  Group: Aly Kazani [24K-0512]  Sana Alam [24K-0573]  Adeena Asif [24K-0628]\n");
    printf("=============================================================\n\n");

    /* Step 1: Parse CLI */
    parse_args(argc, argv, &g_sim.config);

    printf("[CONFIG] threads=%d  clients=%d  rooms=%d  duration=%ds  rate=%.1f/s  ratelimit=%d msg/s\n\n",
           g_sim.config.num_threads,
           g_sim.config.num_clients,
           g_sim.config.num_rooms,
           g_sim.config.duration_sec,
           g_sim.config.arrival_rate,
           g_sim.config.rate_limit_per_sec);

    /* Step 2: Record start time */
    g_sim.start_time_us = now_us();

    /* Step 3: Initialise atomic counters and global mutexes */
    atomic_store(&g_sim.total_messages,    0);
    atomic_store(&g_sim.total_pm,          0);
    atomic_store(&g_sim.active_clients,    0);
    atomic_store(&g_sim.blocked_clients,   0);
    atomic_store(&g_sim.sem_value,         MAX_CLIENTS);
    atomic_store(&g_sim.rate_limited_count,0);
    atomic_store(&g_sim.total_dropped,     0);   /* FIX-R */
    atomic_store(&g_sim.running,           false);
    atomic_store(&g_sim.paused,            false);
    g_sim.speed_multiplier = 1.0f;

    pthread_mutex_init(&g_sim.state_mutex, NULL);

    /* FIX-Q: init rand_mutex and seed rand() before any thread starts */
    pthread_mutex_init(&g_sim.rand_mutex, NULL);
    srand((unsigned int)time(NULL));

    /* Step 4: Initialise logger */
    logger_init();

    /* Step 5: Initialise chat rooms */
    rooms_init();

    /* Step 6: Initialise thread pool + spawn workers */
    threadpool_init(g_sim.config.num_threads);

    /* FIX-A: Start consumer threads AFTER workers so buffers have readers
     * before producers start writing.  This is the core deadlock prevention. */
    rooms_consumer_init();

    /* Step 7: Spawn client generator */
    pthread_t gen_tid;
    if (pthread_create(&gen_tid, NULL, client_generator_thread, NULL) != 0) {
        fprintf(stderr, "[FATAL] Cannot create client generator thread\n");
        return EXIT_FAILURE;
    }

    printf("\n[GUI] Opening visualizer window... close to stop simulation.\n\n");

    /* Step 8: GUI blocks here */
    gui_run();

    /* Step 9: Signal stop */
    atomic_store(&g_sim.running, false);
    atomic_store(&g_sim.paused,  false);

    /* FIX-S: Correct shutdown order */

    /* Step 10: Join generator first – no new tasks after this */
    pthread_join(gen_tid, NULL);

    /* Step 11: Shut down worker pool – no new room_write() calls after this */
    threadpool_shutdown();

    /* Step 12: Shut down consumers – safe to drain now that workers are gone */
    rooms_consumer_shutdown();

    /* Step 13: Export log */
    logger_export();

    /* Step 14: Final statistics */
    printf("\n=============================================================\n");
    printf("  SIMULATION COMPLETE\n");
    printf("=============================================================\n");
    printf("  Total messages sent  : %d\n", atomic_load(&g_sim.total_messages));
    printf("  Private messages     : %d  (direct thread-to-thread)\n",
           atomic_load(&g_sim.total_pm));
    printf("  Rate-limited drops   : %d  (token-bucket throttle)\n",
           atomic_load(&g_sim.rate_limited_count));
    printf("  Total dropped msgs   : %d  (FIX-R: rate-limited drops)\n",   /* FIX-R */
           atomic_load(&g_sim.total_dropped));
    printf("  Duration             : %.2f s\n",
           (double)(now_us() - g_sim.start_time_us) / 1e6);
    printf("  Worker threads used  : %d\n", g_sim.config.num_threads);
    printf("  Rate limit setting   : %d msg/sec per client\n",
           g_sim.config.rate_limit_per_sec);
    printf("\n  Per-thread message counts:\n");
    for (int i = 0; i < g_sim.config.num_threads; i++) {
        printf("    Thread-%d : %d msgs sent, %d PMs received\n",
               i,
               atomic_load(&g_sim.workers[i].msgs_sent),
               g_sim.workers[i].pm_total);
    }
    printf("\n  Per-room totals:\n");
    for (int r = 0; r < NUM_ROOMS; r++) {
        printf("    %s (%s) : written=%d  consumed=%d  in_buffer=%d\n",
               g_sim.rooms[r].name,
               g_sim.rooms[r].label,
               g_sim.rooms[r].total_written,
               g_sim.rooms[r].total_consumed,
               g_sim.rooms[r].count);
    }
    printf("\n  Log exported to: %s\n", LOG_FILENAME);
    printf("=============================================================\n");

    /* Cleanup */
    pthread_mutex_destroy(&g_sim.state_mutex);
    pthread_mutex_destroy(&g_sim.rand_mutex);
    pthread_mutex_destroy(&g_sim.log_mutex);

    return EXIT_SUCCESS;
}
