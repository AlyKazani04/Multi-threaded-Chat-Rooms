#include "chat_sim.h"

SimState g_sim;

int main(int argc, char **argv) {
   printf("=============================================================\n");
   printf("  OS-Level Chat Server Using Thread Pools\n");
   printf("  CS2006 Operating Systems  |  Spring 2026\n");
   printf("  Group: Aly Kazani [24K-0512]  Sana Alam [24K-0573]  Adeena Asif [24K-0628]\n");
   printf("=============================================================\n\n");

   /* Step 1: Parse CLI */
   parse_args(argc, argv, &g_sim.config);

   printf("[CONFIG] threads=%d  clients=%d  rooms=%d  duration=%ds  rate=%.1f/s  ratelimit=%d msg/s\n\n",
         g_sim.config.num_threads, g_sim.config.num_clients, g_sim.config.num_rooms, g_sim.config.duration_sec, g_sim.config.arrival_rate, g_sim.config.rate_limit_per_sec);

   /* Step 2: Record start time */
   g_sim.start_time_us = now_us();

   /* Step 3: Initialise atomic counters and global mutexes */
   atomic_store(&g_sim.total_messages,    0);
   atomic_store(&g_sim.total_pm,          0);
   atomic_store(&g_sim.active_clients,    0);
   atomic_store(&g_sim.blocked_clients,   0);
   atomic_store(&g_sim.sem_value,         MAX_CLIENTS);
   atomic_store(&g_sim.rate_limited_count,0);
   atomic_store(&g_sim.total_dropped,     0);
   atomic_store(&g_sim.running,           false);
   atomic_store(&g_sim.paused,            false);
   g_sim.speed_multiplier = 1.0f;
   g_sim.throughput_sample_count = 0;
   memset(g_sim.throughput_samples, 0, sizeof(g_sim.throughput_samples));

   pthread_mutex_init(&g_sim.state_mutex, NULL);
   pthread_mutex_init(&g_sim.rand_mutex, NULL);
   srand((unsigned int)time(NULL));

   /* Step 4: Initialise logger */
   logger_init();

   /* Step 5: Initialise chat rooms */
   rooms_init();

   /* Step 6: Initialise thread pool + spawn workers */
   threadpool_init(g_sim.config.num_threads);

   /* Start consumer threads AFTER workers so buffers have readers before producers start writing.  This is the core deadlock prevention. */
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
   pthread_join(gen_tid, NULL);

   /* Step 11: Shut down worker pool – no new room_write() calls after this */
   threadpool_shutdown();
   rooms_consumer_shutdown();
   logger_export();

   /* Step 14: Final statistics */
   printf("\n=============================================================\n");
   printf("  SIMULATION COMPLETE\n");
   printf("=============================================================\n");
   printf("  Total messages sent  : %d\n", atomic_load(&g_sim.total_messages));
   printf("  Private messages     : %d  (direct thread-to-thread)\n", atomic_load(&g_sim.total_pm));
   printf("  Rate-limited drops   : %d  (token-bucket throttle)\n", atomic_load(&g_sim.rate_limited_count));
   printf("  Total dropped msgs   : %d  (rate-limited drops)\n", atomic_load(&g_sim.total_dropped));
   printf("  Duration             : %.2f s\n", (double)(now_us() - g_sim.start_time_us) / 1e6);
   printf("  Worker threads used  : %d\n", g_sim.config.num_threads);
   printf("  Rate limit setting   : %d msg/sec per client\n", g_sim.config.rate_limit_per_sec);
   printf("\n  Per-thread message counts:\n");
   for (int i = 0; i < g_sim.config.num_threads; i++) {
      printf("    Thread-%d : %d msgs sent, %d PMs received\n", i, atomic_load(&g_sim.workers[i].msgs_sent), g_sim.workers[i].pm_total);
   }
   printf("\n  Per-room totals:\n");
   for (int r = 0; r < NUM_ROOMS; r++) {
   printf("    %s (%s) : written=%d  consumed=%d  in_buffer=%d\n", g_sim.rooms[r].name, g_sim.rooms[r].label, g_sim.rooms[r].total_written, g_sim.rooms[r].total_consumed, g_sim.rooms[r].count);
   }

   for (int r = 0; r < NUM_ROOMS; r++) {
   long count = g_sim.rooms[r].mutex_wait_count;
   double avg_us = count > 0 ? (double)g_sim.rooms[r].total_mutex_wait_ns / count / 1000.0 : 0.0;
   printf("    %s: avg mutex wait = %.2f µs (over %ld acquisitions)\n", g_sim.rooms[r].name, avg_us, count);
   }

   printf("\n  Log exported to: %s\n", LOG_FILENAME);
   printf("=============================================================\n");

   /* Cleanup */
   pthread_mutex_destroy(&g_sim.state_mutex);
   pthread_mutex_destroy(&g_sim.rand_mutex);
   pthread_mutex_destroy(&g_sim.log_mutex);

   return EXIT_SUCCESS;
}
