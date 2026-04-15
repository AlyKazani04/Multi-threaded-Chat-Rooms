/*
 * client_gen.c  [FIXED VERSION]
 * ─────────────────────────────────────────────────────────────────────────────
 * Project  : OS-Level Chat Server Using Thread Pools
 * Course   : CS2006 Operating Systems – Spring 2026
 *
 * Primary Author : Aly Muhammad Kazani [24K-0512]
 *
 * FIXES APPLIED IN THIS FILE:
 *
 *   FIX-M  Private messaging is now RANDOMISED.
 *          Original: from_t = client_seq % num_threads  (always 0,1,2,3…)
 *                    to_t   = (from_t+1) % num_threads  (always 1,2,3,0…)
 *          This is round-robin, not random.  With 8 threads, T0 always
 *          messages T1, T1 always messages T2, etc.
 *          Fix: use safe_rand_range() (see utils.c) for both sender and
 *          receiver with a retry loop to prevent self-messaging.
 *
 *   FIX-N  Self-messaging prevention added — retry loop ensures from_t != to_t.
 *          A cap of 100 retries avoids an infinite loop if num_threads==1.
 *
 *   FIX-O  speed_multiplier float read race fixed.
 *          g_sim.speed_multiplier is written by the GUI thread and read here
 *          with no synchronisation → undefined behaviour on all platforms.
 *          Fix: read it under state_mutex.
 *
 *   FIX-P  Pause loop sleep reduced from 100 ms to 10 ms and running is
 *          re-checked every iteration so shutdown latency is ≤ 10 ms
 *          instead of up to 100 ms.
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include "chat_sim.h"

static const char *CLIENT_NAMES[] = {
    "Alice","Bob","Charlie","Diana","Eve",
    "Frank","Grace","Hank","Iris","Jack",
    "Karen","Leo","Mia","Nick","Olivia",
    "Paul","Quinn","Rose","Sam","Tina"
};
#define CLIENT_NAMES_SZ 20

static const char *ROOM_A_MSGS[] = {
    "Hello everyone!", "What's up?", "Good morning!",
    "Anyone here?", "How's it going?", "Happy to be here!",
    "Nice weather today.", "Let's get started.", "Great to see you all.",
    "This is the general channel."
};
static const char *ROOM_B_MSGS[] = {
    "HIGH PRIORITY: server is slow.", "Attention: deploy in 5 min.",
    "URGENT: Bug in production.", "Priority task assigned.",
    "Critical update needed.", "Escalating this issue.",
    "Please respond ASAP.", "This is a priority message.",
    "Action required immediately.", "Flagging for review."
};
static const char *ROOM_C_MSGS[] = {
    "Private message sent.", "Just between us...",
    "This is confidential.", "Off the record.",
    "Secure channel active.", "Eyes only.",
    "Private briefing time.", "For your eyes only.",
    "Restricted access note.", "Private sync done."
};
#define MSGS_PER_ROOM 10

static void make_message(int room_id, int client_id,
                         const char *name, char *out, size_t out_sz)
{
    const char *phrase;
    switch (room_id) {
        case 0: phrase = ROOM_A_MSGS[client_id % MSGS_PER_ROOM]; break;
        case 1: phrase = ROOM_B_MSGS[client_id % MSGS_PER_ROOM]; break;
        default:phrase = ROOM_C_MSGS[client_id % MSGS_PER_ROOM]; break;
    }
    snprintf(out, out_sz, "[C%03d] %s: %s", client_id, name, phrase);
}

/*
 * client_generator_thread()
 */
void *client_generator_thread(void *arg)
{
    (void)arg;

    SimConfig *cfg    = &g_sim.config;
    long long  end_us = g_sim.start_time_us + (long long)cfg->duration_sec * 1000000LL;
    int client_seq    = 0;

    while (atomic_load(&g_sim.running)) {

        /* Check duration */
        if (now_us() >= end_us) {
            printf("[%08.3f] Duration elapsed – stopping client generator.\n",
                   (double)(now_us() - g_sim.start_time_us) / 1e6);
            atomic_store(&g_sim.running, false);
            break;
        }

        /* FIX-P: pause loop with 10 ms sleep and running re-check */
        if (atomic_load(&g_sim.paused)) {
            struct timespec ts = { .tv_sec = 0, .tv_nsec = 10000000L }; /* 10ms */
            nanosleep(&ts, NULL);
            continue;
        }

        /* Build task */
        int room_id  = client_seq % NUM_ROOMS;
        int name_idx = client_seq % CLIENT_NAMES_SZ;

        Task t;
        memset(&t, 0, sizeof(t));
        t.client_id = client_seq;
        strncpy(t.sender, CLIENT_NAMES[name_idx], sizeof(t.sender) - 1);

        /* Every 8th message is a Private Message */
        if (client_seq > 0 && client_seq % 8 == 0 &&
            cfg->num_threads >= 2)
        {
            /* FIX-M/N: randomised sender and receiver, no self-messaging */
            int num_t   = cfg->num_threads;
            int from_t  = safe_rand_range(num_t);
            int to_t;
            int retries = 0;
            do {
                to_t = safe_rand_range(num_t);
                retries++;
            } while (to_t == from_t && retries < 100);

            /* If all retries exhausted (only possible with 1 thread), wrap */
            if (to_t == from_t)
                to_t = (from_t + 1) % num_t;

            t.is_private   = true;
            t.room_id      = -1;
            t.from_thread_id = from_t;
            t.to_thread_id = to_t;

            snprintf(t.message, sizeof(t.message),
                     "[DM to T%d] Hey T%d, private message from %s (#%d)",
                     to_t, to_t, CLIENT_NAMES[name_idx], client_seq);

            printf("[%08.3f] [PM-GEN] Client-%d (%s, T%d) -> T%d (private)\n",
                   (double)(now_us() - g_sim.start_time_us) / 1e6,
                   client_seq, t.sender, from_t, to_t);
        }
        else {
            t.is_private = false;
            t.room_id    = room_id;
            make_message(room_id, client_seq, CLIENT_NAMES[name_idx],
                         t.message, sizeof(t.message));
        }

        taskqueue_push(&t);

        if (!t.is_private) {
            printf("[%08.3f] Client-%d (%s) >> %s: '%s'\n",
                   (double)(now_us() - g_sim.start_time_us) / 1e6,
                   client_seq,
                   t.sender,
                   g_sim.rooms[room_id].name,
                   t.message);
        }

        client_seq++;

        /* FIX-O: read speed_multiplier under state_mutex to avoid torn reads */
        pthread_mutex_lock(&g_sim.state_mutex);
        float speed = g_sim.speed_multiplier;
        pthread_mutex_unlock(&g_sim.state_mutex);

        float rate  = cfg->arrival_rate > 0 ? cfg->arrival_rate : 5.0f;
        float delay = (1.0f / rate) / speed;
        if (delay < 0.005f) delay = 0.005f;

        long long delay_us = (long long)(delay * 1e6f);
        struct timespec ts;
        ts.tv_sec  = delay_us / 1000000LL;
        ts.tv_nsec = (delay_us % 1000000LL) * 1000LL;
        nanosleep(&ts, NULL);
    }

    return NULL;
}
