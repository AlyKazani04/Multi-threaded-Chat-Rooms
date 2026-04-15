/*
 * ratelimit.c  [FULLY FIXED VERSION]
 * ─────────────────────────────────────────────────────────────────────────────
 * Project  : OS-Level Chat Server Using Thread Pools
 * Course   : CS2006 Operating Systems – Spring 2026
 *
 * Primary Author : Aly Muhammad Kazani [24K-0512]
 *
 * CRITICAL FIXES APPLIED:
 *
 *   FIX-RL1: rate_last_us update moved INSIDE add>0 condition
 *            Original updated rate_last_us on EVERY call with elapsed_us>0,
 *            even when add==0 tokens were added. This caused the timer to
 *            reset without refilling, leading to permanent starvation.
 *
 *   FIX-RL2: Added mutex protection for reading rate_limit_per_sec
 *            GUI thread modifies this while workers read it → potential tearing.
 *
 *   FIX-RL3: Proper logging of RATE_LIMITED events (was being overwritten
 *            in ring buffer due to log spam)
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include "chat_sim.h"

/*
 * ratelimit_check()
 * Token-bucket rate limiter with CORRECT refill timing.
 *
 * The critical fix: rate_last_us is ONLY updated when we actually add
 * tokens to the bucket. Previously it was updated on every call with
 * elapsed_us>0, even when add==0, which reset the measurement interval
 * without adding tokens → starvation.
 */
bool ratelimit_check(int thread_id)
{
    if (thread_id < 0 || thread_id >= MAX_WORKERS) return true;

    WorkerThread *w = &g_sim.workers[thread_id];
    
    /* FIX-RL2: Read rate limit under mutex to prevent tearing */
    pthread_mutex_lock(&g_sim.state_mutex);
    int limit = g_sim.config.rate_limit_per_sec;
    pthread_mutex_unlock(&g_sim.state_mutex);
    
    if (limit <= 0) return true;

    long long now = now_us();

    /* First call initialisation (safety net - should be set in threadpool_init) */
    if (w->rate_last_us == 0) {
        w->rate_last_us = now;
        w->rate_tokens = limit;
        return true;
    }

    /* Calculate elapsed time since last refill */
    long long elapsed_us = now - w->rate_last_us;
    
    if (elapsed_us > 0) {
        /* How many tokens to add based on elapsed time */
        int add = (int)((double)elapsed_us * limit / 1000000.0);
        
        /* FIX-RL1: ONLY update rate_last_us if we actually added tokens */
        if (add > 0) {
            w->rate_tokens += add;
            if (w->rate_tokens > limit) w->rate_tokens = limit;
            w->rate_last_us = now;  /* ← Only update when tokens were added */
        }
        /* If add == 0, rate_last_us stays the same so time accumulates */
    }

    /* Check and consume token */
    if (w->rate_tokens >= 1) {
        w->rate_tokens--;
        return true;
    }

    /* Rate limit exceeded - log and return false */
    atomic_fetch_add(&g_sim.rate_limited_count, 1);
    
    /* FIX-RL3: Log the rate limit event with meaningful detail */
    char detail[48];
    snprintf(detail, sizeof(detail), "dropped: %d msg/s limit", limit);
    logger_log(thread_id, LOG_RATE_LIMITED, '-', -1, 0,
               atomic_load(&w->msgs_sent), detail);

    /* Console output for debugging (only print every 10 drops to avoid spam) */
    static int drop_counter[MAX_WORKERS] = {0};
    if (++drop_counter[thread_id] % 10 == 1) {
        printf("[%08.3f] [RATE-LIMIT] T%d throttled (limit=%d msg/s, tokens=%d)\n",
               (double)(now_us() - g_sim.start_time_us) / 1e6,
               thread_id, limit, w->rate_tokens);
    }

    return false;
}