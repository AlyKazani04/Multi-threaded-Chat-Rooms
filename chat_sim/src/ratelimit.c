// ratelimit.c
// -----------
// This is the 9.2 additional feature -- per-client rate limiting.
//
// Algorithm: Token Bucket
//    each thread has a bucket of tokens (max = rate_limit_per_sec)
//    sending a message costs 1 token
//    tokens refill over time at rate_limit_per_sec per second
//    if bucket is empty when we try to send --> DROP the message
//
// Why drop instead of block?
//    blocking on rate limit would make the worker thread hold its
//    admission semaphore slot while doing nothing. that would starve
//    other clients. dropping is the real-world approach (like HTTP 429).
//    mention this design choice in the report.
//
// No mutex needed here -- each worker only calls ratelimit_check
// with its own thread index, so no two threads touch the same
// rate_tokens / rate_last_us fields at the same time.
//
// Functions to implement:
//
// --- ratelimit_check(int thread_id) ---
//    if thread_id out of range: return true (allow, safety fallback)
//    get w = &g_sim.workers[thread_id]
//    get limit = g_sim.config.rate_limit_per_sec
//    if limit <= 0: return true (rate limiting disabled)
//
//    now = now_us()
//
//    first call init:
//       if w->rate_last_us == 0:
//          w->rate_last_us = now
//          w->rate_tokens = limit  (start with full bucket)
//
//    token refill:
//       elapsed_us = now - w->rate_last_us
//       tokens_to_add = (double)elapsed_us * limit / 1e6
//       w->rate_tokens += tokens_to_add
//       cap at limit (dont overflow bucket)
//       w->rate_last_us = now
//
//    check and consume:
//       if w->rate_tokens >= 1:
//          w->rate_tokens--
//          return true   (allowed)
//
//    if we get here: bucket empty, rate exceeded
//       atomic_fetch_add(&g_sim.rate_limited_count, 1)
//       log LOG_RATE_LIMITED event
//       print "[RATE-LIMIT] T%d throttled" to console
//       return false   (drop this message)
//
// The GUI has a slider to change config.rate_limit_per_sec at runtime.
// The check reads this value fresh each call so changes take effect immediately.
// To SEE rate limiting in action run with --rate 50 --ratelimit 3