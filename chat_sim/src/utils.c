/*
 * utils.c
 * ─────────────────────────────────────────────────────────────────────────────
 * Project  : OS-Level Chat Server Using Thread Pools
 * Course   : CS2006 Operating Systems – Spring 2026
 *
 * Shared Utilities – contributed by all three members
 *
 * Contents:
 *   • parse_args()       – parse CLI flags  (Aly Muhammad Kazani)
 *   • thread_state_name()– enum to string   (Adeena Asif)
 *   • log_event_name()   – enum to string   (Sana Munir Alam)
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include "chat_sim.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * parse_args()
 * ─────────────────────────────────────────────────────────────────────────────
 * Owner   : Aly Muhammad Kazani [24K-0512]
 * Purpose : Parse CLI arguments from proposal §8.1 plus §9.2 additions:
 *
 *   ./chat_sim --threads 8 --clients 50 --rooms 3 --duration 60 --ratelimit 20
 *
 * Unrecognised flags are silently ignored.
 * All values are clamped to safe ranges.
 * ─────────────────────────────────────────────────────────────────────────────
 */
void parse_args(int argc, char **argv, SimConfig *cfg)
{
    /* Defaults */
    cfg->num_threads        = 8;
    cfg->num_clients        = 30;
    cfg->num_rooms          = NUM_ROOMS;   /* always 3 in this build */
    cfg->duration_sec       = 60;
    cfg->arrival_rate       = 5.0f;        /* 5 req/sec default */
    cfg->rate_limit_per_sec = DEFAULT_RATE_LIMIT; /* §9.2: 20 msgs/sec per client */

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
            /* Fixed at 3 – flag accepted but ignored */
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
            /* §9.2 – per-client max messages per second */
            int v = atoi(argv[i + 1]);
            cfg->rate_limit_per_sec = (v < 1) ? 1 : (v > 1000) ? 1000 : v;
            i++;
        }
    }
}

/*
 * thread_state_name()
 * Owner: Adeena Asif [24K-0628]
 * Returns a human-readable string for a ThreadState enum value.
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
 * Owner: Sana Munir Alam [24K-0573]
 * Returns a human-readable string for a LogEventType enum value.
 * These strings appear verbatim in chat_sim.log.txt.
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
        case LOG_PRIVATE_MSG:    return "PRIVATE_MSG";     /* §9.2 */
        case LOG_RATE_LIMITED:   return "RATE_LIMITED";    /* §9.2 */
        default:                 return "UNKNOWN_EVENT";
    }
}
