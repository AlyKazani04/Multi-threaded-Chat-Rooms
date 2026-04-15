/*
 * client_gen.c
 * ─────────────────────────────────────────────────────────────────────────────
 * Project  : OS-Level Chat Server Using Thread Pools
 * Course   : CS2006 Operating Systems – Spring 2026
 *
 * Primary Author : Aly Muhammad Kazani [24K-0512]  ← Thread Pool Lead
 *
 * Responsibilities covered in this file:
 *   • client_generator_thread() – spawns simulated client Tasks at a
 *     configurable arrival rate and pushes them onto the task queue
 *   • Message content generator – random selection from phrase pool
 *   • Respects paused flag and speed_multiplier from the GUI slider
 *   • Stops when running == false OR duration_sec has elapsed
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include "chat_sim.h"

/* ── Simulated client names ─────────────────────────────────────────────── */
static const char *CLIENT_NAMES[] = {
    "Alice","Bob","Charlie","Diana","Eve",
    "Frank","Grace","Hank","Iris","Jack",
    "Karen","Leo","Mia","Nick","Olivia",
    "Paul","Quinn","Rose","Sam","Tina"
};
#define CLIENT_NAMES_SZ 20

/* ── Sample message phrases for each room ──────────────────────────────── */
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

/*
 * make_message()
 * ─────────────────────────────────────────────────────────────────────────────
 * Owner   : Aly Muhammad Kazani [24K-0512]
 * Purpose : Build a human-readable fake chat message string for the given
 *           room, client name, and client sequence number.
 * ─────────────────────────────────────────────────────────────────────────────
 */
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
 * ─────────────────────────────────────────────────────────────────────────────
 * Owner   : Aly Muhammad Kazani [24K-0512]
 * Purpose : Simulates arriving client requests.  Runs in its own pthread
 *           (spawned from main.c).  Every iteration:
 *
 *   1. If paused → sleep 100 ms and retry.
 *   2. Build a Task with a random room, random client name, and message.
 *   3. taskqueue_push(&task) → wakes one idle worker via sem_post.
 *   4. Sleep for (1.0 / arrival_rate) seconds, adjusted by speed_multiplier.
 *   5. Exit when (elapsed >= duration_sec) OR running == false.
 *
 * The total client count across the simulation matches config.num_clients
 * repeated in cycles (so with 30 clients and 60 s, each client sends
 * approx. arrival_rate * 60 / 30 messages during the run).
 * ─────────────────────────────────────────────────────────────────────────────
 */
void *client_generator_thread(void *arg)
{
    (void)arg;  /* unused */

    SimConfig *cfg     = &g_sim.config;
    long long  end_us  = g_sim.start_time_us + (long long)cfg->duration_sec * 1000000LL;

    int client_seq = 0;   /* Global sequence counter for log [C###] prefix */

    while (atomic_load(&g_sim.running)) {

        /* ── Check if time limit reached ────────────────────────────── */
        if (now_us() >= end_us) {
            printf("[%08.3f] Duration elapsed – stopping client generator.\n",
                   (double)(now_us() - g_sim.start_time_us) / 1e6);
            atomic_store(&g_sim.running, false);
            break;
        }

        /* ── Respect pause flag (GUI Pause button) ───────────────────── */
        if (atomic_load(&g_sim.paused)) {
            struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000000L }; /* 100ms */
            nanosleep(&ts, NULL);
            continue;
        }

        /* ── Build one Task ─────────────────────────────────────────── */
        int room_id   = client_seq % NUM_ROOMS;
        int name_idx  = client_seq % CLIENT_NAMES_SZ;

        Task t;
        memset(&t, 0, sizeof(t));
        t.client_id = client_seq;
        strncpy(t.sender, CLIENT_NAMES[name_idx], sizeof(t.sender) - 1);

        /* ── §9.2 – Every 8th message is a Private Message ──────────── */
        if (client_seq > 0 && client_seq % 8 == 0 &&
            g_sim.config.num_threads >= 2)
        {
            /* Pick two distinct worker thread indices as sender/receiver */
            int from_t = client_seq % g_sim.config.num_threads;
            int to_t   = (from_t + 1) % g_sim.config.num_threads;

            t.is_private   = true;
            t.room_id      = -1;     /* not a room message */
            t.to_thread_id = to_t;

            /* PM message text */
            snprintf(t.message, sizeof(t.message),
                     "[DM to T%d] Hey T%d, this is a private message from %s (#%d)",
                     to_t, to_t, CLIENT_NAMES[name_idx], client_seq);

            printf("[%08.3f] [PM-GEN] Client-%d (%s) -> T%d (private)\n",
                   (double)(now_us() - g_sim.start_time_us) / 1e6,
                   client_seq, t.sender, to_t);
        }
        else {
            /* Standard room broadcast */
            t.is_private = false;
            t.room_id    = room_id;
            make_message(room_id, client_seq, CLIENT_NAMES[name_idx],
                         t.message, sizeof(t.message));
        }

        /* ── Enqueue – wakes one worker via tasks_available semaphore ─ */
        taskqueue_push(&t);

        /* ── Console output: room messages only (PM has its own print) ── */
        if (!t.is_private) {
            printf("[%08.3f] Client-%d (%s) >> %s: '%s'\n",
                   (double)(now_us() - g_sim.start_time_us) / 1e6,
                   client_seq,
                   t.sender,
                   g_sim.rooms[room_id].name,
                   t.message);
        }

        client_seq++;

        /* ── Arrival delay: 1/rate seconds, adjusted by speed slider ─── */
        float rate  = cfg->arrival_rate > 0 ? cfg->arrival_rate : 5.0f;
        float delay = (1.0f / rate) / g_sim.speed_multiplier;
        if (delay < 0.005f) delay = 0.005f;   /* floor at 5 ms */

        long long delay_us = (long long)(delay * 1e6f);
        struct timespec ts;
        ts.tv_sec  = delay_us / 1000000LL;
        ts.tv_nsec = (delay_us % 1000000LL) * 1000LL;
        nanosleep(&ts, NULL);
    }

    return NULL;
}