#include "chat_sim.h"
#include <sys/time.h>

long long now_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000LL + tv.tv_usec;
}

void logger_init(void) {
    g_sim.log_head  = 0;
    g_sim.log_count = 0;
    memset(g_sim.log_ring, 0, sizeof(g_sim.log_ring));
    pthread_mutex_init(&g_sim.log_mutex, NULL);
}

void logger_log(int thread_id, LogEventType event, char room, int sem_val, int msg_len, int msgs_sent, const char *detail) {
    pthread_mutex_lock(&g_sim.log_mutex);

    LogEntry *e = &g_sim.log_ring[g_sim.log_head];

    e->thread_id    = thread_id;
    e->timestamp_us = now_us() - g_sim.start_time_us;
    e->event_type   = event;
    e->room         = room;
    e->sem_value    = sem_val;
    e->msg_len      = msg_len;
    e->msgs_sent    = msgs_sent;

    if (detail) {
        strncpy(e->detail, detail, sizeof(e->detail) - 1);
        e->detail[sizeof(e->detail) - 1] = '\0';
    } else {
        e->detail[0] = '\0';
    }

    /* Advance ring head (overwrites oldest when full) */
    g_sim.log_head = (g_sim.log_head + 1) % MAX_LOG_ENTRIES;
    if (g_sim.log_count < MAX_LOG_ENTRIES)
        g_sim.log_count++;

    pthread_mutex_unlock(&g_sim.log_mutex);
}

void logger_export(void) {
    pthread_mutex_lock(&g_sim.log_mutex);

    FILE *f = fopen(LOG_FILENAME, "w");
    if (!f) {
        pthread_mutex_unlock(&g_sim.log_mutex);
        fprintf(stderr, "[WARN] Cannot open %s for writing\n", LOG_FILENAME);
        return;
    }

    /* Header */
    time_t    now_sec = time(NULL);
    struct tm tm_buf;
    char      timebuf[64];
    localtime_r(&now_sec, &tm_buf);
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    fprintf(f, "EVENT_LOG | OS-Level Chat Server | %s\n", timebuf);
    fprintf(f, "----------------------------------------------------------------\n");
    fprintf(f, "%-5s %-14s %-18s %-6s %-6s %-6s %-6s %s\n", "LINE", "TIMESTAMP_MS", "EVENT", "TID", "ROOM", "SEM", "MSGS", "DETAIL");
    fprintf(f, "----------------------------------------------------------------\n");

    /* Entries – walk from oldest to newest */
    int count  = g_sim.log_count;
    int oldest = (g_sim.log_head - count + MAX_LOG_ENTRIES) % MAX_LOG_ENTRIES;

    for (int i = 0; i < count; i++) {
        int idx    = (oldest + i) % MAX_LOG_ENTRIES;
        LogEntry *e = &g_sim.log_ring[idx];

        double ts_ms = (double)e->timestamp_us / 1000.0;

        fprintf(f, "%-5d %13.3f  %-18s tid=%-3d room=%c  sem=%-4d msgs=%-4d %s\n",
                i + 1, ts_ms, log_event_name(e->event_type), e->thread_id, e->room, e->sem_value,  e->msgs_sent, e->detail);
    }

    fclose(f);
    pthread_mutex_unlock(&g_sim.log_mutex);

    printf("[INFO] Log exported → %s  (%d entries)\n", LOG_FILENAME, count);

    /* ── Throughput CSV ─────────────────────────────────────────────────────
     * Format: clients, threads, second, msgs_per_sec
     * Appends to throughput.csv so multiple runs (10/50/100 clients) stack
     * into one comparative file without overwriting previous results.
     * ──────────────────────────────────────────────────────────────────── */
    int   n_clients = g_sim.config.num_clients;
    int   n_threads = g_sim.config.num_threads;
    int   n         = g_sim.throughput_sample_count;

    /* Compute peak and average over the samples */
    int   peak = 0;
    long  sum  = 0;
    for (int i = 0; i < n; i++) {
        if (g_sim.throughput_samples[i] > peak)
            peak = g_sim.throughput_samples[i];
        sum += g_sim.throughput_samples[i];
    }
    double avg = (n > 0) ? (double)sum / n : 0.0;

    /* Open in append mode so benchmark runs accumulate */
    FILE *csv = fopen("throughput.csv", "a");
    if (!csv) {
        fprintf(stderr, "[WARN] Cannot open throughput.csv for writing\n");
        return;
    }

    /* Write header only if file was empty (i.e. position is 0) */
    fseek(csv, 0, SEEK_END);
    long fsize = ftell(csv);
    if (fsize == 0)
        fprintf(csv, "clients,threads,second,msgs_per_sec,peak_msgs_per_sec,avg_msgs_per_sec\n");

    for (int i = 0; i < n; i++)
        fprintf(csv, "%d,%d,%d,%d,%d,%.2f\n", n_clients, n_threads, i, g_sim.throughput_samples[i], peak, avg);

    fclose(csv);

    printf("[INFO] Throughput CSV → throughput.csv (clients=%d  threads=%d  peak=%d msg/s  avg=%.1f msg/s)\n", n_clients, n_threads, peak, avg);
}