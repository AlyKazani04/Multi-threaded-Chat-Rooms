// rooms.c
// -------
//
// This file implements the Producer-Consumer (Bounded-Buffer) problem
// from Silberschatz ch.6. Each chat room IS the bounded buffer.
//
// Functions to implement:
//
// --- rooms_init() ---
//    loop over NUM_ROOMS (3):
//       set room->name ("Room A", "Room B", "Room C")
//       set room->label ("General", "Priority", "Private")
//       head=0, tail=0, count=0, total_written=0
//       memset buffer to 0
//       pthread_mutex_init(&room->mutex, NULL)
//       pthread_cond_init(&room->cond_not_full, NULL)   <-- producer waits here
//       pthread_cond_init(&room->cond_not_empty, NULL)  <-- consumer waits here
//    print confirmation
//
// --- room_write(int room_id, const char *sender, const char *msg, int thread_id) ---
//    this is the PRODUCER side of bounded buffer
//
//    get pointer to the correct room (rooms[room_id])
//
//    pthread_mutex_lock(&room->mutex)
//    log ACQUIRED_LOCK
//
//    while (room->count >= BUFFER_SLOTS):
//       // buffer is full -- producer must wait
//       // THIS is where cond_not_full is used
//       if !running: unlock and return false  <-- shutdown escape
//       log BUFFER_FULL
//       set worker state to WAITING (lock state_mutex for this)
//       pthread_cond_wait(&room->cond_not_full, &room->mutex)
//          NOTE: cond_wait atomically releases mutex and sleeps.
//                when it returns, mutex is re-acquired automatically.
//       set worker state back to ACTIVE
//
//    // now we have space -- write the message
//    slot = &room->buffer[room->tail]
//    strncpy sender, text into slot
//    set slot->timestamp_us = now_us() - g_sim.start_time_us
//    set slot->thread_id = thread_id
//    check if tail+1 == BUFFER_SLOTS (about to wrap) --> log BUFFER_WRAP
//    room->tail = (room->tail + 1) % BUFFER_SLOTS
//    room->count++
//    room->total_written++
//
//    // wake a consumer (the GUI reads via room_read_latest)
//    pthread_cond_signal(&room->cond_not_empty)
//
//    pthread_mutex_unlock(&room->mutex)
//    log RELEASED_LOCK
//    return true
//
//    IMPORTANT: the message index update AND string copy are both
//    inside the critical section. this is intentional -- no consumer
//    can read a half-written message. mention this in the report.
//
// --- room_read_latest(int room_id, ChatMessage *out, int max_msgs) ---
//    this is used by the GUI to display messages
//    pthread_mutex_lock
//    n = min(room->count, max_msgs)
//    walk backwards from (tail-1) to get most recent first
//       src = (room->tail - 1 - i + BUFFER_SLOTS) % BUFFER_SLOTS
//       out[i] = room->buffer[src]
//    pthread_mutex_unlock
//    return n