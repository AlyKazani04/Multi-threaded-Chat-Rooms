/*
 * main.c
 * ─────────────────────────────────────────────────────────────────────────────
 * Project  : OS-Level Chat Server Using Thread Pools
 * Course   : CS2006 Operating Systems – Spring 2026
 *
 * Integration Author : Adeena Asif [24K-0628]  ← Frontend & Integration Lead
 *   (Each module was built by its respective owner; integration is Adeena's.)
 *
 * Responsibilities:
 *   • Parse CLI flags (calls utils.c::parse_args)
 *   • Initialise global SimState (g_sim)
 *   • Initialise rooms, logger, thread pool (in that order)
 *   • Spawn the client generator thread
 *   • Call gui_run() – blocks until window is closed
 *   • After GUI exits: shutdown thread pool, export log, print final stats
 *
 * Usage (from proposal §8.1):
 *   ./chat_sim --threads 8 --clients 50 --rooms 3 --duration 60
 *
 * All flags are optional; defaults are:
 *   --threads 8   --clients 30   --rooms 3   --duration 60
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include "chat_sim.h"

/* ── Global singleton – the single shared simulation state ─────────────── */
SimState g_sim;

int main(int argc, char **argv)
{
    printf("=============================================================\n");
    printf("  OS-Level Chat Server Using Thread Pools\n");
    printf("  CS2006 Operating Systems  |  Spring 2026\n");
    printf("  Group: Aly Kazani [24K-0512]  Sana Alam [24K-0573]  Adeena Asif [24K-0628]\n");
    printf("=============================================================\n\n");

    /* ── Step 1: Parse CLI arguments ─────────────────────────────────── */
    parse_args(argc, argv, &g_sim.config);

    printf("[CONFIG] threads=%d  clients=%d  rooms=%d  duration=%ds  rate=%.1f/s  ratelimit=%d msg/s\n\n",
           g_sim.config.num_threads,
           g_sim.config.num_clients,
           g_sim.config.num_rooms,
           g_sim.config.duration_sec,
           g_sim.config.arrival_rate,
           g_sim.config.rate_limit_per_sec);

    /* ── Step 2: Record start time (all timestamps are relative to this) */
    g_sim.start_time_us = now_us();

    /* ── Step 3: Initialise atomic counters and global mutex ─────────── */
    atomic_store(&g_sim.total_messages,    0);
    atomic_store(&g_sim.total_pm,          0);   /* §9.2 */
    atomic_store(&g_sim.active_clients,    0);
    atomic_store(&g_sim.blocked_clients,   0);
    atomic_store(&g_sim.sem_value,         MAX_CLIENTS);
    atomic_store(&g_sim.rate_limited_count,0);   /* §9.2 */
    atomic_store(&g_sim.running,           false);
    atomic_store(&g_sim.paused,            false);
    g_sim.speed_multiplier = 1.0f;
    pthread_mutex_init(&g_sim.state_mutex, NULL);

    /* ── Step 4: Initialise logger (in-memory ring) ───────────────────── */
    logger_init();

    /* ── Step 5: Initialise chat rooms (mutex + cond vars per room) ────── */
    rooms_init();

    /* ── Step 6: Initialise thread pool + spawn M worker threads ─────── */
    threadpool_init(g_sim.config.num_threads);

    /* ── Step 7: Spawn the client generator thread ────────────────────── */
    pthread_t gen_tid;
    if (pthread_create(&gen_tid, NULL, client_generator_thread, NULL) != 0) {
        fprintf(stderr, "[FATAL] Cannot create client generator thread\n");
        return EXIT_FAILURE;
    }

    printf("\n[GUI] Opening visualizer window... close to stop simulation.\n\n");

    /* ── Step 8: GUI blocks here until window is closed ──────────────── */
    gui_run();

    /* ── Step 9: Signal everything to stop ───────────────────────────── */
    atomic_store(&g_sim.running, false);
    atomic_store(&g_sim.paused,  false);

    /* Wake generator if it is sleeping in paused mode */
    pthread_join(gen_tid, NULL);

    /* ── Step 10: Shut down thread pool (joins all workers) ─────────── */
    threadpool_shutdown();

    /* ── Step 11: Export final log file ─────────────────────────────── */
    logger_export();

    /* ── Step 12: Print final statistics (mirrors proposal §8.2) ─────── */
    printf("\n=============================================================\n");
    printf("  SIMULATION COMPLETE\n");
    printf("=============================================================\n");
    printf("  Total messages sent  : %d\n", atomic_load(&g_sim.total_messages));
    printf("  Private messages     : %d  (§9.2 direct thread-to-thread)\n",
           atomic_load(&g_sim.total_pm));
    printf("  Rate-limited drops   : %d  (§9.2 per-client throttle)\n",
           atomic_load(&g_sim.rate_limited_count));
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
        printf("    %s (%s) : %d messages written\n",
               g_sim.rooms[r].name,
               g_sim.rooms[r].label,
               g_sim.rooms[r].total_written);
    }
    printf("\n  Log exported to: %s\n", LOG_FILENAME);
    printf("=============================================================\n");

    /* ── Cleanup global mutex ────────────────────────────────────────── */
    pthread_mutex_destroy(&g_sim.state_mutex);
    pthread_mutex_destroy(&g_sim.log_mutex);

    return EXIT_SUCCESS;
}
