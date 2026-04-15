/*
 * gui.c
 * ─────────────────────────────────────────────────────────────────────────────
 * Project  : OS-Level Chat Server Using Thread Pools
 * Course   : CS2006 Operating Systems – Spring 2026
 *
 * Primary Author : Adeena Asif [24K-0628]  ← Frontend & Integration Lead
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include "chat_sim.h"

/* ── raylib / raygui includes ─────────────────────────────────────────────
 * raygui 4.x removed TextToFloat() from raylib but still references it
 * internally.  We provide a minimal implementation before the header so
 * the linker finds it.  This must appear BEFORE #include "raygui.h".
 * ─────────────────────────────────────────────────────────────────────── */
#include "raylib.h"

/* Fix for raygui 4.x using TextToFloat which was removed from raylib 5.x */
static inline float TextToFloat(const char *text)
{
    float value = 0.0f;
    if (text != NULL) value = strtof(text, NULL);
    return value;
}

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

/* ── Layout constants ──────────────────────────────────────────────────── */
#define WIN_W           1780
#define WIN_H           1040
#define PANEL_PAD       10
#define TITLE_H         36
#define FONT_SIZE_TITLE 20
#define FONT_SIZE_BODY  16
#define FONT_SIZE_SMALL 13

/* Panel rectangles (computed once in gui_run) */
static Rectangle r_chat[NUM_ROOMS];   /* One chat area per room           */
static Rectangle r_thread_panel;      /* Thread status table              */
static Rectangle r_monitor_panel;     /* System monitor                   */
static Rectangle r_pm_panel;          /* §9.2 – Private messages panel    */

/* ── Colour helpers ────────────────────────────────────────────────────── */
static Color STATE_COLOR[3] = {
    {120,120,120,255},   /* IDLE    – grey                         */
    {50, 200, 50, 255},  /* ACTIVE  – green                        */
    {255,215, 0, 255}    /* WAITING – yellow                       */
};
static const char *STATE_LABEL[3] = { "IDLE", "ACTIVE", "WAITING" };

/* ── draw_panel_title() ─────────────────────────────────────────────────
 * Owner: Adeena Asif [24K-0628]
 * Draws a filled rectangle header with white title text.
 * ──────────────────────────────────────────────────────────────────────*/
static void draw_panel_title(Rectangle r, const char *title)
{
    DrawRectangle((int)r.x, (int)r.y, (int)r.width, TITLE_H,
                  (Color){30, 80, 160, 255});
    DrawText(title,
             (int)(r.x + 10),
             (int)(r.y + (TITLE_H - FONT_SIZE_TITLE) / 2),
             FONT_SIZE_TITLE,
             WHITE);
}

/* ── draw_chat_panel() ──────────────────────────────────────────────────
 * Owner: Adeena Asif [24K-0628]
 * Renders one chat room's latest messages in a scroll area.
 * Reads room buffer via room_read_latest() (Sana's module – mutex-safe).
 * ──────────────────────────────────────────────────────────────────────*/
static void draw_chat_panel(int room_id, Rectangle panel)
{
    ChatRoom *room = &g_sim.rooms[room_id];

    /* Title bar */
    char title[64];
    snprintf(title, sizeof(title), " %s – %s  [%d msgs total]",
             room->name, room->label, room->total_written);
    draw_panel_title(panel, title);

    /* Scroll content area (below title bar) */
    Rectangle content_area = {
        panel.x, panel.y + TITLE_H,
        panel.width, panel.height - TITLE_H
    };
    DrawRectangleRec(content_area, (Color){18, 18, 30, 255});

    /* Fetch latest messages (mutex-safe) */
#define VISIBLE_MSGS 12
    ChatMessage msgs[VISIBLE_MSGS];
    int n = room_read_latest(room_id, msgs, VISIBLE_MSGS);

    int y = (int)(content_area.y + 6);
    for (int i = n - 1; i >= 0; i--) {
        /* Timestamp */
        char ts[16];
        snprintf(ts, sizeof(ts), "%05.2f",
                 (double)msgs[i].timestamp_us / 1e6);

        /* Thread indicator colour */
        Color tc = STATE_COLOR[THREAD_ACTIVE];

        /* [Thread-ID] Timestamp  sender: text */
        char line[320];
        snprintf(line, sizeof(line), "[T%d] %ss  %s: %s",
                 msgs[i].thread_id, ts,
                 msgs[i].sender, msgs[i].text);

        /* Alternate row shading for readability */
        if (i % 2 == 0)
            DrawRectangle((int)content_area.x, y,
                          (int)content_area.width, FONT_SIZE_BODY + 4,
                          (Color){28, 28, 45, 255});

        DrawText(line,
                 (int)(content_area.x + 6), y + 2,
                 FONT_SIZE_BODY, tc);

        y += FONT_SIZE_BODY + 5;
        if (y > content_area.y + content_area.height - FONT_SIZE_BODY)
            break;
    }
}

/* ── draw_thread_panel() ────────────────────────────────────────────────
 * Owner: Adeena Asif [24K-0628]
 * Renders the live thread status table with colour-coded state badges.
 * Shows PM unread badge count (§9.2) on each row.
 * ──────────────────────────────────────────────────────────────────────*/
static void draw_thread_panel(Rectangle panel)
{
    draw_panel_title(panel, " Thread Status  [ID | STATE | ROOM | MSGS | CLIENT | PM]");
    DrawRectangle((int)panel.x, (int)(panel.y + TITLE_H),
                  (int)panel.width, (int)(panel.height - TITLE_H),
                  (Color){20, 20, 35, 255});

    /* Column headers */
    int hx = (int)(panel.x + 6);
    int hy = (int)(panel.y + TITLE_H + 6);
    DrawText("ID",    hx,       hy, FONT_SIZE_SMALL, GRAY);
    DrawText("STATE", hx + 30,  hy, FONT_SIZE_SMALL, GRAY);
    DrawText("ROOM",  hx + 130, hy, FONT_SIZE_SMALL, GRAY);
    DrawText("MSGS",  hx + 190, hy, FONT_SIZE_SMALL, GRAY);
    DrawText("CLIENT",hx + 240, hy, FONT_SIZE_SMALL, GRAY);
    DrawText("PM IN", hx + 330, hy, FONT_SIZE_SMALL, GRAY);  /* §9.2 */
    DrawLine(hx, hy + FONT_SIZE_SMALL + 2,
             (int)(panel.x + panel.width - 6), hy + FONT_SIZE_SMALL + 2,
             (Color){60,60,90,255});

    int y = hy + FONT_SIZE_SMALL + 8;

    for (int i = 0; i < g_sim.config.num_threads; i++) {
        WorkerThread *w = &g_sim.workers[i];

        /* Acquire snapshot of string fields */
        pthread_mutex_lock(&g_sim.state_mutex);
        ThreadState st    = w->state;
        int room_ass      = w->room_assigned;
        int msgs          = atomic_load(&w->msgs_sent);
        char client[32];
        strncpy(client, w->current_sender, 31);
        pthread_mutex_unlock(&g_sim.state_mutex);

        /* PM unread count (own mutex) */
        int pm_unread = privmsg_unread(i);  /* §9.2 */

        /* Row background tint */
        if (st == THREAD_ACTIVE)
            DrawRectangle((int)panel.x + 2, y - 2,
                          (int)panel.width - 4, FONT_SIZE_BODY + 4,
                          (Color){0,60,0,80});
        else if (st == THREAD_WAITING)
            DrawRectangle((int)panel.x + 2, y - 2,
                          (int)panel.width - 4, FONT_SIZE_BODY + 4,
                          (Color){80,60,0,80});

        /* ID */
        char id_str[8];
        snprintf(id_str, sizeof(id_str), "T%d", i);
        DrawText(id_str, hx, y, FONT_SIZE_BODY, WHITE);

        /* State badge */
        Color sc = STATE_COLOR[st];
        DrawRectangle(hx + 28, y, 90, FONT_SIZE_BODY + 2, (Color){sc.r,sc.g,sc.b,60});
        DrawRectangleLines(hx + 28, y, 90, FONT_SIZE_BODY + 2, sc);
        DrawText(STATE_LABEL[st], hx + 32, y + 1, FONT_SIZE_SMALL, sc);

        /* Room – show "PM" if handling a private message (room_assigned == -2) */
        char room_str[8];
        if (room_ass == -2)
            strncpy(room_str, "PM", sizeof(room_str));  /* §9.2 */
        else if (room_ass >= 0)
            snprintf(room_str, sizeof(room_str), "Rm %c", 'A' + room_ass);
        else
            strncpy(room_str, "—", sizeof(room_str));

        Color room_col = (room_ass == -2) ? (Color){255,150,255,255} : LIGHTGRAY;
        DrawText(room_str, hx + 130, y, FONT_SIZE_BODY, room_col);

        /* Msgs sent */
        char msgs_str[16];
        snprintf(msgs_str, sizeof(msgs_str), "%d", msgs);
        DrawText(msgs_str, hx + 190, y, FONT_SIZE_BODY, LIGHTGRAY);

        /* Current client */
        DrawText(client[0] ? client : "—", hx + 240, y, FONT_SIZE_SMALL,
                 (Color){180,180,220,255});

        /* §9.2 – PM inbox badge: show count in purple, highlight if unread */
        char pm_str[16];
        snprintf(pm_str, sizeof(pm_str), "%d", pm_unread);
        Color pm_col = (pm_unread > 0) ? (Color){220,100,255,255}
                                        : (Color){80,80,100,255};
        if (pm_unread > 0) {
            DrawRectangle(hx + 328, y, 32, FONT_SIZE_BODY + 2,
                          (Color){100,0,120,100});
        }
        DrawText(pm_str, hx + 332, y + 1, FONT_SIZE_SMALL, pm_col);

        y += FONT_SIZE_BODY + 8;
    }
}

/* ── draw_monitor_panel() ───────────────────────────────────────────────
 * Owner: Adeena Asif [24K-0628]
 * Renders the System Monitor panel:
 *   • Semaphore progress bar
 *   • Active / Blocked / Total messages counters
 *   • Pause/Resume toggle button
 *   • Speed slider (1–50 req/sec)
 *   • Export Log button
 * ──────────────────────────────────────────────────────────────────────*/
static void draw_monitor_panel(Rectangle panel)
{
    draw_panel_title(panel, " System Monitor");
    DrawRectangle((int)panel.x, (int)(panel.y + TITLE_H),
                  (int)panel.width, (int)(panel.height - TITLE_H),
                  (Color){20, 20, 35, 255});

    int x  = (int)(panel.x + 12);
    int y  = (int)(panel.y + TITLE_H + 12);
    int pw = (int)(panel.width - 24);  /* usable width */

    /* ── Semaphore progress bar ─────────────────────────────────────── */
    int sv = atomic_load(&g_sim.sem_value);
    if (sv < 0) sv = 0;
    if (sv > MAX_CLIENTS) sv = MAX_CLIENTS;

    char sem_label[64];
    snprintf(sem_label, sizeof(sem_label),
             "Admission Slots: %d / %d", sv, MAX_CLIENTS);
    DrawText(sem_label, x, y, FONT_SIZE_BODY, WHITE);
    y += FONT_SIZE_BODY + 4;

    /* Background bar */
    DrawRectangle(x, y, pw, 22, (Color){40, 40, 60, 255});
    /* Filled portion – green when plenty of slots, red when scarce */
    float frac  = (float)sv / MAX_CLIENTS;
    Color barcol = (sv > 20) ? (Color){50,200,50,255} :
                   (sv >  5) ? (Color){255,165,0,255} :
                               (Color){220,50,50,255};
    DrawRectangle(x, y, (int)(pw * frac), 22, barcol);
    DrawRectangleLines(x, y, pw, 22, (Color){100,100,130,255});
    y += 30;

    /* ── Counters ────────────────────────────────────────────────────── */
    int total   = atomic_load(&g_sim.total_messages);
    int active  = atomic_load(&g_sim.active_clients);
    int blocked = atomic_load(&g_sim.blocked_clients);

    char line[128];
    snprintf(line, sizeof(line), "Total Messages Sent   : %d", total);
    DrawText(line, x, y, FONT_SIZE_BODY, LIGHTGRAY); y += FONT_SIZE_BODY + 6;

    snprintf(line, sizeof(line), "Private Messages (PM) : %d",     /* §9.2 */
             atomic_load(&g_sim.total_pm));
    DrawText(line, x, y, FONT_SIZE_BODY, (Color){200,120,255,255}); y += FONT_SIZE_BODY + 6;

    snprintf(line, sizeof(line), "Rate-Limited Drops    : %d",     /* §9.2 */
             atomic_load(&g_sim.rate_limited_count));
    Color rlc = (atomic_load(&g_sim.rate_limited_count) > 0)
                ? (Color){255,100,100,255} : LIGHTGRAY;
    DrawText(line, x, y, FONT_SIZE_BODY, rlc); y += FONT_SIZE_BODY + 6;

    snprintf(line, sizeof(line), "Active Clients (now)  : %d", active);
    DrawText(line, x, y, FONT_SIZE_BODY, (Color){100,220,100,255}); y += FONT_SIZE_BODY + 6;

    snprintf(line, sizeof(line), "Blocked Clients (now) : %d", blocked);
    Color bc = (blocked > 0) ? (Color){255,80,80,255} : LIGHTGRAY;
    DrawText(line, x, y, FONT_SIZE_BODY, bc); y += FONT_SIZE_BODY + 6;

    /* Queue depth */
    pthread_mutex_lock(&g_sim.queue.mutex);
    int qd = g_sim.queue.count;
    pthread_mutex_unlock(&g_sim.queue.mutex);
    snprintf(line, sizeof(line), "Task Queue Depth      : %d", qd);
    DrawText(line, x, y, FONT_SIZE_BODY, LIGHTGRAY); y += FONT_SIZE_BODY + 6;

    /* Elapsed time */
    double elapsed = (double)(now_us() - g_sim.start_time_us) / 1e6;
    snprintf(line, sizeof(line), "Elapsed Time (s)      : %.1f", elapsed);
    DrawText(line, x, y, FONT_SIZE_BODY, LIGHTGRAY); y += FONT_SIZE_BODY + 14;

    /* ── Speed slider ────────────────────────────────────────────────── */
    DrawText("Arrival Rate (req/s):", x, y, FONT_SIZE_BODY, WHITE);
    y += FONT_SIZE_BODY + 4;

    float speed = g_sim.config.arrival_rate;
    Rectangle slider_rect = { (float)x, (float)y, (float)pw, 20.0f };
    GuiSlider(slider_rect, "1", "50", &speed, 1.0f, 50.0f);
    g_sim.config.arrival_rate = speed;
    y += 30;

    char sp_txt[32];
    snprintf(sp_txt, sizeof(sp_txt), "  %.0f req/sec", speed);
    DrawText(sp_txt, x, y, FONT_SIZE_SMALL, (Color){200,200,200,255});
    y += FONT_SIZE_SMALL + 10;

    /* ── §9.2 Rate-Limit slider ──────────────────────────────────────── */
    DrawText("Rate Limit (msg/s/client):", x, y, FONT_SIZE_BODY, WHITE);
    y += FONT_SIZE_BODY + 4;

    float rl_val = (float)g_sim.config.rate_limit_per_sec;
    Rectangle rl_slider = { (float)x, (float)y, (float)pw, 20.0f };
    GuiSlider(rl_slider, "1", "100", &rl_val, 1.0f, 100.0f);
    g_sim.config.rate_limit_per_sec = (int)rl_val;
    y += 30;

    char rl_txt[48];
    snprintf(rl_txt, sizeof(rl_txt), "  %.0f msg/sec  (drops: %d)",
             rl_val, atomic_load(&g_sim.rate_limited_count));
    DrawText(rl_txt, x, y, FONT_SIZE_SMALL, (Color){200,160,255,255});
    y += FONT_SIZE_SMALL + 10;

    /* ── Pause / Resume button ───────────────────────────────────────── */
    bool paused = atomic_load(&g_sim.paused);
    Rectangle btn_pause = { (float)x, (float)y, (float)(pw / 2 - 4), 36.0f };

    GuiSetStyle(BUTTON, TEXT_ALIGNMENT, TEXT_ALIGN_CENTER);
    if (GuiButton(btn_pause, paused ? "# RESUME" : "|| PAUSE")) {
        atomic_store(&g_sim.paused, !paused);
    }

    /* ── Export Log button ───────────────────────────────────────────── */
    Rectangle btn_export = { (float)(x + pw / 2 + 4), (float)y,
                             (float)(pw / 2 - 4),    36.0f };
    if (GuiButton(btn_export, "Export Log")) {
        logger_export();
    }
    y += 46;

    /* ── Per-room message counts ─────────────────────────────────────── */
    DrawText("Room Message Counts:", x, y, FONT_SIZE_BODY, WHITE);
    y += FONT_SIZE_BODY + 4;

    const Color ROOM_COLS[3] = {
        {100,180,255,255},   /* Room A – blue   */
        {255,140,100,255},   /* Room B – orange */
        {150,255,150,255}    /* Room C – green  */
    };
    for (int r = 0; r < NUM_ROOMS; r++) {
        pthread_mutex_lock(&g_sim.rooms[r].mutex);
        int tw = g_sim.rooms[r].total_written;
        int cnt= g_sim.rooms[r].count;
        pthread_mutex_unlock(&g_sim.rooms[r].mutex);

        char rmsg[80];
        snprintf(rmsg, sizeof(rmsg), "  %s (%s)  buf=%d/50  total=%d",
                 g_sim.rooms[r].name, g_sim.rooms[r].label, cnt, tw);
        DrawText(rmsg, x, y, FONT_SIZE_SMALL, ROOM_COLS[r]);
        y += FONT_SIZE_SMALL + 5;
    }
}

/*
 * draw_pm_panel()
 * ─────────────────────────────────────────────────────────────────────────────
 * Owner   : Adeena Asif [24K-0628]
 * Purpose : §9.2 Additional Feature – renders a live Private Messages feed
 *           showing the most recent direct thread-to-thread PMs across all
 *           inboxes.  Reads via privmsg_read() (Aly's module – mutex-safe).
 *
 *   Each row shows:
 *     [timestamp]  T<from> → T<to>  <sender name>: <message text>
 *
 *   The header badge shows total PM count.
 * ─────────────────────────────────────────────────────────────────────────────
 */
static void draw_pm_panel(Rectangle panel)
{
    /* Title with total PM count badge */
    char title[64];
    snprintf(title, sizeof(title), " Private Messages  [%d total]",
             atomic_load(&g_sim.total_pm));
    /* Purple-tinted title bar to distinguish from room panels */
    DrawRectangle((int)panel.x, (int)panel.y, (int)panel.width, TITLE_H,
                  (Color){90, 20, 120, 255});
    DrawText(title,
             (int)(panel.x + 10),
             (int)(panel.y + (TITLE_H - FONT_SIZE_TITLE) / 2),
             FONT_SIZE_TITLE, WHITE);

    /* Content area */
    Rectangle ca = { panel.x, panel.y + TITLE_H,
                     panel.width, panel.height - TITLE_H };
    DrawRectangleRec(ca, (Color){18, 12, 28, 255});

#define PM_VISIBLE 14
    int y = (int)(ca.y + 6);

    /* Collect PMs across all threads (up to PM_VISIBLE most recent) */
    PrivateMessage all_pms[MAX_WORKERS * 4];
    int total = 0;

    for (int t = 0; t < g_sim.config.num_threads && total < MAX_WORKERS * 4; t++) {
        PrivateMessage tmp[4];
        int n = privmsg_read(t, tmp, 4);
        for (int j = 0; j < n && total < MAX_WORKERS * 4; j++)
            all_pms[total++] = tmp[j];
    }

    /* Sort by timestamp descending (simple insertion sort – small N) */
    for (int i = 1; i < total; i++) {
        PrivateMessage key = all_pms[i];
        int j = i - 1;
        while (j >= 0 && all_pms[j].timestamp_us < key.timestamp_us) {
            all_pms[j + 1] = all_pms[j];
            j--;
        }
        all_pms[j + 1] = key;
    }

    int show = total < PM_VISIBLE ? total : PM_VISIBLE;
    for (int i = 0; i < show; i++) {
        PrivateMessage *pm = &all_pms[i];

        /* Alternating row shade */
        if (i % 2 == 0)
            DrawRectangle((int)ca.x, y, (int)ca.width, FONT_SIZE_BODY + 4,
                          (Color){35, 18, 50, 255});

        char ts[14];
        snprintf(ts, sizeof(ts), "%05.2f", (double)pm->timestamp_us / 1e6);

        char row[320];
        snprintf(row, sizeof(row), "%ss  T%d→T%d  %s: %s",
                 ts,
                 pm->from_thread, pm->to_thread,
                 pm->from_name, pm->text);

        DrawText(row, (int)(ca.x + 6), y + 2, FONT_SIZE_SMALL,
                 (Color){220, 160, 255, 255});

        y += FONT_SIZE_BODY + 5;
        if (y > ca.y + ca.height - FONT_SIZE_BODY) break;
    }

    if (total == 0) {
        DrawText("  No private messages yet...",
                 (int)(ca.x + 6), (int)(ca.y + 8),
                 FONT_SIZE_SMALL, (Color){80,60,100,255});
    }
}

/*
 * gui_run()
 * ─────────────────────────────────────────────────────────────────────────────
 * Owner   : Adeena Asif [24K-0628]
 * Purpose : Open the raylib window and run the immediate-mode GUI render loop.
 *           Called from the MAIN THREAD; blocks until the window is closed
 *           or the simulation stops.
 *
 *   Layout (1200 × 800 px):
 *   ┌─────────────────────────────────────────────────────────────────┐
 *   │  Title bar                                                       │
 *   ├──────────────┬──────────────┬──────────────┬────────────────────┤
 *   │  Room A      │  Room B      │  Room C      │  Thread Status     │
 *   │  (Chat msgs) │  (Chat msgs) │  (Chat msgs) │  (table)           │
 *   │              │              │              ├────────────────────┤
 *   │              │              │              │  System Monitor    │
 *   │              │              │              │  (sem bar / btns)  │
 *   └──────────────┴──────────────┴──────────────┴────────────────────┘
 * ─────────────────────────────────────────────────────────────────────────────
 */
void gui_run(void)
{
    /* ── Initialise raylib window ─────────────────────────────────────── */
    InitWindow(WIN_W, WIN_H,
               "CS2006 OS-Level Chat Server – Thread Pool Visualizer");
    SetTargetFPS(30);

    /* ── Compute panel layout ─────────────────────────────────────────── */
    int titlebar_h = 48;
    int left_w     = (WIN_W * 3) / 5;          /* 60% for chat + PM panels  */
    int right_w    = WIN_W - left_w - PANEL_PAD * 2;
    int content_h  = WIN_H - titlebar_h - PANEL_PAD * 3;

    /* Chat rooms: top 65% of left column */
    int chat_h     = (int)(content_h * 0.62f);
    int chat_w     = (left_w - PANEL_PAD * 2) / NUM_ROOMS;

    for (int r = 0; r < NUM_ROOMS; r++) {
        r_chat[r] = (Rectangle){
            (float)(PANEL_PAD + r * (chat_w + PANEL_PAD)),
            (float)(titlebar_h + PANEL_PAD),
            (float)chat_w,
            (float)chat_h
        };
    }

    /* §9.2 PM panel: bottom 35% of left column */
    int pm_y = titlebar_h + PANEL_PAD + chat_h + PANEL_PAD;
    int pm_h = WIN_H - pm_y - PANEL_PAD;
    r_pm_panel = (Rectangle){
        (float)PANEL_PAD,
        (float)pm_y,
        (float)(left_w - PANEL_PAD),
        (float)pm_h
    };

    int right_x = left_w + PANEL_PAD * 2;

    /* Thread status: top 48% of right column */
    int thread_h = (int)(content_h * 0.48f);
    r_thread_panel = (Rectangle){
        (float)right_x,
        (float)(titlebar_h + PANEL_PAD),
        (float)right_w,
        (float)thread_h
    };

    /* System monitor: bottom 52% of right column */
    int mon_y = titlebar_h + PANEL_PAD + thread_h + PANEL_PAD;
    int mon_h = WIN_H - mon_y - PANEL_PAD;
    r_monitor_panel = (Rectangle){
        (float)right_x,
        (float)mon_y,
        (float)right_w,
        (float)mon_h
    };

    /* ── raygui style tweaks ─────────────────────────────────────────── */
    GuiSetStyle(DEFAULT, TEXT_SIZE, FONT_SIZE_BODY);
    GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL,   ColorToInt((Color){40,40,60,255}));
    GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL,   ColorToInt(WHITE));
    GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL, ColorToInt((Color){80,80,120,255}));
    GuiSetStyle(BUTTON,  BASE_COLOR_NORMAL,   ColorToInt((Color){50,80,150,255}));
    GuiSetStyle(BUTTON,  TEXT_COLOR_NORMAL,   ColorToInt(WHITE));
    GuiSetStyle(BUTTON,  BASE_COLOR_FOCUSED,  ColorToInt((Color){70,110,200,255}));
    GuiSetStyle(BUTTON,  BASE_COLOR_PRESSED,  ColorToInt((Color){30,60,130,255}));
    GuiSetStyle(SLIDER,  BASE_COLOR_NORMAL,   ColorToInt((Color){30,100,200,255}));

    /* ── Main render loop ────────────────────────────────────────────── */
    while (!WindowShouldClose() && atomic_load(&g_sim.running)) {

        BeginDrawing();
        ClearBackground((Color){12, 12, 22, 255});

        /* ── Title bar ───────────────────────────────────────────────── */
        DrawRectangle(0, 0, WIN_W, titlebar_h, (Color){20,40,100,255});
        DrawText("OS-Level Chat Server  |  Thread Pool Visualizer",
                 12, 10, FONT_SIZE_TITLE, WHITE);

        double elapsed = (double)(now_us() - g_sim.start_time_us) / 1e6;
        int    rem     = g_sim.config.duration_sec - (int)elapsed;
        if (rem < 0) rem = 0;
        char hdr[128];
        snprintf(hdr, sizeof(hdr),
                 "Workers: %d   |   Clients: %d   |   Remaining: %ds",
                 g_sim.config.num_threads,
                 g_sim.config.num_clients,
                 rem);
        DrawText(hdr, WIN_W - 480, 14, FONT_SIZE_SMALL,
                 (Color){180,200,255,255});

        /* Running / Paused status indicator */
        bool paused = atomic_load(&g_sim.paused);
        const char *status = paused ? "  PAUSED  " : "  RUNNING  ";
        Color status_col   = paused ? (Color){255,180,0,255}
                                    : (Color){50,220,50,255};
        int sw = MeasureText(status, FONT_SIZE_BODY);
        DrawRectangle(WIN_W - sw - 16, 8, sw + 12, 30, (Color){0,0,0,120});
        DrawText(status, WIN_W - sw - 10, 14, FONT_SIZE_BODY, status_col);

        /* ── Chat Panels ─────────────────────────────────────────────── */
        for (int r = 0; r < NUM_ROOMS; r++) {
            draw_chat_panel(r, r_chat[r]);
            DrawRectangleLinesEx(r_chat[r], 1.5f, (Color){60,80,140,255});
        }

        /* ── §9.2 Private Messages Panel ─────────────────────────────── */
        draw_pm_panel(r_pm_panel);
        DrawRectangleLinesEx(r_pm_panel, 1.5f, (Color){120,40,160,255});

        /* ── Thread Status Panel ─────────────────────────────────────── */
        draw_thread_panel(r_thread_panel);
        DrawRectangleLinesEx(r_thread_panel, 1.5f, (Color){60,80,140,255});

        /* ── System Monitor Panel ────────────────────────────────────── */
        draw_monitor_panel(r_monitor_panel);
        DrawRectangleLinesEx(r_monitor_panel, 1.5f, (Color){60,80,140,255});

        /* ── Legend bar at bottom (inside monitor or full-width) ─────── */
        int leg_y = WIN_H - 24;
        DrawText("State legend:", PANEL_PAD, leg_y, FONT_SIZE_SMALL,
                 (Color){160,160,160,255});
        DrawRectangle(130, leg_y + 1, 16, 14, STATE_COLOR[THREAD_IDLE]);
        DrawText("IDLE",    150, leg_y, FONT_SIZE_SMALL, LIGHTGRAY);
        DrawRectangle(195, leg_y + 1, 16, 14, STATE_COLOR[THREAD_ACTIVE]);
        DrawText("ACTIVE",  215, leg_y, FONT_SIZE_SMALL, LIGHTGRAY);
        DrawRectangle(275, leg_y + 1, 16, 14, STATE_COLOR[THREAD_WAITING]);
        DrawText("WAITING", 295, leg_y, FONT_SIZE_SMALL, LIGHTGRAY);

        EndDrawing();
    }

    /* Export log on window close */
    logger_export();
    CloseWindow();
}