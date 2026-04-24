#include "chat_sim.h"

static void *worker_thread(void *arg);

static const char *CLIENT_NAMES[20] = {
    "Alice","Bob","Charlie","Diana","Eve",
    "Frank","Grace","Hank","Iris","Jack",
    "Karen","Leo","Mia","Nick","Olivia",
    "Paul","Quinn","Rose","Sam","Tina"
};

void threadpool_init(int num_workers) {
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
        w->last_room_used = -1;
        atomic_store(&w->msgs_sent, 0);
        w->current_sender[0] = '\0';
        w->last_active_us    = 0;

        w->rate_tokens  = g_sim.config.rate_limit_per_sec;
        w->rate_last_us = now_us();

        int *idx = malloc(sizeof(int));
        if (idx == NULL) {
            fprintf(stderr, "[FATAL] malloc failed for worker index %d\n", i);
            exit(EXIT_FAILURE);
        }
        *idx = i;

        int rc = pthread_create(&w->tid, NULL, worker_thread, idx);
        if (rc != 0) {
            free(idx);
            fprintf(stderr, "[FATAL] pthread_create failed for worker %d (rc=%d)\n", i, rc);
            exit(EXIT_FAILURE);
        }

        logger_log(i, LOG_THREAD_CREATE, '-', MAX_CLIENTS, 0, 0,  CLIENT_NAMES[i % CLIENT_NAMES_COUNT]);
    }

    printf("[%08.3f] Thread pool initialized: %d workers, semaphore=%d\n", (double)(now_us() - g_sim.start_time_us) / 1e6, num_workers, MAX_CLIENTS);
}

void threadpool_shutdown(void){
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
        logger_log(i, LOG_THREAD_EXIT, '-', 0, 0, atomic_load(&g_sim.workers[i].msgs_sent), "shutdown");
    }

    pthread_mutex_destroy(&g_sim.queue.mutex);
    sem_destroy(&g_sim.queue.tasks_available);
    sem_destroy(&g_sim.admission_sem);

    for (int i = 0; i < g_sim.config.num_threads; i++)
        pthread_mutex_destroy(&g_sim.workers[i].pm_mutex);

    printf("[INFO] Thread pool shut down cleanly.\n");
}

void taskqueue_push(Task *t){
    TaskQueue *q = &g_sim.queue;

    pthread_mutex_lock(&q->mutex);

    if (q->count >= TASK_QUEUE_CAP) {
        q->head = (q->head + 1) % TASK_QUEUE_CAP;
        q->count--;
    }

    q->tasks[q->tail] = *t;
    q->tail           = (q->tail + 1) % TASK_QUEUE_CAP;
    q->count++;
    atomic_store(&g_sim.blocked_clients, q->count);

    pthread_mutex_unlock(&q->mutex);

    sem_post(&q->tasks_available);
}

bool taskqueue_pop(Task *out){
    TaskQueue *q = &g_sim.queue;

    pthread_mutex_lock(&q->mutex);
    if (q->count == 0) {
        pthread_mutex_unlock(&q->mutex);
        return false;
    }

    *out    = q->tasks[q->head];
    q->head = (q->head + 1) % TASK_QUEUE_CAP;
    q->count--;
    atomic_store(&g_sim.blocked_clients, q->count);

    pthread_mutex_unlock(&q->mutex);
    return true;
}


static void *worker_thread(void *arg){
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

        /* 3. Rate limit check BEFORE admission */
        if (!ratelimit_check(idx)) {
            char room_char = t.is_private ? 'P' : ('A' + t.room_id);
            logger_log(idx, LOG_MSG_DROPPED, room_char, -1, 0,  atomic_load(&self->msgs_sent), t.sender);
            atomic_fetch_add(&g_sim.total_dropped, 1);
            sched_yield();
            continue;
        }

        /* 4. Admission control */
        int sv;
        sem_getvalue(&g_sim.admission_sem, &sv);
        logger_log(idx, LOG_SEM_BLOCK, '-', sv, 0, 0, "about to wait");

        pthread_mutex_lock(&g_sim.state_mutex);
        self->state = THREAD_WAITING;
        pthread_mutex_unlock(&g_sim.state_mutex);

        sem_wait(&g_sim.admission_sem);

        /* Only decrement if we're still running (not woken for shutdown) */
        if (!atomic_load(&g_sim.running)) {
            /* We were woken to exit — restore the semaphore slot */
            sem_post(&g_sim.admission_sem);
            break;
        }

        sem_getvalue(&g_sim.admission_sem, &sv);
        atomic_store(&g_sim.sem_value, sv);
        atomic_fetch_add(&g_sim.active_clients, 1);

        pthread_mutex_lock(&g_sim.state_mutex);
        self->state = THREAD_ACTIVE;
        self->room_assigned = t.is_private ? -2 : t.room_id;
        if (!t.is_private) {
            self->last_room_used = t.room_id;
        }
        snprintf(self->current_sender, sizeof(self->current_sender), "%s", t.sender);
        self->last_active_us = now_us();
        pthread_mutex_unlock(&g_sim.state_mutex);

        logger_log(idx, LOG_SEM_WAIT, '-', sv, 0, 0, "ADMITTED");

        /* 5. Route: Private Message OR Room Broadcast */
        bool success = false;
        if (t.is_private) {
            success = privmsg_send(t.from_thread_id, t.to_thread_id, t.sender, t.message);
        } else {
            success = room_write(t.room_id, t.sender, t.message, idx);
        }

        /* 6. Increment counters ONLY if successful */
        if (success) {
            int sent = atomic_fetch_add(&self->msgs_sent, 1) + 1;
            atomic_fetch_add(&g_sim.total_messages, 1);
            self->last_active_us = now_us();
            char room_char = t.is_private ? 'P' : ('A' + t.room_id);
            logger_log(idx, t.is_private ? LOG_PRIVATE_MSG : LOG_MSG_BROADCAST, room_char, sv, (int)strlen(t.message), sent, t.sender);
        } else {
            /* Failed to send - count it as a dropped message */
            char room_char = t.is_private ? 'P' : ('A' + t.room_id);
            atomic_fetch_add(&g_sim.total_dropped, 1);
            logger_log(idx, LOG_MSG_DROPPED, room_char, -1, 0, atomic_load(&self->msgs_sent), "send failed");
        }

        /* 7. Release admission slot */
        sem_post(&g_sim.admission_sem);
        sem_getvalue(&g_sim.admission_sem, &sv);
        atomic_store(&g_sim.sem_value, sv);
        atomic_fetch_sub(&g_sim.active_clients, 1);
        
        int final_count = atomic_load(&self->msgs_sent);
        logger_log(idx, LOG_SEM_POST, '-', sv, 0, final_count, t.sender);

        /* 8. Return to IDLE */
        pthread_mutex_lock(&g_sim.state_mutex);
        self->state = THREAD_IDLE;
        self->room_assigned = -1;
        pthread_mutex_unlock(&g_sim.state_mutex);

        sched_yield();
    }

    logger_log(idx, LOG_THREAD_EXIT, '-', 0, 0, atomic_load(&self->msgs_sent), "normal exit");

    return NULL;
}