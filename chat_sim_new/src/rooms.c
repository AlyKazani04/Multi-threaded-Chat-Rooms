/*
 * rooms.c  [FIXED VERSION]
 * ─────────────────────────────────────────────────────────────────────────────
 * Project  : OS-Level Chat Server Using Thread Pools
 * Course   : CS2006 Operating Systems – Spring 2026
 *
 * Primary Author : Sana Munir Alam [24K-0573]
 *
 * FIXES APPLIED IN THIS FILE:
 *
 *   FIX-A  rooms_consumer_init() — starts one consumer_thread per room.
 *          Without this, no thread ever reads from the buffer so count can
 *          only increase; once it reaches BUFFER_SLOTS every producer blocks
 *          on cond_not_full forever → guaranteed deadlock under load.
 *
 *   FIX-B  consumer_thread body — drains one slot from the head, decrements
 *          count, increments total_consumed, then signals cond_not_full.
 *          Signalling cond_not_full here is the ONLY correct place; the
 *          original code never called it at all.
 *
 *   FIX-C  room_write() while-loop — added running check inside the wait so
 *          a producer stuck on cond_not_full exits cleanly on shutdown
 *          instead of spinning with running=false.
 *
 *   FIX-D  room_write() — added LOG_BUFFER_RESUME after cond_wait returns so
 *          the log shows both sides of a buffer-full event (previously only
 *          BUFFER_FULL was logged, never the wakeup).
 *
 *   FIX-S  rooms_consumer_shutdown() — broadcasts cond_not_empty to all
 *          rooms so sleeping consumers wake, check running==false, and exit.
 *          Called from main() AFTER threadpool_shutdown() to avoid killing
 *          consumers while a worker is still in room_write().
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include "chat_sim.h"

/* ── forward declaration ────────────────────────────────────────────────── */
static void *consumer_thread(void *arg);  /* FIX-A */

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
        room->total_consumed = 0;   /* FIX-A: initialise drain counter */

        memset(room->buffer, 0, sizeof(room->buffer));

        pthread_mutex_init(&room->mutex,          NULL);
        pthread_cond_init (&room->cond_not_full,  NULL);
        pthread_cond_init (&room->cond_not_empty, NULL);
    }

    printf("[INFO] 3 chat rooms initialised: Room A (General), "
           "Room B (Priority), Room C (Private)\n");
}

/* ─────────────────────────────────────────────────────────────────────────────
 * FIX-A  rooms_consumer_init()
 * ─────────────────────────────────────────────────────────────────────────────
 * Starts one consumer_thread per room.  Must be called AFTER rooms_init()
 * and threadpool_init() but BEFORE the first task is pushed.
 *
 * Without consumer threads the buffer's count can only ever increase.
 * Once it hits BUFFER_SLOTS=50, every room_write() call blocks on
 * cond_not_full and nothing ever signals it → guaranteed deadlock.
 * ─────────────────────────────────────────────────────────────────────────────
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

    printf("[INFO] %d consumer threads started (one per room).\n", NUM_ROOMS);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * FIX-S  rooms_consumer_shutdown()
 * ─────────────────────────────────────────────────────────────────────────────
 * Must be called AFTER threadpool_shutdown() (all workers have exited,
 * so no new room_write() calls are possible) to safely drain and join
 * consumer threads.
 *
 * Shutdown order matters:
 *   1. Join gen_tid            → no new tasks arrive
 *   2. threadpool_shutdown()   → no new room_write() calls
 *   3. rooms_consumer_shutdown() → safe to drain and join consumers
 *
 * If consumers are stopped BEFORE workers, a worker blocking on
 * cond_not_full in room_write() will never be woken → secondary deadlock.
 * ─────────────────────────────────────────────────────────────────────────────
 */
void rooms_consumer_shutdown(void)
{
    /* running is already false here; broadcast wakes sleeping consumers */
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

/* ─────────────────────────────────────────────────────────────────────────────
 * FIX-B  consumer_thread()
 * ─────────────────────────────────────────────────────────────────────────────
 * The consumer half of the Bounded-Buffer pattern.  One instance per room.
 *
 * Loop:
 *   1. Lock room->mutex
 *   2. while count == 0 AND running: cond_wait(cond_not_empty)
 *   3. If count == 0 and !running → exit (drain done)
 *   4. Advance head, decrement count, increment total_consumed
 *   5. Signal cond_not_full  ← THIS IS THE ONLY CORRECT PLACE TO SIGNAL IT
 *   6. Unlock; sleep 1 ms to avoid starving producers on single-core hosts
 *
 * cond_not_full is ONLY signalled here — by the consumer after freeing a
 * slot.  The original code never signalled it at all, which is why every
 * producer eventually blocked forever once the buffer filled.
 * ─────────────────────────────────────────────────────────────────────────────
 */
static void *consumer_thread(void *arg)
{
    int room_id = *(int *)arg;
    free(arg);

    ChatRoom *room      = &g_sim.rooms[room_id];
    char      room_char = 'A' + room_id;

    while (1) {
        pthread_mutex_lock(&room->mutex);

        /* FIX-B: wait while buffer is empty */
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

        /* FIX-B: signal cond_not_full – this is the fix for the deadlock.
         * A producer blocked in room_write() on cond_not_full wakes here. */
        pthread_cond_signal(&room->cond_not_full);

        pthread_mutex_unlock(&room->mutex);

        /* Small sleep prevents consumer from starving producers on single-core */
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000L }; /* 1 ms */
        nanosleep(&ts, NULL);

        (void)room_char; /* suppress unused warning if logging is removed */
    }

    return NULL;
}

/*
 * room_write()
 * ─────────────────────────────────────────────────────────────────────────────
 * Producer side of the Bounded-Buffer.
 *
 * FIXES:
 *   FIX-C: added !running check INSIDE the while loop so shutdown can
 *           interrupt a producer waiting on cond_not_full.
 *   FIX-D: added LOG_BUFFER_RESUME after cond_wait returns so the log
 *           shows both sides of a buffer-full stall.
 * ─────────────────────────────────────────────────────────────────────────────
 */
bool room_write(int room_id, const char *sender, const char *msg,
                int thread_id)
{
    if (room_id < 0 || room_id >= NUM_ROOMS) return false;

    ChatRoom *room      = &g_sim.rooms[room_id];
    char      room_char = 'A' + room_id;

    pthread_mutex_lock(&room->mutex);
    logger_log(thread_id, LOG_ACQUIRED_LOCK, room_char, -1, 0, 0, sender);

    /* Wait while buffer is full */
    while (room->count >= BUFFER_SLOTS) {
        /* FIX-C: check running INSIDE the loop before sleeping */
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

        /* FIX-D: log that the producer resumed after the buffer freed up */
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
