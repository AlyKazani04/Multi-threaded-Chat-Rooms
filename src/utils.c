/*
 * utils.c  [FIXED VERSION]
 * ─────────────────────────────────────────────────────────────────────────────
 * Project  : OS-Level Chat Server Using Thread Pools
 * Course   : CS2006 Operating Systems – Spring 2026
 *
 * FIXES APPLIED IN THIS FILE:
 *
 *   FIX-Q  safe_rand_range() added.
 *          rand() has global state and is NOT thread-safe.  Concurrent calls
 *          from multiple worker threads race on its internal state causing
 *          undefined behaviour.
 *          Fix: all rand() calls go through safe_rand_range() which acquires
 *          g_sim.rand_mutex before calling rand().
 *
 *   FIX-V  log_event_name() updated with cases for the four new event types:
 *          LOG_MSG_DROPPED, LOG_BUFFER_RESUME, LOG_SEM_BLOCK.
 *          Without these the log file printed "UNKNOWN_EVENT" for every drop,
 *          buffer-resume, and pre-block event.
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include "chat_sim.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * parse_args()
 */
void parse_args(int argc, char **argv, SimConfig *cfg)
{
    cfg->num_threads        = 8;
    cfg->num_clients        = 30;
    cfg->num_rooms          = NUM_ROOMS;
    cfg->duration_sec       = 60;
    cfg->arrival_rate       = 5.0f;
    cfg->rate_limit_per_sec = DEFAULT_RATE_LIMIT;

    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--threads") == 0) {
            int v = atoi(argv[i + 1]);
            cfg->num_threads = (v < MIN_WORKERS) ? MIN_WORKERS :
                               (v > MAX_WORKERS) ? MAX_WORKERS : v;
            i++;
        } else if (strcmp(argv[i], "--clients") == 0) {
            int v = atoi(argv[i + 1]);
            cfg->num_clients = (v < 1) ? 1 :
                               (v > MAX_CLIENTS) ? MAX_CLIENTS : v;
            i++;
        } else if (strcmp(argv[i], "--rooms") == 0) {
            int v = atoi(argv[i + 1]);
            cfg->num_rooms = (v < 1) ? 1 : (v > NUM_ROOMS) ? NUM_ROOMS : v;
            i++;
        } else if (strcmp(argv[i], "--duration") == 0) {
            int v = atoi(argv[i + 1]);
            cfg->duration_sec = (v < 5) ? 5 : (v > 3600) ? 3600 : v;
            i++;
        } else if (strcmp(argv[i], "--rate") == 0) {
            float v = (float)atof(argv[i + 1]);
            cfg->arrival_rate = (v < 0.5f) ? 0.5f : (v > 50.0f) ? 50.0f : v;
            i++;
        } else if (strcmp(argv[i], "--ratelimit") == 0) {
            int v = atoi(argv[i + 1]);
            cfg->rate_limit_per_sec = (v < 1) ? 1 : (v > 1000) ? 1000 : v;
            i++;
        }
    }
}

/*
 * thread_state_name()
 */
const char *thread_state_name(ThreadState s)
{
    switch (s) {
        case THREAD_IDLE:    return "IDLE";
        case THREAD_ACTIVE:  return "ACTIVE";
        case THREAD_WAITING: return "WAITING";
        default:             return "UNKNOWN";
    }
}

/*
 * log_event_name()
 * FIX-V: added cases for LOG_MSG_DROPPED, LOG_BUFFER_RESUME, LOG_SEM_BLOCK
 */
const char *log_event_name(LogEventType e)
{
    switch (e) {
        case LOG_THREAD_CREATE:  return "THREAD_CREATE";
        case LOG_ACQUIRED_LOCK:  return "ACQUIRED_LOCK";
        case LOG_RELEASED_LOCK:  return "RELEASED_LOCK";
        case LOG_SEM_WAIT:       return "SEM_WAIT";
        case LOG_SEM_POST:       return "SEM_POST";
        case LOG_MSG_BROADCAST:  return "MSG_BROADCAST";
        case LOG_THREAD_EXIT:    return "THREAD_EXIT";
        case LOG_BUFFER_FULL:    return "BUFFER_FULL";
        case LOG_BUFFER_WRAP:    return "BUFFER_WRAP";
        case LOG_PRIVATE_MSG:    return "PRIVATE_MSG";
        case LOG_RATE_LIMITED:   return "RATE_LIMITED";
        case LOG_MSG_DROPPED:    return "MSG_DROPPED";    /* FIX-V */
        case LOG_BUFFER_RESUME:  return "BUFFER_RESUME";  /* FIX-V */
        case LOG_SEM_BLOCK:      return "SEM_BLOCK";      /* FIX-V */
        default:                 return "UNKNOWN_EVENT";
    }
}

/*
 * safe_rand_range()
 * FIX-Q: thread-safe wrapper around rand().
 * Returns a random integer in [0, n).
 * Acquires rand_mutex so concurrent calls from worker threads do not race
 * on rand()'s global state (undefined behaviour without this lock).
 */
int safe_rand_range(int n)
{
    if (n <= 1) return 0;
    pthread_mutex_lock(&g_sim.rand_mutex);
    int v = rand() % n;
    pthread_mutex_unlock(&g_sim.rand_mutex);
    return v;
}
