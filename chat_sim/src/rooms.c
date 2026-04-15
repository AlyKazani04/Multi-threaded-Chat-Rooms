/*
 * rooms.c
 * ─────────────────────────────────────────────────────────────────────────────
 * Project  : OS-Level Chat Server Using Thread Pools
 * Course   : CS2006 Operating Systems – Spring 2026
 *
 * Primary Author : Sana Munir Alam [24K-0573]  ← Synchronisation Lead
 *
 * Responsibilities covered in this file:
 *   • Initialise 3 chat rooms with mutex + two condition variables each
 *   • room_write()  – mutex-protected bounded-buffer write (Producer side)
 *     including cond_wait(buffer_not_full) when the buffer is at capacity
 *   • room_read_latest() – read up to N most-recent messages for the GUI
 *   • All log calls for ACQUIRED LOCK / RELEASED LOCK / BUFFER_FULL / BUFFER_WRAP
 *
 * OS concepts demonstrated:
 *   • Producer-Consumer (Bounded-Buffer) Problem  – Silberschatz §6
 *   • pthread_mutex_t  – mutual exclusion on the circular buffer
 *   • pthread_cond_t   – buffer_not_full  (producer waits when full)
 *                       buffer_not_empty (consumer waits when empty)
 *   • Critical section: message index update + string copy are atomic
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include "chat_sim.h"

/*
 * rooms_init()
 * ─────────────────────────────────────────────────────────────────────────────
 * Owner   : Sana Munir Alam [24K-0573]
 * Purpose : Zero-initialise all three ChatRoom structs and set up their
 *           POSIX synchronisation primitives.
 *
 * Each room gets:
 *   • pthread_mutex_t    – single lock for the circular buffer
 *   • pthread_cond_t cond_not_full  – producers wait here when count == BUFFER_SLOTS
 *   • pthread_cond_t cond_not_empty – consumers wait here when count == 0
 * ─────────────────────────────────────────────────────────────────────────────
 */
void rooms_init(void)
{
    /* Room metadata (name, label) */
    const char *names[]  = { "Room A", "Room B", "Room C" };
    const char *labels[] = { "General", "Priority", "Private" };

    for (int r = 0; r < NUM_ROOMS; r++) {
        ChatRoom *room = &g_sim.rooms[r];

        /* Identity */
        strncpy(room->name,  names[r],  sizeof(room->name)  - 1);
        strncpy(room->label, labels[r], sizeof(room->label) - 1);

        /* Buffer bookkeeping */
        room->head          = 0;
        room->tail          = 0;
        room->count         = 0;
        room->total_written = 0;

        /* Clear message slots */
        memset(room->buffer, 0, sizeof(room->buffer));

        /* POSIX primitives */
        pthread_mutex_init(&room->mutex,          NULL);
        pthread_cond_init (&room->cond_not_full,  NULL);
        pthread_cond_init (&room->cond_not_empty, NULL);
    }

    printf("[INFO] 3 chat rooms initialised: Room A (General), "
           "Room B (Priority), Room C (Private)\n");
}

/*
 * room_write()
 * ─────────────────────────────────────────────────────────────────────────────
 * Owner   : Sana Munir Alam [24K-0573]
 * Purpose : Write one message into the room's circular buffer.
 *           Implements the PRODUCER half of the Bounded-Buffer problem.
 *
 * Algorithm:
 *   1. Lock room->mutex                           ← ACQUIRED LOCK event
 *   2. while buffer is full:
 *         pthread_cond_wait(&cond_not_full, &mutex)  ← BUFFER_FULL event
 *         (This releases the mutex atomically while sleeping, so other
 *          threads can consume and signal cond_not_full.)
 *   3. Write message to buffer[tail], advance tail, increment count
 *      If tail wraps around 0 again → log BUFFER_WRAP event
 *   4. Signal cond_not_empty so any waiting consumer can wake
 *   5. Unlock room->mutex                         ← RELEASED LOCK event
 *
 * Thread-safety invariant:
 *   The message index (tail) update AND the string copy both happen
 *   inside the critical section → no Consumer can read a half-written
 *   message (data integrity guarantee from proposal §4.2).
 *
 * Parameters:
 *   room_id   – 0=A, 1=B, 2=C
 *   sender    – null-terminated client name
 *   msg       – null-terminated message body
 *   thread_id – index of the calling worker (for logging)
 *
 * Returns: true on success, false if simulation stopped before write
 * ─────────────────────────────────────────────────────────────────────────────
 */
bool room_write(int room_id, const char *sender, const char *msg,
                int thread_id)
{
    if (room_id < 0 || room_id >= NUM_ROOMS) return false;

    ChatRoom *room = &g_sim.rooms[room_id];
    char      room_char = 'A' + room_id;

    /* ── Step 1: Acquire mutex ─────────────────────────────────────────── */
    pthread_mutex_lock(&room->mutex);
    logger_log(thread_id, LOG_ACQUIRED_LOCK, room_char, -1, 0, 0, sender);

    /* ── Step 2: Wait while buffer is full (cond_wait releases mutex) ──── */
    while (room->count >= BUFFER_SLOTS) {
        if (!atomic_load(&g_sim.running)) {
            /* Shutdown during wait – release lock and bail out */
            pthread_mutex_unlock(&room->mutex);
            return false;
        }
        logger_log(thread_id, LOG_BUFFER_FULL, room_char, -1, 0, 0, sender);

        pthread_mutex_lock(&g_sim.state_mutex);
        g_sim.workers[thread_id].state = THREAD_WAITING;
        pthread_mutex_unlock(&g_sim.state_mutex);

        /* Atomically releases mutex and blocks; re-acquires on wakeup */
        pthread_cond_wait(&room->cond_not_full, &room->mutex);

        pthread_mutex_lock(&g_sim.state_mutex);
        g_sim.workers[thread_id].state = THREAD_ACTIVE;
        pthread_mutex_unlock(&g_sim.state_mutex);
    }

    /* ── Step 3: Write into the circular buffer (critical section) ──────── */
    ChatMessage *slot = &room->buffer[room->tail];

    strncpy(slot->sender,  sender, sizeof(slot->sender)  - 1);
    strncpy(slot->text,    msg,    sizeof(slot->text)     - 1);
    slot->sender[sizeof(slot->sender) - 1] = '\0';
    slot->text[sizeof(slot->text)     - 1] = '\0';
    slot->timestamp_us = now_us() - g_sim.start_time_us;
    slot->thread_id    = thread_id;

    /* Advance tail pointer (wraps around) */
    bool wrapped = (room->tail + 1 == BUFFER_SLOTS);
    room->tail   = (room->tail + 1) % BUFFER_SLOTS;
    room->count++;
    room->total_written++;

    if (wrapped)
        logger_log(thread_id, LOG_BUFFER_WRAP, room_char, -1,
                   (int)strlen(msg), 0, sender);

    /* ── Step 4: Signal a waiting consumer that data is available ─────── */
    pthread_cond_signal(&room->cond_not_empty);

    /* ── Step 5: Release mutex ─────────────────────────────────────────── */
    pthread_mutex_unlock(&room->mutex);
    logger_log(thread_id, LOG_RELEASED_LOCK, room_char, -1,
               (int)strlen(msg), 0, sender);

    return true;
}

/*
 * room_read_latest()
 * ─────────────────────────────────────────────────────────────────────────────
 * Owner   : Sana Munir Alam [24K-0573]
 * Purpose : Copy up to max_msgs of the most-recent messages from a room's
 *           circular buffer into the caller's array.  Used by the GUI
 *           (gui.c, Adeena Asif) to render the chat panel each frame.
 *
 * Thread-safety: acquires room->mutex for the duration of the copy so
 *                the GUI never sees a half-written ChatMessage struct.
 *
 * Returns : actual number of messages copied (≤ max_msgs)
 * ─────────────────────────────────────────────────────────────────────────────
 */
int room_read_latest(int room_id, ChatMessage *out, int max_msgs)
{
    if (room_id < 0 || room_id >= NUM_ROOMS || max_msgs <= 0) return 0;

    ChatRoom *room = &g_sim.rooms[room_id];

    pthread_mutex_lock(&room->mutex);

    int n = room->count < max_msgs ? room->count : max_msgs;

    /* Walk backwards from (tail-1) to get the most-recent messages first */
    for (int i = 0; i < n; i++) {
        int src = (room->tail - 1 - i + BUFFER_SLOTS) % BUFFER_SLOTS;
        out[i]  = room->buffer[src];
    }

    pthread_mutex_unlock(&room->mutex);
    return n;
}
