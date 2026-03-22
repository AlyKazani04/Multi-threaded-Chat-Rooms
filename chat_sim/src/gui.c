// gui.c
//
// Builds the real-time visualization using raylib + raygui.
// raygui is a single-header immediate-mode UI library.
// It must be included in EXACTLY ONE .c file like this:
//    #include "raylib.h"
//    #define RAYGUI_IMPLEMENTATION
//    #include "raygui.h"
// Do not include raygui.h anywhere else or you'll get linker errors.
//
// IMPORTANT: raygui downloaded from github calls TextToFloat() internally
// which was removed in raylib 5.0. Add this BEFORE including raygui.h:
//    static inline float TextToFloat(const char *text) {
//        return text ? strtof(text, NULL) : 0.0f;
//    }
// Without this you'll get "undefined reference to TextToFloat" at link time.
//
// Window: 1280 x 860 pixels [ Might need to enlarge it if it gets too crowded]
//
// Layout (split into 4 panels + title bar):
//
//   [Title bar - full width, 48px tall]
//   [Room A chat] [Room B chat] [Room C chat] | [Thread Status table]
//   [Private Messages panel - purple      ] | [System Monitor      ]
//
// Left side (60% of window width):
//   - Top 62%: three chat room panels side by side (one per room)
//   - Bottom 38%: private messages panel (purple border, §9.2)
//
// Right side (40% of window width):
//   - Top 48%: thread status table
//   - Bottom 52%: system monitor
//
// Panels to implement:
//
// --- draw_panel_title(Rectangle r, const char *title) [static] ---
//    draws a filled blue rectangle as the panel header
//    draws white text inside it
//
// --- draw_chat_panel(int room_id, Rectangle panel) [static] ---
//    title bar shows room name, label, total message count
//    dark background for content area
//    call room_read_latest(room_id, msgs, 12) to get latest 12 messages
//    display each as: "[T%d] %ss  sender: text"
//    alternate row shading for readability
//
// --- draw_thread_panel(Rectangle panel) [static] ---
//    title: "Thread Status [ID | STATE | ROOM | MSGS | CLIENT | PM]"
//    one row per worker thread
//    columns: ID, STATE badge (coloured box), ROOM, MSGS, CLIENT, PM inbox count
//    STATE colours: grey=IDLE, green=ACTIVE, yellow=WAITING
//    if room_assigned == -2: show "PM" in purple (worker handling a private msg)
//    PM inbox badge: purple highlight if unread > 0
//    call privmsg_unread(i) for the badge count
//    lock state_mutex when reading string fields from WorkerThread
//
// --- draw_pm_panel(Rectangle panel) [static] ---
//    §9.2 feature -- purple-tinted title bar
//    title shows total PM count
//    collect recent PMs from all thread inboxes using privmsg_read()
//    sort by timestamp descending (most recent first)
//    display each as: "%ss  T%d→T%d  sender: text"
//    if no PMs yet: show "No private messages yet..."
//
// --- draw_monitor_panel(Rectangle panel) [static] ---
//    semaphore progress bar:
//       background bar, filled portion changes colour (green->orange->red)
//       label shows "Admission Slots: X / 50"
//    counters (read from atomic globals, no lock needed):
//       Total Messages Sent, Private Messages, Rate-Limited Drops,
//       Active Clients, Blocked Clients, Task Queue Depth, Elapsed Time
//    Arrival Rate slider (1-50): adjusts config.arrival_rate live
//    Rate Limit slider (1-100): adjusts config.rate_limit_per_sec live §9.2
//    Pause/Resume button: toggles g_sim.paused
//    Export Log button: calls logger_export()
//    Per-room message counts at the bottom
//
// --- gui_run() ---
//    InitWindow(1280, 860, "...")
//    SetTargetFPS(30)
//    compute all panel rectangles (do this once before the loop)
//    set raygui styles (dark theme colours for buttons, sliders etc)
//    LOOP while !WindowShouldClose() && running:
//       BeginDrawing()
//       ClearBackground (very dark navy)
//       draw title bar (group name, config info, RUNNING/PAUSED indicator)
//       call draw_chat_panel for each of 3 rooms
//       call draw_pm_panel
//       call draw_thread_panel
//       call draw_monitor_panel
//       draw legend at bottom (colour key for thread states)
//       EndDrawing()
//    logger_export() on exit
//    CloseWindow()
//
// The GUI runs on the MAIN thread. All the worker threads run in background.
// GUI reads shared state -- uses atomic reads for counters (no lock),
// locks state_mutex only when reading string fields from WorkerThread.