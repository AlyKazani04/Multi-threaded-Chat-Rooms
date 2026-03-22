// client_gen.c
// ------------
//
// This file simulates clients arriving and sending messages.
// Runs in its own thread (spawned from main.c).
// It does NOT process messages -- it just builds Tasks and pushes
// them onto the task queue for the worker threads to handle.
//
// Data needed (define as static arrays inside this file):
//
//    CLIENT_NAMES[20] = { "Alice", "Bob", "Charlie", ... "Tina" }
//
//    ROOM_A_MSGS[10] = general chat phrases
//    ROOM_B_MSGS[10] = urgent / priority phrases
//    ROOM_C_MSGS[10] = private / confidential phrases
//
// Functions to implement:
//
// --- make_message(int room_id, int client_id, const char *name, char *out, size_t out_sz) [static helper] ---
//    picks the right phrase array based on room_id
//    formats: "[C003] Alice: Hello everyone!"
//    snprintf into out
//
// --- client_generator_thread(void *arg) ---
//    arg is unused, cast to void
//
//    calculate end_us = g_sim.start_time_us + duration_sec * 1000000
//
//    int client_seq = 0  // global counter for [C###] prefix
//
//    LOOP while running==true:
//
//       check if now_us() >= end_us:
//          print "Duration elapsed" message
//          set running=false and break
//
//       if paused==true:
//          nanosleep 100ms and continue
//
//       build a Task:
//          room_id = client_seq % NUM_ROOMS   (cycles A B C A B C...)
//          name_idx = client_seq % 20
//
//          §9.2 PRIVATE MESSAGE LOGIC:
//          if (client_seq > 0 && client_seq % 8 == 0 && num_threads >= 2):
//             set task.is_private = true
//             set task.room_id = -1
//             from_t = client_seq % num_threads
//             to_t = (from_t + 1) % num_threads
//             task.to_thread_id = to_t
//             build a "[DM to T%d]..." message
//             print [PM-GEN] line to console
//          else:
//             set task.is_private = false
//             call make_message() for room message
//             print normal client arrival line to console
//
//       taskqueue_push(&t)   <-- wakes one worker via sem_post internally
//
//       client_seq++
//
//       calculate sleep delay:
//          rate = config.arrival_rate (default 5.0)
//          delay = (1.0 / rate) / speed_multiplier
//          floor delay at 5ms minimum
//          nanosleep(delay)
//
//    return NULL
//
// NOTE: the is_private flag and to_thread_id are checked in threadpool.c by worker_thread. this file just generates the tasks.
