#include "chat_sim.h"

bool ratelimit_check(int thread_id){
    if (thread_id < 0 || thread_id >= MAX_WORKERS) return true;

    WorkerThread *w = &g_sim.workers[thread_id];
    
    /* Read rate limit under mutex to prevent tearing */
    pthread_mutex_lock(&g_sim.state_mutex);
    int limit = g_sim.config.rate_limit_per_sec;
    pthread_mutex_unlock(&g_sim.state_mutex);
    
    if (limit <= 0) return true;

    long long now = now_us();

    if (w->rate_last_us == 0) {
        w->rate_last_us = now;
        w->rate_tokens = limit;
        return true;
    }

    long long elapsed_us = now - w->rate_last_us;
    
    if (elapsed_us > 0) {
        /* How many tokens to add based on elapsed time */
        int add = (int)((double)elapsed_us * limit / 1000000.0);
        
        /* ONLY update rate_last_us if we actually added tokens */
        if (add > 0) {
            w->rate_tokens += add;
            if (w->rate_tokens > limit) w->rate_tokens = limit;
            w->rate_last_us = now;  /* ← Only update when tokens were added */
        }
    }

    /* Check and consume token */
    if (w->rate_tokens >= 1) {
        w->rate_tokens--;
        return true;
    }

    /* Rate limit exceeded - log and return false */
    atomic_fetch_add(&g_sim.rate_limited_count, 1);
    
    char detail[48];
    snprintf(detail, sizeof(detail), "dropped: %d msg/s limit", limit);
    logger_log(thread_id, LOG_RATE_LIMITED, '-', -1, 0, atomic_load(&w->msgs_sent), detail);

    static int drop_counter[MAX_WORKERS] = {0};
    if (++drop_counter[thread_id] % 10 == 1) {
        printf("[%08.3f] [RATE-LIMIT] T%d throttled (limit=%d msg/s, tokens=%d)\n", (double)(now_us() - g_sim.start_time_us) / 1e6,  thread_id, limit, w->rate_tokens);
    }

    return false;
}