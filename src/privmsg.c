#include "chat_sim.h"

void privmsg_init(void){
    for (int i = 0; i < MAX_WORKERS; i++) {
        WorkerThread *w = &g_sim.workers[i];
        w->pm_head  = 0;
        w->pm_tail  = 0;
        w->pm_count = 0;
        w->pm_total = 0;
        memset(w->pm_inbox, 0, sizeof(w->pm_inbox));
        pthread_mutex_init(&w->pm_mutex, NULL);
    }
    printf("[INFO] Private message inboxes initialised (%d threads × %d slots)\n", MAX_WORKERS, PM_INBOX_SLOTS);
}


bool privmsg_send(int from_thread, int to_thread, const char *from_name, const char *text){
    if (to_thread < 0 || to_thread >= g_sim.config.num_threads) return false;
    if (from_thread == to_thread) return false;   /* can't PM yourself */

    WorkerThread *recipient = &g_sim.workers[to_thread];

    /*  Lock only the recipient's inbox mutex (fine-grained)  */
    pthread_mutex_lock(&recipient->pm_mutex);

    /* If inbox is full, advance head to overwrite oldest message */
    if (recipient->pm_count >= PM_INBOX_SLOTS) {
        recipient->pm_head = (recipient->pm_head + 1) % PM_INBOX_SLOTS;
        recipient->pm_count--;
    }

    /* Write the new PM into the tail slot */
    PrivateMessage *slot = &recipient->pm_inbox[recipient->pm_tail];

    slot->from_thread  = from_thread;
    slot->to_thread    = to_thread;
    slot->timestamp_us = now_us() - g_sim.start_time_us;
    strncpy(slot->from_name, from_name ? from_name : "?", sizeof(slot->from_name) - 1);
    strncpy(slot->text, text ? text : "", sizeof(slot->text) - 1);

    recipient->pm_tail  = (recipient->pm_tail  + 1) % PM_INBOX_SLOTS;
    recipient->pm_count++;
    recipient->pm_total++;

    pthread_mutex_unlock(&recipient->pm_mutex);

    /* Update global PM counter (atomic – no lock needed) */
    atomic_fetch_add(&g_sim.total_pm, 1);

    /*  Log the PM event */
    char detail[64];
    snprintf(detail, sizeof(detail), "T%d->T%d %s", from_thread, to_thread, from_name ? from_name : "?");
    logger_log(from_thread, LOG_PRIVATE_MSG, '-', -1, (int)strlen(text), 0, detail);

    /*  Console output (mirrors broadcast format but labelled [PM])  */
    printf("[%08.3f] [PM] T%d (%s) -> T%d: '%s'\n",
           (double)(now_us() - g_sim.start_time_us) / 1e6,
           from_thread, from_name ? from_name : "?", to_thread, text);

    return true;
}

int privmsg_read(int thread_id, PrivateMessage *out, int max_msgs){
    if (thread_id < 0 || thread_id >= MAX_WORKERS || max_msgs <= 0) return 0;

    WorkerThread *w = &g_sim.workers[thread_id];
    pthread_mutex_lock(&w->pm_mutex);
    int n = (w->pm_count < max_msgs) ? w->pm_count : max_msgs;

    /* Walk backwards from (tail-1) to get most-recent first */
    for (int i = 0; i < n; i++) {
        int src = (w->pm_tail - 1 - i + PM_INBOX_SLOTS) % PM_INBOX_SLOTS;
        out[i]  = w->pm_inbox[src];
    }

    pthread_mutex_unlock(&w->pm_mutex);
    return n;
}

int privmsg_unread(int thread_id) {
    if (thread_id < 0 || thread_id >= MAX_WORKERS) return 0;
    WorkerThread *w = &g_sim.workers[thread_id];

    pthread_mutex_lock(&w->pm_mutex);
    int c = w->pm_count;
    pthread_mutex_unlock(&w->pm_mutex);

    return c;
}