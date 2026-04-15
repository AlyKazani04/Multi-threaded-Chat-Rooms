/*
 * rooms.c  [FINAL CORRECTED VERSION]
 * ─────────────────────────────────────────────────────────────────────────────
 * Project  : OS-Level Chat Server Using Thread Pools
 * Course   : CS2006 Operating Systems – Spring 2026
 *
 * Primary Author : Sana Munir Alam [24K-0573]
 *
 * CRITICAL FIX: Consumer speed reduced to 500ms per message
 *               This ensures GUI has time to display messages before they
 *               are consumed/drained from the buffer.
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include "chat_sim.h"

/* ── forward declaration ────────────────────────────────────────────────── */
static void *consumer_thread(void *arg);

/*
 * rooms_init()
 * Initialise all three ChatRoom structs and their POSIX primitives.
 */
void rooms_init(void)
{
    const char *names[]  = { "Room A", "Room B", "Room C" };
    const char *labels[] = { "General", "Priority", "Private" };

    for (int r = 0; r < NUM_ROOMS; r++) {
        ChatRoom *room = &g_sim.rooms[r];

        strncpy(room->name,  names[r],  sizeof(room->name)  - 1);
        strncpy(room->label, labels[r], sizeof(room->label) - 1);

        room->head           = 0;
        room->tail           = 0;
        room->count          = 0;
        room->total_written  = 0;
        room->total_consumed = 0;

        memset(room->buffer, 0, sizeof(room->buffer));

        pthread_mutex_init(&room->mutex,          NULL);
        pthread_cond_init (&room->cond_not_full,  NULL);
        pthread_cond_init (&room->cond_not_empty, NULL);
    }

    printf("[INFO] 3 chat rooms initialised: Room A (General), "
           "Room B (Priority), Room C (Private)\n");
}

/*
 * rooms_consumer_init()
 * Starts one consumer_thread per room with SLOW consumption rate.
 * Messages stay visible in GUI for ~25 seconds at 2 msg/sec drain rate.
 */
void rooms_consumer_init(void)
{
    for (int r = 0; r < NUM_ROOMS; r++) {
        int *room_idx = malloc(sizeof(int));
        if (!room_idx) {
            fprintf(stderr, "[FATAL] malloc failed in rooms_consumer_init\n");
            exit(EXIT_FAILURE);
        }
        *room_idx = r;

        int rc = pthread_create(&g_sim.consumer_tids[r], NULL,
                                consumer_thread, room_idx);
        if (rc != 0) {
            fprintf(stderr,
                    "[FATAL] pthread_create failed for consumer room %d (rc=%d)\n",
                    r, rc);
            exit(EXIT_FAILURE);
        }
    }

    printf("[INFO] %d SLOW consumer threads started (500ms per message).\n", NUM_ROOMS);
    printf("[INFO] Messages will remain visible in GUI for ~25 seconds each.\n");
}

/*
 * rooms_consumer_shutdown()
 * Safely stops all consumer threads.
 */
void rooms_consumer_shutdown(void)
{
    /* Wake all sleeping consumers */
    for (int r = 0; r < NUM_ROOMS; r++) {
        pthread_mutex_lock(&g_sim.rooms[r].mutex);
        pthread_cond_broadcast(&g_sim.rooms[r].cond_not_empty);
        pthread_mutex_unlock(&g_sim.rooms[r].mutex);
    }

    for (int r = 0; r < NUM_ROOMS; r++) {
        pthread_join(g_sim.consumer_tids[r], NULL);
        printf("[INFO] Consumer thread for Room %c joined. Drained %d messages.\n",
               'A' + r, g_sim.rooms[r].total_consumed);
    }
}

/*
 * consumer_thread()
 * SLOW consumer - one message every 500ms maximum.
 * This ensures GUI has time to display messages before they're consumed.
 */
static void *consumer_thread(void *arg)
{
    int room_id = *(int *)arg;
    free(arg);

    ChatRoom *room      = &g_sim.rooms[room_id];
    char      room_char = 'A' + room_id;

    printf("[INFO] Consumer for Room %c started (slow mode: 2 msg/sec max)\n", room_char);

    while (1) {
        pthread_mutex_lock(&room->mutex);

        /* Wait while buffer is empty */
        while (room->count == 0 && atomic_load(&g_sim.running)) {
            pthread_cond_wait(&room->cond_not_empty, &room->mutex);
        }

        /* Exit condition: simulation stopped AND buffer drained */
        if (room->count == 0 && !atomic_load(&g_sim.running)) {
            pthread_mutex_unlock(&room->mutex);
            break;
        }

        /* Drain one slot from the head */
        room->head = (room->head + 1) % BUFFER_SLOTS;
        room->count--;
        room->total_consumed++;

        /* Signal that space is now available */
        pthread_cond_signal(&room->cond_not_full);

        pthread_mutex_unlock(&room->mutex);

        /* CRITICAL FIX: Sleep 500ms between consuming messages
         * This gives the GUI plenty of time to display messages
         * before they disappear from the buffer.
         * 
         * With BUFFER_SLOTS=50 and 2 msg/sec drain rate:
         * - Buffer takes ~10 seconds to fill (at 5 msg/sec arrival)
         * - Each message stays visible for ~25 seconds
         * - GUI always has content to show
         */
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 500000000L }; /* 500 ms */
        nanosleep(&ts, NULL);
    }

    printf("[INFO] Consumer for Room %c exited. Total consumed: %d\n",
           room_char, room->total_consumed);

    return NULL;
}

/*
 * room_write()
 * Producer side of the Bounded-Buffer.
 */
bool room_write(int room_id, const char *sender, const char *msg,
                int thread_id)
{
    if (room_id < 0 || room_id >= NUM_ROOMS) return false;

    ChatRoom *room      = &g_sim.rooms[room_id];
    char      room_char = 'A' + room_id;

    pthread_mutex_lock(&room->mutex);
    logger_log(thread_id, LOG_ACQUIRED_LOCK, room_char, -1, 0, 0, sender);

    /* Wait while buffer is full (demonstrates condition variable) */
    while (room->count >= BUFFER_SLOTS) {
        if (!atomic_load(&g_sim.running)) {
            pthread_mutex_unlock(&room->mutex);
            return false;
        }

        logger_log(thread_id, LOG_BUFFER_FULL, room_char, -1, 0, 0, sender);

        pthread_mutex_lock(&g_sim.state_mutex);
        g_sim.workers[thread_id].state = THREAD_WAITING;
        pthread_mutex_unlock(&g_sim.state_mutex);

        /* Atomically releases mutex and blocks; consumer will signal us */
        pthread_cond_wait(&room->cond_not_full, &room->mutex);

        /* Log that we resumed */
        logger_log(thread_id, LOG_BUFFER_RESUME, room_char, -1, 0, 0, sender);

        pthread_mutex_lock(&g_sim.state_mutex);
        g_sim.workers[thread_id].state = THREAD_ACTIVE;
        pthread_mutex_unlock(&g_sim.state_mutex);
    }

    /* Write into the circular buffer (critical section) */
    ChatMessage *slot = &room->buffer[room->tail];

    strncpy(slot->sender, sender, sizeof(slot->sender) - 1);
    strncpy(slot->text,   msg,    sizeof(slot->text)   - 1);
    slot->sender[sizeof(slot->sender) - 1] = '\0';
    slot->text[sizeof(slot->text)     - 1] = '\0';
    slot->timestamp_us = now_us() - g_sim.start_time_us;
    slot->thread_id    = thread_id;

    bool wrapped = (room->tail + 1 == BUFFER_SLOTS);
    room->tail   = (room->tail + 1) % BUFFER_SLOTS;
    room->count++;
    room->total_written++;

    if (wrapped)
        logger_log(thread_id, LOG_BUFFER_WRAP, room_char, -1,
                   (int)strlen(msg), 0, sender);

    /* Signal consumer that data is available */
    pthread_cond_signal(&room->cond_not_empty);

    pthread_mutex_unlock(&room->mutex);
    logger_log(thread_id, LOG_RELEASED_LOCK, room_char, -1,
               (int)strlen(msg), 0, sender);

    return true;
}

/*
 * room_read_latest()
 * Copy up to max_msgs recent messages into the caller's array (for GUI).
 * This does NOT consume/modify the buffer.
 */
int room_read_latest(int room_id, ChatMessage *out, int max_msgs)
{
    if (room_id < 0 || room_id >= NUM_ROOMS || max_msgs <= 0) return 0;

    ChatRoom *room = &g_sim.rooms[room_id];

    pthread_mutex_lock(&room->mutex);

    int n = room->count < max_msgs ? room->count : max_msgs;

    /* Walk backwards from (tail-1) to get the most-recent messages */
    for (int i = 0; i < n; i++) {
        int src = (room->tail - 1 - i + BUFFER_SLOTS) % BUFFER_SLOTS;
        out[i]  = room->buffer[src];
    }

    pthread_mutex_unlock(&room->mutex);
    return n;
}