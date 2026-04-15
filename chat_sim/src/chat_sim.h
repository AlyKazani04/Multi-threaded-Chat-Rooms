/*
 * chat_sim.h
 * ─────────────────────────────────────────────────────────────────────────────
 * Project  : OS-Level Chat Server Using Thread Pools
 * Course   : CS2006 Operating Systems – Spring 2026
 * Group    : Aly Muhammad Kazani [24K-0512]
 *            Sana Munir Alam    [24K-0573]
 *            Adeena Asif        [24K-0628]
 *
 * File Responsibility:
 *   Central header shared by ALL source files.  Defines every constant,
 *   struct, global variable declaration, and function prototype used
 *   across the project.  Include this file ONCE per translation unit.
 *
 * Member Ownership:
 *   Constants, ChatRoom, ThreadWorker, TaskQueue  →  Aly Muhammad Kazani
 *   LogEntry, log helpers                         →  Sana Munir Alam
 *   SimState (GUI view model)                     →  Adeena Asif
 * ─────────────────────────────────────────────────────────────────────────────
 */

#ifndef CHAT_SIM_H
#define CHAT_SIM_H

#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ─────────────────────────────────────────────────────────────────────────────
 * COMPILE-TIME CONSTANTS
 * ───────────────────────────────────────────────────────────────────────────*/

#define MAX_WORKERS        10         /* Maximum worker threads in the pool    */
#define MIN_WORKERS        5          /* Minimum worker threads allowed        */
#define MAX_CLIENTS        50         /* Semaphore-enforced admission ceiling  */
#define NUM_ROOMS          3          /* Fixed: Room A (General), B (Priority), C (Private) */
#define BUFFER_SLOTS       50         /* Circular buffer depth per chat room   */
#define MAX_MSG_LEN        256        /* Maximum characters per message        */
#define MAX_LOG_ENTRIES    4096       /* In-memory log ring before flush       */
#define CLIENT_NAMES_COUNT 20         /* Pool of simulated client names        */
#define LOG_FILENAME       "chat_sim.log.txt"

/* ── Additional Feature constants (§9.2) ───────────────────────────────── */
#define PM_INBOX_SLOTS     32         /* Max unread private messages per thread */
#define DEFAULT_RATE_LIMIT 20         /* Default max messages per second per client */

/* ─────────────────────────────────────────────────────────────────────────────
 * ENUMERATIONS
 * ───────────────────────────────────────────────────────────────────────────*/

/* Worker thread lifecycle state – drives GUI colour coding */
typedef enum {
    THREAD_IDLE    = 0,   /* waiting for a task in the task queue  */
    THREAD_ACTIVE  = 1,   /* currently processing a client message */
    THREAD_WAITING = 2    /* blocked on cond_wait (buffer full)    */
} ThreadState;

/* Log event types – every OS-level event gets one of these tags */
typedef enum {
    LOG_THREAD_CREATE,
    LOG_ACQUIRED_LOCK,
    LOG_RELEASED_LOCK,
    LOG_SEM_WAIT,
    LOG_SEM_POST,
    LOG_MSG_BROADCAST,
    LOG_THREAD_EXIT,
    LOG_BUFFER_FULL,
    LOG_BUFFER_WRAP,
    LOG_PRIVATE_MSG,      /* §9.2 – direct thread-to-thread private message */
    LOG_RATE_LIMITED      /* §9.2 – client throttled by per-client rate limit */
} LogEventType;

/* ─────────────────────────────────────────────────────────────────────────────
 * STRUCTS
 * ───────────────────────────────────────────────────────────────────────────*/

/*
 * ChatMessage
 * One slot in a room's circular buffer.
 * Owner: Sana Munir Alam [24K-0573]
 */
typedef struct {
    char      sender[32];          /* Simulated client name           */
    char      text[MAX_MSG_LEN];   /* Message body                    */
    long long timestamp_us;        /* Microseconds since program start*/
    int       thread_id;           /* Worker thread index (0-based)   */
} ChatMessage;

/*
 * PrivateMessage
 * §9.2 Additional Feature – direct message between two specific thread IDs.
 * Stored in the recipient's PM inbox (per-WorkerThread ring buffer).
 * Bypasses all room buffers and room mutexes entirely.
 * Owner: Aly Muhammad Kazani [24K-0512]
 */
typedef struct {
    int       from_thread;         /* Sender worker index             */
    int       to_thread;           /* Recipient worker index          */
    char      from_name[32];       /* Sender client name              */
    char      text[MAX_MSG_LEN];   /* Message body                    */
    long long timestamp_us;        /* Microseconds since program start*/
} PrivateMessage;

/*
 * ChatRoom
 * Models one bounded-buffer chat room (Producer-Consumer pattern).
 * Mutex + two condition variables guard every access.
 * Owner: Sana Munir Alam [24K-0573]
 */
typedef struct {
    char          name[16];                       /* "Room A", "Room B", "Room C" */
    char          label[32];                      /* "General", "Priority", "Private" */
    ChatMessage   buffer[BUFFER_SLOTS];           /* Circular message buffer       */
    int           head;                           /* Next read position            */
    int           tail;                           /* Next write position           */
    int           count;                          /* Current slot occupancy        */
    int           total_written;                  /* All-time write counter        */
    pthread_mutex_t mutex;                        /* Mutual exclusion on buffer    */
    pthread_cond_t  cond_not_full;                /* Producer waits here           */
    pthread_cond_t  cond_not_empty;               /* Consumer waits here           */
} ChatRoom;

/*
 * Task
 * A unit of work placed in the task queue by the client generator.
 * Owner: Aly Muhammad Kazani [24K-0512]
 */
typedef struct {
    int  room_id;                    /* 0 = A, 1 = B, 2 = C; -1 = PM  */
    char sender[32];                 /* Simulated client name           */
    char message[MAX_MSG_LEN];       /* Message body to post            */
    int  client_id;                  /* Global client sequence number   */

    /* §9.2 – Private messaging fields */
    bool is_private;                 /* true → deliver to to_thread_id only */
    int  to_thread_id;               /* Recipient worker index (used if is_private) */
} Task;

/*
 * TaskQueue
 * FIFO queue shared between the client generator and the worker pool.
 * Protected by its own mutex + semaphore.
 * Owner: Aly Muhammad Kazani [24K-0512]
 */
#define TASK_QUEUE_CAP 512

typedef struct {
    Task            tasks[TASK_QUEUE_CAP];
    int             head;
    int             tail;
    int             count;
    pthread_mutex_t mutex;
    sem_t           tasks_available;   /* Signals workers that work exists */
} TaskQueue;

/*
 * WorkerThread
 * Per-thread bookkeeping visible to the GUI and logger.
 * Owner: Aly Muhammad Kazani [24K-0512]
 */
typedef struct {
    pthread_t    tid;
    int          index;              /* 0-based index in pool array    */
    ThreadState  state;
    int          room_assigned;      /* -1 = none, -2 = handling PM    */
    atomic_int   msgs_sent;
    char         current_sender[32]; /* Last client handled            */
    long long    last_active_us;

    /* §9.2 – Private Message inbox (ring buffer, mutex-protected) */
    PrivateMessage pm_inbox[PM_INBOX_SLOTS];
    int            pm_head;          /* Oldest unread slot             */
    int            pm_tail;          /* Next write slot                */
    int            pm_count;         /* Number of unread PMs           */
    int            pm_total;         /* All-time PM received count     */
    pthread_mutex_t pm_mutex;        /* Protects pm_inbox ring         */

    /* §9.2 – Rate-limiting: per-thread message token bucket */
    long long    rate_last_us;       /* Timestamp of last message sent */
    int          rate_tokens;        /* Available send tokens (refilled each second) */
} WorkerThread;

/*
 * LogEntry
 * One structured event record written by every thread.
 * Owner: Sana Munir Alam [24K-0573]
 */
typedef struct {
    int          thread_id;
    long long    timestamp_us;
    LogEventType event_type;
    char         room;               /* 'A', 'B', 'C', or '-'         */
    int          sem_value;
    int          msg_len;
    int          msgs_sent;
    char         detail[64];         /* Free-form supplementary text  */
} LogEntry;

/*
 * SimConfig
 * Runtime configuration parsed from CLI flags.
 * Owner: Aly Muhammad Kazani [24K-0512]
 */
typedef struct {
    int   num_threads;        /* --threads   (5-10, default 8)           */
    int   num_clients;        /* --clients   (1-50, default 30)          */
    int   num_rooms;          /* --rooms     (fixed at 3 in this build)  */
    int   duration_sec;       /* --duration  (seconds, default 60)       */
    float arrival_rate;       /* requests per second, adjusted by slider */
    int   rate_limit_per_sec; /* §9.2 --ratelimit: max msgs/sec per client (default 20) */
} SimConfig;

/*
 * SimState
 * Shared simulation state read by the GUI on every frame.
 * All reads from the GUI thread are lock-free via atomic_int counters;
 * string fields are protected by state_mutex.
 * Owner: Adeena Asif [24K-0628]
 */
typedef struct {
    /* --- core objects -------------------------------------------------- */
    ChatRoom      rooms[NUM_ROOMS];
    WorkerThread  workers[MAX_WORKERS];
    TaskQueue     queue;

    /* --- global counters (atomic, no lock needed to read) -------------- */
    atomic_int    total_messages;     /* All rooms combined             */
    atomic_int    total_pm;           /* §9.2 – private messages sent   */
    atomic_int    active_clients;     /* Currently inside sem_wait      */
    atomic_int    blocked_clients;    /* Waiting at sem_wait            */
    atomic_int    sem_value;          /* Current semaphore count        */
    atomic_int    rate_limited_count; /* §9.2 – total throttled drops   */

    /* --- semaphore controlling admission ------------------------------- */
    sem_t         admission_sem;

    /* --- control flags ------------------------------------------------- */
    atomic_bool   running;            /* false → all threads exit       */
    atomic_bool   paused;             /* true  → client gen pauses      */
    float         speed_multiplier;   /* 1.0 = normal, 0.1 = slow       */

    /* --- log ring buffer (written by workers, flushed on export) ------- */
    LogEntry      log_ring[MAX_LOG_ENTRIES];
    int           log_head;           /* Next write position            */
    int           log_count;
    pthread_mutex_t log_mutex;

    /* --- configuration ------------------------------------------------- */
    SimConfig     config;
    long long     start_time_us;      /* Epoch us at sim start          */

    /* --- state_mutex protects WorkerThread string fields --------------- */
    pthread_mutex_t state_mutex;
} SimState;

/* ─────────────────────────────────────────────────────────────────────────────
 * GLOBAL SINGLETON (defined in main.c, extern everywhere else)
 * ───────────────────────────────────────────────────────────────────────────*/
extern SimState g_sim;

/* ─────────────────────────────────────────────────────────────────────────────
 * FUNCTION PROTOTYPES
 * ───────────────────────────────────────────────────────────────────────────*/

/* --- threadpool.c  (Aly Muhammad Kazani) ---------------------------------- */
void  threadpool_init(int num_workers);
void  threadpool_shutdown(void);
void  taskqueue_push(Task *t);
bool  taskqueue_pop(Task *out);

/* --- rooms.c  (Sana Munir Alam) ------------------------------------------ */
void  rooms_init(void);
bool  room_write(int room_id, const char *sender, const char *msg,
                 int thread_id);
int   room_read_latest(int room_id, ChatMessage *out, int max_msgs);

/* --- privmsg.c  (Aly Muhammad Kazani) – §9.2 Additional Feature ---------- */
void  privmsg_init(void);
bool  privmsg_send(int from_thread, int to_thread,
                   const char *from_name, const char *text);
int   privmsg_read(int thread_id, PrivateMessage *out, int max_msgs);
int   privmsg_unread(int thread_id);

/* --- logger.c  (Sana Munir Alam) ----------------------------------------- */
void  logger_init(void);
void  logger_log(int thread_id, LogEventType event, char room,
                 int sem_val, int msg_len, int msgs_sent,
                 const char *detail);
void  logger_export(void);
long long now_us(void);  /* microseconds since epoch */

/* --- client_gen.c  (Aly Muhammad Kazani) ---------------------------------- */
void *client_generator_thread(void *arg);

/* --- gui.c  (Adeena Asif) ------------------------------------------------ */
void  gui_run(void);

/* --- utils.c  (shared) ---------------------------------------------------- */
void  parse_args(int argc, char **argv, SimConfig *cfg);
const char *thread_state_name(ThreadState s);
const char *log_event_name(LogEventType e);

/* --- ratelimit.c  (Aly Muhammad Kazani) – §9.2 Additional Feature --------- */
/* Returns true if the client is allowed to send (within rate limit),
 * false if the message should be dropped/deferred (rate exceeded).        */
bool  ratelimit_check(int thread_id);

#endif /* CHAT_SIM_H */
