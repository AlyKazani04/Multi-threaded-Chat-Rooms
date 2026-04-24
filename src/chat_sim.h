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
#include <sched.h>

/* ─────────────────────────────────────────────────────────────────────────────
 * COMPILE-TIME CONSTANTS
 * ───────────────────────────────────────────────────────────────────────────*/
#define MAX_WORKERS        10
#define MIN_WORKERS        5
#define MAX_CLIENTS        50
#define NUM_ROOMS          3
#define BUFFER_SLOTS       50
#define MAX_MSG_LEN        256
#define MAX_LOG_ENTRIES    4096
#define CLIENT_NAMES_COUNT 20
#define LOG_FILENAME       "chat_sim.log.txt"

#define PM_INBOX_SLOTS     32
#define DEFAULT_RATE_LIMIT 20

/* ─────────────────────────────────────────────────────────────────────────────
 * ENUMERATIONS
 * ───────────────────────────────────────────────────────────────────────────*/
typedef enum {
    THREAD_IDLE    = 0,
    THREAD_ACTIVE  = 1,
    THREAD_WAITING = 2
} ThreadState;

/* FIX-F/I/V: Added LOG_MSG_DROPPED, LOG_BUFFER_RESUME, LOG_SEM_BLOCK */
typedef enum {
    LOG_THREAD_CREATE,
    LOG_ACQUIRED_LOCK,
    LOG_RELEASED_LOCK,
    LOG_SEM_WAIT,        /* post-admit  (ADMITTED)            */
    LOG_SEM_POST,
    LOG_MSG_BROADCAST,
    LOG_THREAD_EXIT,
    LOG_BUFFER_FULL,
    LOG_BUFFER_WRAP,
    LOG_PRIVATE_MSG,
    LOG_RATE_LIMITED,
    LOG_MSG_DROPPED,
    LOG_BUFFER_RESUME, 
    LOG_SEM_BLOCK 
} LogEventType;

/* ─────────────────────────────────────────────────────────────────────────────
 * STRUCTS
 * ───────────────────────────────────────────────────────────────────────────*/
typedef struct {
    char      sender[32];
    char      text[MAX_MSG_LEN];
    long long timestamp_us;
    int       thread_id;
} ChatMessage;

typedef struct {
    int       from_thread;
    int       to_thread;
    char      from_name[32];
    char      text[MAX_MSG_LEN];
    long long timestamp_us;
} PrivateMessage;

typedef struct {
    char          name[16];
    char          label[32];
    ChatMessage   buffer[BUFFER_SLOTS];
    int           head;
    int           tail;
    int           count;
    int           total_written;
    int           total_consumed;
    long long total_mutex_wait_ns;
    int       mutex_wait_count;
    pthread_mutex_t mutex;
    pthread_cond_t  cond_not_full;
    pthread_cond_t  cond_not_empty;
} ChatRoom;

typedef struct {
    int  room_id;
    char sender[32];
    char message[MAX_MSG_LEN];
    int  client_id;
    bool is_private;
    int  from_thread_id;
    int  to_thread_id;
} Task;

#define TASK_QUEUE_CAP 512

typedef struct {
    Task            tasks[TASK_QUEUE_CAP];
    int             head;
    int             tail;
    int             count;
    pthread_mutex_t mutex;
    sem_t           tasks_available;
} TaskQueue;

typedef struct {
    pthread_t    tid;
    int          index;
    ThreadState  state;
    int          room_assigned;
    atomic_int   msgs_sent;
    char         current_sender[32];
    long long    last_active_us;
    int          last_room_used;

    PrivateMessage pm_inbox[PM_INBOX_SLOTS];
    int            pm_head;
    int            pm_tail;
    int            pm_count;
    int            pm_total;
    pthread_mutex_t pm_mutex;

    long long    rate_last_us;
    int          rate_tokens;
} WorkerThread;

typedef struct {
    int          thread_id;
    long long    timestamp_us;
    LogEventType event_type;
    char         room;
    int          sem_value;
    int          msg_len;
    int          msgs_sent;
    char         detail[64];
} LogEntry;

typedef struct {
    int   num_threads;
    int   num_clients;
    int   num_rooms;
    int   duration_sec;
    float arrival_rate;
    int   rate_limit_per_sec;
} SimConfig;

typedef struct {
    ChatRoom      rooms[NUM_ROOMS];
    WorkerThread  workers[MAX_WORKERS];
    TaskQueue     queue;

    pthread_t     consumer_tids[NUM_ROOMS];

    atomic_int    total_messages;
    atomic_int    total_pm;
    atomic_int    active_clients;
    atomic_int    blocked_clients;
    atomic_int    sem_value;
    atomic_int    rate_limited_count;
    atomic_int    total_dropped;

    sem_t         admission_sem;

    atomic_bool   running;
    atomic_bool   paused;
    float         speed_multiplier;

    LogEntry      log_ring[MAX_LOG_ENTRIES];
    int           log_head;
    int           log_count;
    pthread_mutex_t log_mutex;

    SimConfig     config;
    long long     start_time_us;

    pthread_mutex_t state_mutex;
    pthread_mutex_t rand_mutex;

    int throughput_samples[3600];  // one per second
    int throughput_sample_count;
} SimState;

/* ─────────────────────────────────────────────────────────────────────────────
 * GLOBAL SINGLETON
 * ───────────────────────────────────────────────────────────────────────────*/
extern SimState g_sim;

/* ─────────────────────────────────────────────────────────────────────────────
 * FUNCTION PROTOTYPES
 * ───────────────────────────────────────────────────────────────────────────*/

/* threadpool.c */
void  threadpool_init(int num_workers);
void  threadpool_shutdown(void);
void  taskqueue_push(Task *t);
bool  taskqueue_pop(Task *out);

/* rooms.c */
void  rooms_init(void);
bool  room_write(int room_id, const char *sender, const char *msg, int thread_id);
int   room_read_latest(int room_id, ChatMessage *out, int max_msgs);
void  rooms_consumer_init(void);
void  rooms_consumer_shutdown(void);

/* privmsg.c */
void  privmsg_init(void);
bool  privmsg_send(int from_thread, int to_thread, const char *from_name, const char *text);
int   privmsg_read(int thread_id, PrivateMessage *out, int max_msgs);
int   privmsg_unread(int thread_id);

/* logger.c */
void  logger_init(void);
void  logger_log(int thread_id, LogEventType event, char room, int sem_val, int msg_len, int msgs_sent, const char *detail);
void  logger_export(void);
long long now_us(void);

/* client_gen.c */
void *client_generator_thread(void *arg);

/* gui.c */
void  gui_run(void);

/* utils.c */
void  parse_args(int argc, char **argv, SimConfig *cfg);
const char *thread_state_name(ThreadState s);
const char *log_event_name(LogEventType e);
int   safe_rand_range(int n);  /* FIX-Q: thread-safe rand() wrapper */

/* ratelimit.c */
bool  ratelimit_check(int thread_id);

#endif /* CHAT_SIM_H */
