/*
 * logger.c
 * ─────────────────────────────────────────────────────────────────────────────
 * Project  : OS-Level Chat Server Using Thread Pools
 * Course   : CS2006 Operating Systems – Spring 2026
 *
 * Primary Author : Sana Munir Alam [24K-0573]  ← Logging Lead
 *
 * Responsibilities covered in this file:
 *   • In-memory ring-buffer log (MAX_LOG_ENTRIES = 4096 entries)
 *   • logger_log()   – called by every thread to record OS events
 *   • logger_export() – flush ring buffer to chat_sim.log.txt on disk
 *   • now_us()        – microsecond timestamp helper used everywhere
 *
 * Log format (matches proposal §4.5 exactly):
 *   [THREAD_ID] [TIMESTAMP_US] ACQUIRED LOCK room=A
 *   [THREAD_ID] [TIMESTAMP_US] RELEASED LOCK room=A
 *   [THREAD_ID] [TIMESTAMP_US] SEM_WAIT value=49
 *   [THREAD_ID] [TIMESTAMP_US] MSG_BROADCAST room=A len=28
 *   [THREAD_ID] [TIMESTAMP_US] THREAD_EXIT msgs=47
 *
 * Correctness check (from proposal §10.2):
 *   After export, scan for consecutive ACQUIRED LOCK entries for the
 *   same room without an intervening RELEASED LOCK → if found, mutex
 *   is broken.  A clean run shows strictly alternating pairs.
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include "chat_sim.h"
#include <sys/time.h>

/*
 * now_us()
 * ─────────────────────────────────────────────────────────────────────────────
 * Owner   : Sana Munir Alam [24K-0573]
 * Purpose : Return current wall-clock time in microseconds since the Unix
 *           epoch.  Used for all timestamps in log entries and ChatMessage
 *           structs.  gettimeofday() is portable on all POSIX systems.
 * ─────────────────────────────────────────────────────────────────────────────
 */
long long now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000LL + tv.tv_usec;
}

/*
 * logger_init()
 * ─────────────────────────────────────────────────────────────────────────────
 * Owner   : Sana Munir Alam [24K-0573]
 * Purpose : Initialise the in-memory log ring buffer and its mutex.
 *           Called once from main() before any thread is created.
 * ─────────────────────────────────────────────────────────────────────────────
 */
void logger_init(void)
{
    g_sim.log_head  = 0;
    g_sim.log_count = 0;
    memset(g_sim.log_ring, 0, sizeof(g_sim.log_ring));
    pthread_mutex_init(&g_sim.log_mutex, NULL);
}

/*
 * logger_log()
 * ─────────────────────────────────────────────────────────────────────────────
 * Owner   : Sana Munir Alam [24K-0573]
 * Purpose : Thread-safe structured logging.  Stores one LogEntry in the
 *           ring buffer.  When the ring wraps, oldest entries are silently
 *           overwritten (ring buffer semantics – most-recent always visible).
 *
 * Parameters:
 *   thread_id  – worker index (0-based)
 *   event      – LogEventType enum value
 *   room       – 'A', 'B', 'C', or '-' for non-room events
 *   sem_val    – current semaphore count (-1 if not applicable)
 *   msg_len    – length of message if LOG_MSG_BROADCAST, else 0
 *   msgs_sent  – per-thread message count if LOG_THREAD_EXIT, else 0
 *   detail     – free-form string (client name, status string, etc.)
 *
 * Thread-safety: uses log_mutex so concurrent callers don't corrupt
 *                the ring head/count integers.
 * ─────────────────────────────────────────────────────────────────────────────
 */
void logger_log(int thread_id, LogEventType event, char room,
                int sem_val, int msg_len, int msgs_sent,
                const char *detail)
{
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

/*
 * logger_export()
 * ─────────────────────────────────────────────────────────────────────────────
 * Owner   : Sana Munir Alam [24K-0573]
 * Purpose : Write all buffered log entries to "chat_sim.log.txt" in the
 *           format specified in proposal §4.5.
 *
 *           Called by:
 *             • The "Export Log" GUI button (Adeena's gui.c)
 *             • Automatically at simulation end (main.c)
 *
 * The file includes a header line and a separator, then one record per
 * entry in chronological order.  Format:
 *
 *   EVENT_LOG | OS-Level Chat Server | <datetime>
 *   ----------------------------------------------------------
 *   LINE  TIMESTAMP_MS  EVENT_TYPE  tid=N  room=X  detail...
 *
 * ─────────────────────────────────────────────────────────────────────────────
 */
void logger_export(void)
{
    pthread_mutex_lock(&g_sim.log_mutex);

    FILE *f = fopen(LOG_FILENAME, "w");
    if (!f) {
        pthread_mutex_unlock(&g_sim.log_mutex);
        fprintf(stderr, "[WARN] Cannot open %s for writing\n", LOG_FILENAME);
        return;
    }

    /* Header */
    time_t now_sec = time(NULL);
    char   timebuf[64];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", localtime(&now_sec));
    fprintf(f, "EVENT_LOG | OS-Level Chat Server | %s\n", timebuf);
    fprintf(f, "----------------------------------------------------------------\n");
    fprintf(f, "%-5s %-14s %-18s %-6s %-6s %-6s %-6s %s\n",
            "LINE", "TIMESTAMP_MS", "EVENT", "TID", "ROOM", "SEM", "MSGS", "DETAIL");
    fprintf(f, "----------------------------------------------------------------\n");

    /* Entries – walk from oldest to newest */
    int count  = g_sim.log_count;
    int oldest = (g_sim.log_head - count + MAX_LOG_ENTRIES) % MAX_LOG_ENTRIES;

    for (int i = 0; i < count; i++) {
        int idx    = (oldest + i) % MAX_LOG_ENTRIES;
        LogEntry *e = &g_sim.log_ring[idx];

        double ts_ms = (double)e->timestamp_us / 1000.0;

        fprintf(f, "%-5d %13.3f  %-18s tid=%-3d room=%c  sem=%-4d msgs=%-4d %s\n",
                i + 1,
                ts_ms,
                log_event_name(e->event_type),
                e->thread_id,
                e->room,
                e->sem_value,
                e->msgs_sent,
                e->detail);
    }

    fclose(f);
    pthread_mutex_unlock(&g_sim.log_mutex);

    printf("[INFO] Log exported → %s  (%d entries)\n", LOG_FILENAME, count);
}
