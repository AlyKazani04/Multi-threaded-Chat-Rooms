/*
 * threadpool.c  [FIXED VERSION]
 * ─────────────────────────────────────────────────────────────────────────────
 * Project  : OS-Level Chat Server Using Thread Pools
 * Course   : CS2006 Operating Systems – Spring 2026
 *
 * Primary Author : Aly Muhammad Kazani [24K-0512]
 *
 * FIXES APPLIED IN THIS FILE:
 *
 *   FIX-G  Thread state TOCTOU race removed.
 *          Original code called sem_getvalue() then checked if sv==0 to
 *          decide whether to set THREAD_WAITING BEFORE calling sem_wait().
 *          Between those two lines another thread could grab the last slot,
 *          so a thread that will block showed ACTIVE and one that won't block
 *          showed WAITING — both wrong.
 *          Fix: always set WAITING before sem_wait, always set ACTIVE after,
 *          unconditionally.  The state accurately reflects what the thread
 *          IS DOING, not what it guessed it might do.
 *
 *   FIX-F  Dropped messages are now visible.
 *          When ratelimit_check() returns false the message was already
 *          popped and the worker had already been counted ACTIVE; the drop
 *          was invisible to the GUI.  Now LOG_MSG_DROPPED is emitted and
 *          total_dropped is incremented on every drop.
 *
 *   FIX-I  LOG_SEM_BLOCK emitted BEFORE sem_wait (pre-block),
 *          LOG_SEM_WAIT kept for AFTER sem_wait (post-admit).
 *          Previously LOG_SEM_WAIT was used for both, making it impossible
 *          to distinguish "I am about to block" from "I was just admitted".
 *
 *   FIX-L  rate_tokens initialised in threadpool_init() to rate_limit_per_sec
 *          so the very first message from each thread is never falsely dropped.
 *          (Original: tokens=0 on first call when elapsed_us==0 → refill of 0
 *           → immediate drop of message #1.)
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include "chat_sim.h"

static void *worker_thread(void *arg);

static const char *CLIENT_NAMES[20] = {
    "Alice","Bob","Charlie","Diana","Eve",
    "Frank","Grace","Hank","Iris","Jack",
    "Karen","Leo","Mia","Nick","Olivia",
    "Paul","Quinn","Rose","Sam","Tina"
};

/*
 * threadpool_init()
 */
void threadpool_init(int num_workers)
{
    if (num_workers < MIN_WORKERS) num_workers = MIN_WORKERS;
    if (num_workers > MAX_WORKERS) num_workers = MAX_WORKERS;

    g_sim.config.num_threads = num_workers;

    /* Init task queue */
    TaskQueue *q = &g_sim.queue;
    q->head  = 0;
    q->tail  = 0;
    q->count = 0;
    pthread_mutex_init(&q->mutex, NULL);
    sem_init(&q->tasks_available, 0, 0);

    /* Admission semaphore */
    sem_init(&g_sim.admission_sem, 0, MAX_CLIENTS);
    atomic_store(&g_sim.sem_value, MAX_CLIENTS);

    atomic_store(&g_sim.running, true);
    atomic_store(&g_sim.paused,  false);

    privmsg_init();

    /* Spawn worker threads */
    for (int i = 0; i < num_workers; i++) {
        WorkerThread *w = &g_sim.workers[i];
        w->index          = i;
        w->state          = THREAD_IDLE;
        w->room_assigned  = -1;
        atomic_store(&w->msgs_sent, 0);
        w->current_sender[0] = '\0';
        w->last_active_us    = 0;

        /* FIX-L: initialise token bucket to full so first message is never
         * incorrectly dropped due to zero elapsed time on first call */
        w->rate_tokens  = g_sim.config.rate_limit_per_sec;
        w->rate_last_us = now_us();

        int *idx = malloc(sizeof(int));
        *idx = i;

        int rc = pthread_create(&w->tid, NULL, worker_thread, idx);
        if (rc != 0) {
            fprintf(stderr, "[FATAL] pthread_create failed for worker %d (rc=%d)\n", i, rc);
            exit(EXIT_FAILURE);
        }

        logger_log(i, LOG_THREAD_CREATE, '-', MAX_CLIENTS, 0, 0,
                   CLIENT_NAMES[i % CLIENT_NAMES_COUNT]);
    }

    printf("[%08.3f] Thread pool initialized: %d workers, semaphore=%d\n",
           (double)(now_us() - g_sim.start_time_us) / 1e6,
           num_workers, MAX_CLIENTS);
}

/*
 * threadpool_shutdown()
 * Set running=false, unblock workers, broadcast room conds, join all.
 */
void threadpool_shutdown(void)
{
    atomic_store(&g_sim.running, false);
    atomic_store(&g_sim.paused,  false);

    /* Wake every potentially-blocked worker */
    for (int i = 0; i < g_sim.config.num_threads; i++)
        sem_post(&g_sim.queue.tasks_available);

    /* Unblock any room condition-variable waits */
    for (int r = 0; r < NUM_ROOMS; r++) {
        pthread_mutex_lock(&g_sim.rooms[r].mutex);
        pthread_cond_broadcast(&g_sim.rooms[r].cond_not_full);
        pthread_cond_broadcast(&g_sim.rooms[r].cond_not_empty);
        pthread_mutex_unlock(&g_sim.rooms[r].mutex);
    }

    for (int i = 0; i < g_sim.config.num_threads; i++) {
        pthread_join(g_sim.workers[i].tid, NULL);
        logger_log(i, LOG_THREAD_EXIT, '-', 0, 0,
                   atomic_load(&g_sim.workers[i].msgs_sent), "shutdown");
    }

    pthread_mutex_destroy(&g_sim.queue.mutex);
    sem_destroy(&g_sim.queue.tasks_available);
    sem_destroy(&g_sim.admission_sem);

    for (int i = 0; i < g_sim.config.num_threads; i++)
        pthread_mutex_destroy(&g_sim.workers[i].pm_mutex);

    printf("[INFO] Thread pool shut down cleanly.\n");
}

/*
 * taskqueue_push()
 */
void taskqueue_push(Task *t)
{
    TaskQueue *q = &g_sim.queue;

    pthread_mutex_lock(&q->mutex);

    if (q->count >= TASK_QUEUE_CAP) {
        q->head = (q->head + 1) % TASK_QUEUE_CAP;
        q->count--;
    }

    q->tasks[q->tail] = *t;
    q->tail           = (q->tail + 1) % TASK_QUEUE_CAP;
    q->count++;

    pthread_mutex_unlock(&q->mutex);

    sem_post(&q->tasks_available);
}

/*
 * taskqueue_pop()
 */
bool taskqueue_pop(Task *out)
{
    TaskQueue *q = &g_sim.queue;

    pthread_mutex_lock(&q->mutex);
    if (q->count == 0) {
        pthread_mutex_unlock(&q->mutex);
        return false;
    }

    *out    = q->tasks[q->head];
    q->head = (q->head + 1) % TASK_QUEUE_CAP;
    q->count--;

    pthread_mutex_unlock(&q->mutex);
    return true;
}

/*
 * worker_thread()
 * ─────────────────────────────────────────────────────────────────────────────
 * Core worker loop.  See FIX-G and FIX-F notes at the top of this file.
 * ─────────────────────────────────────────────────────────────────────────────
 */
// static void *worker_thread(void *arg)
// {
//     int idx = *(int *)arg;
//     free(arg);

//     WorkerThread *self = &g_sim.workers[idx];

//     while (atomic_load(&g_sim.running)) {

//         /* 1. Block until a task is available */
//         sem_wait(&g_sim.queue.tasks_available);

//         if (!atomic_load(&g_sim.running)) break;

//         /* 2. Pop the task */
//         Task t;
//         if (!taskqueue_pop(&t)) continue;

//         /* 3. Admission control
//          *
//          * FIX-G: TOCTOU race removed.
//          * Always set WAITING before sem_wait, always set ACTIVE after —
//          * unconditionally.  Do NOT read sem_getvalue to guess whether we
//          * will block; that guess races with other threads and produces wrong
//          * GUI state.
//          *
//          * FIX-I: Use LOG_SEM_BLOCK (pre-wait) and LOG_SEM_WAIT (post-admit)
//          * so the log clearly distinguishes the two events.
//          */
//         int sv;
//         sem_getvalue(&g_sim.admission_sem, &sv);
//         logger_log(idx, LOG_SEM_BLOCK, '-', sv, 0, 0, "about to wait");

//         /* FIX-G: set WAITING unconditionally BEFORE sem_wait */
//         pthread_mutex_lock(&g_sim.state_mutex);
//         self->state = THREAD_WAITING;
//         pthread_mutex_unlock(&g_sim.state_mutex);

//         atomic_fetch_add(&g_sim.blocked_clients, 1);

//         sem_wait(&g_sim.admission_sem);   /* may or may not block */

//         atomic_fetch_sub(&g_sim.blocked_clients, 1);

//         sem_getvalue(&g_sim.admission_sem, &sv);
//         atomic_store(&g_sim.sem_value, sv);
//         atomic_fetch_add(&g_sim.active_clients, 1);

//         /* FIX-G: set ACTIVE unconditionally AFTER sem_wait */
//         pthread_mutex_lock(&g_sim.state_mutex);
//         self->state          = THREAD_ACTIVE;
//         self->room_assigned  = t.is_private ? -2 : t.room_id;
//         strncpy(self->current_sender, t.sender, sizeof(self->current_sender) - 1);
//         self->current_sender[sizeof(self->current_sender) - 1] = '\0';
//         self->last_active_us = now_us();
//         pthread_mutex_unlock(&g_sim.state_mutex);

//         logger_log(idx, LOG_SEM_WAIT, '-', sv, 0, 0, "ADMITTED");

//         /* 4. Rate-limit check
//          *
//          * FIX-F: dropped messages now emit LOG_MSG_DROPPED and increment
//          * total_dropped so the GUI and log reflect every drop.
//          */
//         if (!ratelimit_check(idx)) {
//             /* Emit an explicit drop event */
//             logger_log(idx, LOG_MSG_DROPPED, t.is_private ? 'P' : ('A' + t.room_id),
//                        sv, 0, atomic_load(&self->msgs_sent), t.sender);
//             atomic_fetch_add(&g_sim.total_dropped, 1);

//             sem_post(&g_sim.admission_sem);
//             sem_getvalue(&g_sim.admission_sem, &sv);
//             atomic_store(&g_sim.sem_value, sv);
//             atomic_fetch_sub(&g_sim.active_clients, 1);

//             pthread_mutex_lock(&g_sim.state_mutex);
//             self->state         = THREAD_IDLE;
//             self->room_assigned = -1;
//             pthread_mutex_unlock(&g_sim.state_mutex);

//             sched_yield();
//             continue;
//         }

//         /* 5. Route: Private Message OR Room Broadcast */
//         if (t.is_private) {
//             privmsg_send(t.from_thread_id, t.to_thread_id, t.sender, t.message);
//         } else {
//             room_write(t.room_id, t.sender, t.message, idx);
//         }

//         /* 6. Increment counters */
//         int sent = atomic_fetch_add(&self->msgs_sent, 1) + 1;
//         atomic_fetch_add(&g_sim.total_messages, 1);
//         self->last_active_us = now_us();

//         char room_char = t.is_private ? 'P' : ('A' + t.room_id);
//         logger_log(idx, t.is_private ? LOG_PRIVATE_MSG : LOG_MSG_BROADCAST,
//                    room_char, sv,
//                    (int)strlen(t.message),
//                    sent,
//                    t.sender);

//         /* 7. Release admission slot */
//         sem_post(&g_sim.admission_sem);
//         sem_getvalue(&g_sim.admission_sem, &sv);
//         atomic_store(&g_sim.sem_value, sv);
//         atomic_fetch_sub(&g_sim.active_clients, 1);
//         logger_log(idx, LOG_SEM_POST, '-', sv, 0, sent, t.sender);

//         /* 8. Return to IDLE */
//         pthread_mutex_lock(&g_sim.state_mutex);
//         self->state         = THREAD_IDLE;
//         self->room_assigned = -1;
//         pthread_mutex_unlock(&g_sim.state_mutex);

//         sched_yield();
//     }

//     logger_log(idx, LOG_THREAD_EXIT, '-', 0, 0,
//                atomic_load(&self->msgs_sent), "normal exit");

//     return NULL;
// }


/*
 * worker_thread()
 * ─────────────────────────────────────────────────────────────────────────────
 * FIX: Ensure total_dropped is actually incremented and logged correctly
 * FIX: Rate limit check happens BEFORE admission (saves wasted slots)
 * ─────────────────────────────────────────────────────────────────────────────
 */
static void *worker_thread(void *arg)
{
    int idx = *(int *)arg;
    free(arg);

    WorkerThread *self = &g_sim.workers[idx];

    while (atomic_load(&g_sim.running)) {

        /* 1. Block until a task is available */
        sem_wait(&g_sim.queue.tasks_available);

        if (!atomic_load(&g_sim.running)) break;

        /* 2. Pop the task */
        Task t;
        if (!taskqueue_pop(&t)) continue;

        /* 3. CRITICAL FIX: Rate limit check BEFORE admission
         *    This prevents wasting admission slots on messages that will be dropped.
         */
        if (!ratelimit_check(idx)) {
            /* Rate limited - message dropped */
            char room_char = t.is_private ? 'P' : ('A' + t.room_id);
            logger_log(idx, LOG_MSG_DROPPED, room_char, -1, 0,
                       atomic_load(&self->msgs_sent), t.sender);
            atomic_fetch_add(&g_sim.total_dropped, 1);
            
            /* No admission slot was taken, so just yield and continue */
            sched_yield();
            continue;
        }

        /* 4. Admission control - only reach here if rate limit passed */
        int sv;
        sem_getvalue(&g_sim.admission_sem, &sv);
        logger_log(idx, LOG_SEM_BLOCK, '-', sv, 0, 0, "about to wait");

        /* Set WAITING state BEFORE sem_wait */
        pthread_mutex_lock(&g_sim.state_mutex);
        self->state = THREAD_WAITING;
        pthread_mutex_unlock(&g_sim.state_mutex);

        atomic_fetch_add(&g_sim.blocked_clients, 1);
        sem_wait(&g_sim.admission_sem);
        atomic_fetch_sub(&g_sim.blocked_clients, 1);

        sem_getvalue(&g_sim.admission_sem, &sv);
        atomic_store(&g_sim.sem_value, sv);
        atomic_fetch_add(&g_sim.active_clients, 1);

        /* Set ACTIVE state AFTER sem_wait */
        pthread_mutex_lock(&g_sim.state_mutex);
        self->state          = THREAD_ACTIVE;
        self->room_assigned  = t.is_private ? -2 : t.room_id;
        strncpy(self->current_sender, t.sender, sizeof(self->current_sender) - 1);
        self->current_sender[sizeof(self->current_sender) - 1] = '\0';
        self->last_active_us = now_us();
        pthread_mutex_unlock(&g_sim.state_mutex);

        logger_log(idx, LOG_SEM_WAIT, '-', sv, 0, 0, "ADMITTED");

        /* 5. Route: Private Message OR Room Broadcast */
        if (t.is_private) {
            privmsg_send(t.from_thread_id, t.to_thread_id, t.sender, t.message);
        } else {
            room_write(t.room_id, t.sender, t.message, idx);
        }

        /* 6. Increment counters */
        int sent = atomic_fetch_add(&self->msgs_sent, 1) + 1;
        atomic_fetch_add(&g_sim.total_messages, 1);
        self->last_active_us = now_us();

        char room_char = t.is_private ? 'P' : ('A' + t.room_id);
        logger_log(idx, t.is_private ? LOG_PRIVATE_MSG : LOG_MSG_BROADCAST,
                   room_char, sv,
                   (int)strlen(t.message),
                   sent,
                   t.sender);

        /* 7. Release admission slot */
        sem_post(&g_sim.admission_sem);
        sem_getvalue(&g_sim.admission_sem, &sv);
        atomic_store(&g_sim.sem_value, sv);
        atomic_fetch_sub(&g_sim.active_clients, 1);
        logger_log(idx, LOG_SEM_POST, '-', sv, 0, sent, t.sender);

        /* 8. Return to IDLE */
        pthread_mutex_lock(&g_sim.state_mutex);
        self->state         = THREAD_IDLE;
        self->room_assigned = -1;
        pthread_mutex_unlock(&g_sim.state_mutex);

        sched_yield();
    }

    logger_log(idx, LOG_THREAD_EXIT, '-', 0, 0,
               atomic_load(&self->msgs_sent), "normal exit");

    return NULL;
}