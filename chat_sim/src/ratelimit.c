/*
 * ratelimit.c
 * ─────────────────────────────────────────────────────────────────────────────
 * Project  : OS-Level Chat Server Using Thread Pools
 * Course   : CS2006 Operating Systems – Spring 2026
 *
 * Primary Author : Aly Muhammad Kazani [24K-0512]  ← Thread Pool Lead
 *
 * §9.2 Additional Feature – Rate-Limiting Per Client Thread
 * ─────────────────────────────────────────────────────────────────────────────
 * Implements a token-bucket rate limiter for each worker thread.
 *
 * Algorithm (Token Bucket):
 *   Each thread has a bucket of tokens (initially full at rate_limit_per_sec).
 *   Sending one message consumes one token.
 *   Tokens are refilled at a rate of rate_limit_per_sec per second.
 *   If the bucket is empty, the message is DROPPED and logged as
 *   LOG_RATE_LIMITED – the thread does NOT block (non-blocking policy).
 *
 * Why non-blocking?
 *   Blocking on rate-limit would make threads starve the task queue.
 *   Dropping and logging is consistent with real-world rate limiting
 *   (HTTP 429 Too Many Requests), and the proposal says "configurable
 *   max messages per second" without specifying blocking vs dropping.
 *
 * Configuration:
 *   --ratelimit N  (CLI flag, default DEFAULT_RATE_LIMIT = 20 msgs/sec)
 *   The limit can also be changed at runtime via the GUI rate slider,
 *   which adjusts config.rate_limit_per_sec.
 *
 * OS concepts demonstrated:
 *   • Fine-grained per-thread state (no shared lock needed for the check –
 *     each thread only reads its own rate_tokens/rate_last_us fields)
 *   • clock_gettime() / gettimeofday() for microsecond-resolution timing
 *   • Atomic comparison to avoid a mutex (read-own-field pattern)
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include "chat_sim.h"

/*
 * ratelimit_check()
 * ─────────────────────────────────────────────────────────────────────────────
 * Owner   : Aly Muhammad Kazani [24K-0512]
 * Purpose : Check whether thread thread_id is allowed to send a message now.
 *
 *   1. Compute elapsed time since last send (microseconds).
 *   2. Refill tokens proportionally: elapsed_us * rate / 1e6.
 *   3. If tokens >= 1 → allow and consume one token → return true.
 *   4. If tokens == 0 → drop → log LOG_RATE_LIMITED → return false.
 *
 * Thread-safety:
 *   Each worker only calls ratelimit_check() for itself (thread_id == own
 *   index), so no two threads ever write to the same rate_tokens field
 *   concurrently.  No mutex is needed.
 *
 * Parameters:
 *   thread_id – worker index (same as index passed to worker_thread)
 *
 * Returns : true  → message allowed
 *           false → message should be dropped (rate limit exceeded)
 * ─────────────────────────────────────────────────────────────────────────────
 */
bool ratelimit_check(int thread_id)
{
    if (thread_id < 0 || thread_id >= MAX_WORKERS) return true; /* safety */

    WorkerThread *w   = &g_sim.workers[thread_id];
    int limit         = g_sim.config.rate_limit_per_sec;

    if (limit <= 0) return true;   /* rate limiting disabled */

    long long now = now_us();

    /* ── First call initialisation ───────────────────────────────────── */
    if (w->rate_last_us == 0) {
        w->rate_last_us = now;
        w->rate_tokens  = limit;   /* start with a full bucket */
    }

    /* ── Token refill: add tokens for time elapsed since last check ───── */
    long long elapsed_us = now - w->rate_last_us;
    if (elapsed_us > 0) {
        /* tokens_to_add = elapsed_seconds * limit */
        int add = (int)((double)elapsed_us * limit / 1e6);
        w->rate_tokens += add;
        if (w->rate_tokens > limit) w->rate_tokens = limit;  /* cap at bucket size */
        w->rate_last_us = now;
    }

    /* ── Check and consume ───────────────────────────────────────────── */
    if (w->rate_tokens >= 1) {
        w->rate_tokens--;
        return true;   /* ← allowed */
    }

    /* ── Rate limit exceeded – log and drop ─────────────────────────── */
    atomic_fetch_add(&g_sim.rate_limited_count, 1);
    logger_log(thread_id, LOG_RATE_LIMITED, '-',
               -1, 0, atomic_load(&w->msgs_sent),
               "dropped: rate limit exceeded");

    printf("[%08.3f] [RATE-LIMIT] T%d throttled (limit=%d msg/s)\n",
           (double)(now_us() - g_sim.start_time_us) / 1e6,
           thread_id, limit);

    return false;   /* ← dropped */
}
