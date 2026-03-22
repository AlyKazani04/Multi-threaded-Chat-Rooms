// utils.c
// -------
// Shared by all three members -- just helper functions, nothing complex.
//
// Functions to implement:
//
// --- parse_args(int argc, char **argv, SimConfig *cfg) ---
//    sets defaults first:
//       num_threads = 8
//       num_clients = 30
//       num_rooms = 3 (fixed, dont change)
//       duration_sec = 60
//       arrival_rate = 5.0f
//       rate_limit_per_sec = 20  (§9.2 default)
//
//    loop through argv looking for flags:
//       --threads N   --> clamp to [MIN_WORKERS, MAX_WORKERS]
//       --clients N   --> clamp to [1, MAX_CLIENTS]
//       --rooms N     --> accept but ignore (always 3)
//       --duration N  --> clamp to [5, 3600]
//       --rate F      --> clamp to [0.5, 50.0]
//       --ratelimit N --> clamp to [1, 1000]  §9.2
//    unknown flags: silently ignore
//
// --- thread_state_name(ThreadState s) ---
//    simple switch:
//       THREAD_IDLE    --> "IDLE"
//       THREAD_ACTIVE  --> "ACTIVE"
//       THREAD_WAITING --> "WAITING"
//       default        --> "UNKNOWN"
//
// --- log_event_name(LogEventType e) ---
//    simple switch returning the exact string that appears in the log file:
//       LOG_THREAD_CREATE  --> "THREAD_CREATE"
//       LOG_ACQUIRED_LOCK  --> "ACQUIRED_LOCK"
//       LOG_RELEASED_LOCK  --> "RELEASED_LOCK"
//       LOG_SEM_WAIT       --> "SEM_WAIT"
//       LOG_SEM_POST       --> "SEM_POST"
//       LOG_MSG_BROADCAST  --> "MSG_BROADCAST"
//       LOG_THREAD_EXIT    --> "THREAD_EXIT"
//       LOG_BUFFER_FULL    --> "BUFFER_FULL"
//       LOG_BUFFER_WRAP    --> "BUFFER_WRAP"
//       LOG_PRIVATE_MSG    --> "PRIVATE_MSG"   9.2
//       LOG_RATE_LIMITED   --> "RATE_LIMITED"  9.2
//       default            --> "UNKNOWN_EVENT"
//
// thats literally it for this file. keep it simple.