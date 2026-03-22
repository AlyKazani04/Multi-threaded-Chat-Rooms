// threadpool.c
//
// This is the core of the project. Thread pool + task queue + worker loop.
// Proposal pseudocode section 7.1 and 7.2 maps directly to this file.
//
// Functions to implement:
//
// --- threadpool_init(int num_workers) ---
//    clamp num_workers to [MIN_WORKERS, MAX_WORKERS]
//    init the TaskQueue: head=0, tail=0, count=0
//    pthread_mutex_init for queue mutex
//    sem_init for tasks_available starting at 0 (no tasks yet)
//    sem_init for g_sim.admission_sem starting at MAX_CLIENTS (50)
//    set atomic running=true, paused=false
//    call privmsg_init() here to set up PM inboxes before threads start
//    loop i=0 to num_workers:
//       init WorkerThread fields (index, state=IDLE, room_assigned=-1 etc)
//       malloc an int* for the index (thread arg)
//       pthread_create --> worker_thread
//       log THREAD_CREATE event
//    print confirmation line
//
// --- threadpool_shutdown() ---
//    set running=false, paused=false
//    post num_workers sentinel wakeups to tasks_available
//       so every blocked worker wakes up and sees running=false
//    broadcast cond_not_full and cond_not_empty on all rooms
//       so any thread stuck in cond_wait inside room_write also wakes up
//    pthread_join all workers
//    destroy queue mutex, tasks_available semaphore, admission_sem
//    destroy all pm_mutex (one per worker) -- added for §9.2
//
// --- taskqueue_push(Task *t) ---
//    lock queue mutex
//    if queue is full (count >= 512): drop oldest (advance head, decrement count)
//    copy task into tasks[tail], advance tail, increment count
//    unlock queue mutex
//    sem_post tasks_available  <-- wakes one waiting worker
//
// --- taskqueue_pop(Task *out) ---
//    lock queue mutex
//    if count==0: unlock and return false
//    copy tasks[head] into *out, advance head, decrement count
//    unlock and return true
//
// --- worker_thread(void *arg) [STATIC, not in header] ---
//    this is the body of each worker. runs in a loop while running==true.
//
//    LOOP:
//      sem_wait(&tasks_available)       <-- block until work exists
//      if !running: break               <-- shutdown sentinel check
//
//      taskqueue_pop(&t)                <-- get the task
//
//      check if sem==0 before waiting (log BLOCKING if so, set WAITING state)
//      sem_wait(&admission_sem)         <-- block if 50 clients already active
//                                           THIS IS THE SEMAPHORE BOUNDARY TEST
//      update sem_value atomic, increment active_clients
//      log SEM_WAIT ADMITTED
//
//      lock state_mutex
//      set self->state = ACTIVE
//      set self->room_assigned = t.is_private ? -2 : t.room_id
//      copy sender name
//      unlock state_mutex
//
//      // §9.2 rate limit check -- comes BEFORE writing
//      if (!ratelimit_check(idx)):
//          release admission sem, decrement active_clients, go IDLE, continue
//
//      // route the message
//      if t.is_private:
//          privmsg_send(idx, t.to_thread_id, t.sender, t.message)
//      else:
//          room_write(t.room_id, t.sender, t.message, idx)
//          // room_write handles the mutex+cond_wait internally
//
//      increment msgs_sent and total_messages
//      log MSG_BROADCAST or PRIVATE_MSG
//
//      sem_post(&admission_sem)         <-- release the slot
//      update sem_value, decrement active_clients
//      log SEM_POST
//
//      lock state_mutex, set IDLE, room_assigned=-1, unlock
//      sched_yield()
//
//    log THREAD_EXIT on the way out
//    return NULL