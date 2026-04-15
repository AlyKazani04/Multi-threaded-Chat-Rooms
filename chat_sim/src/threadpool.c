/*
 * threadpool.c
 * ─────────────────────────────────────────────────────────────────────────────
 * Project  : OS-Level Chat Server Using Thread Pools
 * Course   : CS2006 Operating Systems – Spring 2026
 *
 * Primary Author : Aly Muhammad Kazani [24K-0512]  ← Thread Pool Lead
 *
 * Responsibilities covered in this file:
 *   • Thread pool initialisation (pthread_create for M worker threads)
 *   • Task queue – thread-safe FIFO (mutex + semaphore)
 *   • Worker thread main loop (sem_wait / mutex / cond_wait lifecycle)
 *   • Semaphore-based admission control (MAX_CLIENTS = 50)
 *   • Graceful shutdown (sets running=false, posts sentinel wakeups)
 *
 * OS concepts demonstrated:
 *   • Producer-Consumer bounded buffer (task queue)
 *   • POSIX threads (pthread_create / pthread_join)
 *   • Semaphores: tasks_available (binary signalling) + admission_sem (counting)
 *   • Mutex + condition variable for room buffer (see rooms.c for the other half)
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include "chat_sim.h"

/* ── forward declaration of the worker body ─────────────────────────────── */
static void *worker_thread(void *arg);

/* ── name pool used by the client generator (also used here for logging) ── */
static const char *CLIENT_NAMES[20] = {
    "Alice","Bob","Charlie","Diana","Eve",
    "Frank","Grace","Hank","Iris","Jack",
    "Karen","Leo","Mia","Nick","Olivia",
    "Paul","Quinn","Rose","Sam","Tina"
};

/*
 * threadpool_init()
 * ─────────────────────────────────────────────────────────────────────────────
 * Owner   : Aly Muhammad Kazani [24K-0512]
 * Purpose : Bring the thread pool, task queue, and admission semaphore into a
 *           known-good state, then spawn M worker threads.
 *
 * Steps (mirrors proposal pseudocode §7.2):
 *   1. sem_init(&admission_semaphore, 0, MAX_CLIENTS)  → 50 slots
 *   2. sem_init(&tasks_available, 0, 0)                → no work yet
 *   3. pthread_mutex_init(&queue_mutex)
 *   4. pthread_create × num_workers                    → all begin IDLE
 * ─────────────────────────────────────────────────────────────────────────────
 */
void threadpool_init(int num_workers)
{
    /* Clamp to [MIN_WORKERS, MAX_WORKERS] */
    if (num_workers < MIN_WORKERS) num_workers = MIN_WORKERS;
    if (num_workers > MAX_WORKERS) num_workers = MAX_WORKERS;

    g_sim.config.num_threads = num_workers;

    /* --- Init task queue ------------------------------------------------ */
    TaskQueue *q = &g_sim.queue;
    q->head  = 0;
    q->tail  = 0;
    q->count = 0;
    pthread_mutex_init(&q->mutex, NULL);
    sem_init(&q->tasks_available, 0, 0);   /* starts at 0 – no tasks yet */

    /* --- Admission semaphore – caps concurrent active clients at 50 ----- */
    sem_init(&g_sim.admission_sem, 0, MAX_CLIENTS);
    atomic_store(&g_sim.sem_value, MAX_CLIENTS);

    /* --- Mark simulation as running ------------------------------------- */
    atomic_store(&g_sim.running, true);
    atomic_store(&g_sim.paused,  false);

    /* --- Initialise per-thread PM inboxes (§9.2 private messaging) ------ */
    privmsg_init();

    /* --- Spawn worker threads ------------------------------------------- */
    for (int i = 0; i < num_workers; i++) {
        WorkerThread *w = &g_sim.workers[i];
        w->index          = i;
        w->state          = THREAD_IDLE;
        w->room_assigned  = -1;
        atomic_store(&w->msgs_sent, 0);
        w->current_sender[0] = '\0';
        w->last_active_us    = 0;

        int *idx = malloc(sizeof(int));
        *idx = i;

        int rc = pthread_create(&w->tid, NULL, worker_thread, idx);
        if (rc != 0) {
            fprintf(stderr, "[FATAL] pthread_create failed for worker %d (rc=%d)\n", i, rc);
            exit(EXIT_FAILURE);
        }

        /* Log creation event (tid index used as pseudo-id) */
        logger_log(i, LOG_THREAD_CREATE, '-', MAX_CLIENTS, 0, 0,
                   CLIENT_NAMES[i % CLIENT_NAMES_COUNT]);
    }

    printf("[%08.3f] Thread pool initialized: %d workers, semaphore=%d\n",
           (double)(now_us() - g_sim.start_time_us) / 1e6,
           num_workers, MAX_CLIENTS);
}

/*
 * threadpool_shutdown()
 * ─────────────────────────────────────────────────────────────────────────────
 * Owner   : Aly Muhammad Kazani [24K-0512]
 * Purpose : Gracefully stop all worker threads and free synchronisation
 *           primitives.
 *
 *   1. Set running = false so workers exit their while loop.
 *   2. Post num_workers sentinel wakeups to tasks_available so every
 *      blocked worker unblocks at least once and checks running.
 *   3. pthread_join each worker.
 *   4. Destroy mutexes and semaphores.
 * ─────────────────────────────────────────────────────────────────────────────
 */
void threadpool_shutdown(void)
{
    atomic_store(&g_sim.running, false);
    atomic_store(&g_sim.paused,  false);

    /* Wake every potentially-blocked worker */
    for (int i = 0; i < g_sim.config.num_threads; i++)
        sem_post(&g_sim.queue.tasks_available);

    /* Also unblock any room condition-variable waits */
    for (int r = 0; r < NUM_ROOMS; r++) {
        pthread_mutex_lock(&g_sim.rooms[r].mutex);
        pthread_cond_broadcast(&g_sim.rooms[r].cond_not_full);
        pthread_cond_broadcast(&g_sim.rooms[r].cond_not_empty);
        pthread_mutex_unlock(&g_sim.rooms[r].mutex);
    }

    /* Join all workers */
    for (int i = 0; i < g_sim.config.num_threads; i++) {
        pthread_join(g_sim.workers[i].tid, NULL);
        logger_log(i, LOG_THREAD_EXIT, '-', 0, 0,
                   atomic_load(&g_sim.workers[i].msgs_sent), "shutdown");
    }

    /* Destroy primitives */
    pthread_mutex_destroy(&g_sim.queue.mutex);
    sem_destroy(&g_sim.queue.tasks_available);
    sem_destroy(&g_sim.admission_sem);

    /* Destroy per-thread PM inbox mutexes (§9.2) */
    for (int i = 0; i < g_sim.config.num_threads; i++)
        pthread_mutex_destroy(&g_sim.workers[i].pm_mutex);

    printf("[INFO] Thread pool shut down cleanly.\n");
}

/*
 * taskqueue_push()
 * ─────────────────────────────────────────────────────────────────────────────
 * Owner   : Aly Muhammad Kazani [24K-0512]
 * Purpose : Enqueue a Task from the client generator (called from the
 *           client_generator_thread, possibly also from test code).
 *
 * Thread-safety: Locks queue_mutex, appends, unlocks, then posts
 *                tasks_available so a waiting worker wakes up.
 * ─────────────────────────────────────────────────────────────────────────────
 */
void taskqueue_push(Task *t)
{
    TaskQueue *q = &g_sim.queue;

    pthread_mutex_lock(&q->mutex);

    if (q->count >= TASK_QUEUE_CAP) {
        /* Queue full – drop oldest task (circular overwrite) */
        q->head = (q->head + 1) % TASK_QUEUE_CAP;
        q->count--;
    }

    q->tasks[q->tail] = *t;
    q->tail           = (q->tail + 1) % TASK_QUEUE_CAP;
    q->count++;

    pthread_mutex_unlock(&q->mutex);

    /* Signal one waiting worker that a task is available */
    sem_post(&q->tasks_available);
}

/*
 * taskqueue_pop()
 * ─────────────────────────────────────────────────────────────────────────────
 * Owner   : Aly Muhammad Kazani [24K-0512]
 * Purpose : Dequeue the front Task. Called only from worker_thread after
 *           sem_wait(tasks_available) has confirmed there is at least one item.
 *
 * Returns : true  → task copied to *out
 *           false → queue was empty (should not happen under normal flow)
 * ─────────────────────────────────────────────────────────────────────────────
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
 * Owner   : Aly Muhammad Kazani [24K-0512]
 * Purpose : Body of each worker thread – the core OS concurrency loop.
 *
 * Algorithm (directly implements proposal pseudocode §7.1):
 *
 *   while (running) {
 *       sem_wait(&tasks_available);          // block until work exists
 *       pop task from queue (mutex-protected)
 *
 *       sem_wait(&admission_sem);            // block if 50 clients active
 *       mark ACTIVE; update GUI state
 *
 *       pthread_mutex_lock(&room_mutex[t.room_id]);
 *       while (buffer_full(room))            // Producer-Consumer wait
 *           pthread_cond_wait(&cond_not_full, &room_mutex);
 *       write_to_buffer(message);            // critical section
 *       pthread_cond_signal(&cond_not_empty);
 *       pthread_mutex_unlock(&room_mutex);
 *
 *       log_event();
 *       sem_post(&admission_sem);            // release slot
 *       mark IDLE
 *   }
 * ─────────────────────────────────────────────────────────────────────────────
 */
static void *worker_thread(void *arg)
{
    int idx = *(int *)arg;
    free(arg);

    WorkerThread *self = &g_sim.workers[idx];

    while (atomic_load(&g_sim.running)) {

        /* ── 1. Block until a task is available (or shutdown posted) ─── */
        sem_wait(&g_sim.queue.tasks_available);

        if (!atomic_load(&g_sim.running)) break;  /* shutdown sentinel */

        /* ── 2. Pop the task from the queue ───────────────────────────── */
        Task t;
        if (!taskqueue_pop(&t)) continue;   /* spurious wake – skip */

        /* ── 3. Admission control – block if 50 clients already active ── */
        /*        This is the semaphore boundary the proposal stress-tests  */
        int sv;
        sem_getvalue(&g_sim.admission_sem, &sv);
        if (sv == 0) {
            /* About to block – log it */
            atomic_fetch_add(&g_sim.blocked_clients, 1);
            logger_log(idx, LOG_SEM_WAIT, '-', sv, 0, 0, "BLOCKING");

            pthread_mutex_lock(&g_sim.state_mutex);
            self->state = THREAD_WAITING;
            pthread_mutex_unlock(&g_sim.state_mutex);
        }

        sem_wait(&g_sim.admission_sem);     /* ← blocks here if sem == 0 */
        atomic_fetch_sub(&g_sim.blocked_clients, 1);

        sem_getvalue(&g_sim.admission_sem, &sv);
        atomic_store(&g_sim.sem_value, sv);
        atomic_fetch_add(&g_sim.active_clients, 1);
        logger_log(idx, LOG_SEM_WAIT, '-', sv, 0, 0, "ADMITTED");

        /* ── 4. Update worker state for GUI ───────────────────────────── */
        pthread_mutex_lock(&g_sim.state_mutex);
        self->state          = THREAD_ACTIVE;
        self->room_assigned  = t.is_private ? -2 : t.room_id;
        strncpy(self->current_sender, t.sender, sizeof(self->current_sender) - 1);
        self->current_sender[sizeof(self->current_sender) - 1] = '\0';
        self->last_active_us = now_us();
        pthread_mutex_unlock(&g_sim.state_mutex);

        /* ── 5a. §9.2 Rate-limit check – drop if this client is flooding ─ */
        if (!ratelimit_check(idx)) {
            /* Message dropped – release admission slot and go IDLE */
            sem_post(&g_sim.admission_sem);
            sem_getvalue(&g_sim.admission_sem, &sv);
            atomic_store(&g_sim.sem_value, sv);
            atomic_fetch_sub(&g_sim.active_clients, 1);

            pthread_mutex_lock(&g_sim.state_mutex);
            self->state         = THREAD_IDLE;
            self->room_assigned = -1;
            pthread_mutex_unlock(&g_sim.state_mutex);
            sched_yield();
            continue;
        }

        /* ── 5b. Route: Private Message OR Room Broadcast ─────────────── */
        if (t.is_private) {
            /* §9.2 – Private message bypasses room buffers entirely */
            privmsg_send(idx, t.to_thread_id, t.sender, t.message);
        } else {
            /* Standard room broadcast (Sana's rooms.c handles mutex+cond) */
            room_write(t.room_id, t.sender, t.message, idx);
        }

        /* ── 6. Increment per-thread and global counters ──────────────── */
        int sent = atomic_fetch_add(&self->msgs_sent, 1) + 1;
        atomic_fetch_add(&g_sim.total_messages, 1);
        self->last_active_us = now_us();

        /* Log the broadcast/PM event */
        char room_char = t.is_private ? 'P' : ('A' + t.room_id);
        logger_log(idx, t.is_private ? LOG_PRIVATE_MSG : LOG_MSG_BROADCAST,
                   room_char, sv,
                   (int)strlen(t.message),
                   sent,
                   t.sender);

        /* ── 7. Release admission slot ────────────────────────────────── */
        sem_post(&g_sim.admission_sem);
        sem_getvalue(&g_sim.admission_sem, &sv);
        atomic_store(&g_sim.sem_value, sv);
        atomic_fetch_sub(&g_sim.active_clients, 1);
        logger_log(idx, LOG_SEM_POST, '-', sv, 0, sent, t.sender);

        /* ── 8. Return to IDLE ─────────────────────────────────────────── */
        pthread_mutex_lock(&g_sim.state_mutex);
        self->state         = THREAD_IDLE;
        self->room_assigned = -1;
        pthread_mutex_unlock(&g_sim.state_mutex);

        /* Small yield so other threads get scheduled fairly */
        sched_yield();
    }

    /* Log exit */
    logger_log(idx, LOG_THREAD_EXIT, '-', 0, 0,
               atomic_load(&self->msgs_sent), "normal exit");

    return NULL;
}