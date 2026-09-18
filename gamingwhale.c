/* ============================================================
 *  gaming_whale.c  —  you are a whale. eat the fish.
 *
 *  build:  cc -O2 -std=c11 -o gaming_whale gaming_whale.c
 *  run:    ./gaming_whale
 *
 *  controls:  WASD / arrow keys  — swim
 *             Q / Esc            — quit
 * ============================================================ */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define SCR_W 240
#define SCR_H 70
#define FPS   30
#define FRAME_DT (1.0 / (double)FPS)

/* ------------------------------------------------------------------ */
/*  palette                                                            */
/* ------------------------------------------------------------------ */
enum { C_DEF, C_WHALE, C_FISH, C_BUBBLE, C_WATER, C_WEED, C_HUD, C_SAND, C_NCOLOR };

static const char *COL[C_NCOLOR] = {
    "\x1b[0m",   /* 0 default  */
    "\x1b[97m",  /* 1 whale    */
    "\x1b[93m",  /* 2 fish     */
    "\x1b[96m",  /* 3 bubbles  */
    "\x1b[34m",  /* 4 water    */
    "\x1b[92m",  /* 5 seaweed  */
    "\x1b[95m",  /* 6 hud      */
    "\x1b[33m"   /* 7 sand     */
};

/* ------------------------------------------------------------------ */
/*  terminal plumbing                                                  */
/* ------------------------------------------------------------------ */
static struct termios g_saved_term;
static int  g_term_saved  = 0;
static int  g_alt_screen  = 0;
static int  g_running     = 1;

static void term_restore(void)
{
    if (g_alt_screen) {
        const char *s = "\x1b[0m\x1b[?25h\x1b[?1049l";
        if (write(STDOUT_FILENO, s, strlen(s)) < 0) { /* ignore */ }
        g_alt_screen = 0;
    }
    if (g_term_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_term);
        g_term_saved = 0;
    }
}

static void on_signal(int sig)
{
    (void)sig;
    term_restore();
    _exit(0);
}

static void term_setup(void)
{
    if (tcgetattr(STDIN_FILENO, &g_saved_term) == 0) {
        struct termios t = g_saved_term;
        t.c_lflag &= (tcflag_t)~(ICANON | ECHO);
        t.c_iflag &= (tcflag_t)~(IXON | ICRNL | INLCR);
        t.c_cc[VMIN]  = 0;
        t.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &t) == 0)
            g_term_saved = 1;
    }

    int fl = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (fl != -1)
        fcntl(STDIN_FILENO, F_SETFL, fl | O_NONBLOCK);

    /* alternate screen + hide cursor + clear */
    const char *s = "\x1b[?1049h\x1b[?25l\x1b[2J";
    if (write(STDOUT_FILENO, s, strlen(s)) < 0) { /* ignore */ }
    g_alt_screen = 1;
}

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void sleep_sec(double s)
{
    if (s <= 0.0) return;
    struct timespec ts;
    ts.tv_sec  = (time_t)s;
    ts.tv_nsec = (long)((s - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
}

/* ------------------------------------------------------------------ */
/*  keyboard                                                           */
/* ------------------------------------------------------------------ */
enum { K_UP = 1000, K_DOWN, K_LEFT, K_RIGHT };

static int read_key(void)
{
    unsigned char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n <= 0) return -1;
    if (c != 0x1b) return (int)c;

    unsigned char s1, s2;
    if (read(STDIN_FILENO, &s1, 1) != 1) return 27;
    if (read(STDIN_FILENO, &s2, 1) != 1) return 27;

    if (s1 == '[') {
        switch (s2) {
        case 'A': return K_UP;
        case 'B': return K_DOWN;
        case 'C': return K_RIGHT;
        case 'D': return K_LEFT;
        default:  break;
        }
    }
    return 27;
}

/* ------------------------------------------------------------------ */
/*  drawing surface                                                    */
/* ------------------------------------------------------------------ */
static int TW = 80, TH = 24;

static char          gch[SCR_H][SCR_W];
static unsigned char gco[SCR_H][SCR_W];

static char          wch[SCR_H][SCR_W];   /* static water texture */
static unsigned char wco[SCR_H][SCR_W];

static void px(int x, int y, char c, unsigned char col)
{
    if (x < 0 || y < 0 || x >= TW || y >= TH) return;
    gch[y][x] = c;
    gco[y][x] = col;
}

static void text(int x, int y, const char *s, unsigned char col)
{
    for (; *s; ++s, ++x)
        if (*s != ' ')
            px(x, y, *s, col);
}

static void frame_clear(void)
{
    for (int y = 0; y < TH; ++y) {
        memset(gch[y], ' ', (size_t)TW);
        memset(gco[y], C_DEF, (size_t)TW);
    }
}

static void flush_frame(void)
{
    static char obuf[1 << 18];
    size_t n = 0;
    int cur = -1;

    for (int y = 0; y < TH; ++y) {
        char pos[24];
        int m = snprintf(pos, sizeof pos, "\x1b[%d;1H", y + 1);
        if (m > 0) { memcpy(obuf + n, pos, (size_t)m); n += (size_t)m; }

        int last = (y == TH - 1) ? TW - 1 : TW;   /* avoid scroll at bottom-right */
        for (int x = 0; x < last; ++x) {
            unsigned char c = gco[y][x];
            if ((int)c != cur) {
                const char *e = COL[c];
                size_t l = strlen(e);
                memcpy(obuf + n, e, l);
                n += l;
                cur = (int)c;
            }
            obuf[n++] = gch[y][x];
        }
    }
    memcpy(obuf + n, "\x1b[0m", 4);
    n += 4;

    if (write(STDOUT_FILENO, obuf, n) < 0) { /* ignore */ }
}

static void get_term_size(void)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        TW = ws.ws_col;
        TH = ws.ws_row;
    } else {
        TW = 80;
        TH = 24;
    }
    if (TW > SCR_W) TW = SCR_W;
    if (TH > SCR_H) TH = SCR_H;
    if (TW < 1) TW = 1;
    if (TH < 1) TH = 1;
}

/* ------------------------------------------------------------------ */
/*  scenery                                                            */
/* ------------------------------------------------------------------ */
static void build_water(void)
{
    for (int y = 0; y < TH; ++y) {
        for (int x = 0; x < TW; ++x) {
            int r = rand() % 100;
            if (r < 4)       { wch[y][x] = '.';  wco[y][x] = C_WATER; }
            else if (r < 6)  { wch[y][x] = '`';  wco[y][x] = C_WATER; }
            else if (r < 8)  { wch[y][x] = '\''; wco[y][x] = C_WATER; }
            else             { wch[y][x] = ' ';  wco[y][x] = C_DEF;   }
        }
    }
}

#define MAX_WEED 48
static int g_weed_x[MAX_WEED];
static int g_weed_h[MAX_WEED];
static int g_weed_n = 0;

static void build_weed(void)
{
    g_weed_n = 0;
    int count = TW / 7;
    if (count > MAX_WEED) count = MAX_WEED;
    for (int i = 0; i < count; ++i) {
        g_weed_x[g_weed_n] = rand() % TW;
        g_weed_h[g_weed_n] = 2 + rand() % 5;
        ++g_weed_n;
    }
}

/* ------------------------------------------------------------------ */
/*  bubbles                                                            */
/* ------------------------------------------------------------------ */
#define MAX_BUB 70
static float g_bx[MAX_BUB], g_by[MAX_BUB], g_bvy[MAX_BUB];
static char  g_bc[MAX_BUB];

static void bubbles_init(void)
{
    for (int i = 0; i < MAX_BUB; ++i) {
        g_bx[i]  = (float)(rand() % TW);
        g_by[i]  = (float)(1 + rand() % (TH > 1 ? TH - 1 : 1));
        g_bvy[i] = 1.5f + (float)(rand() % 250) / 100.0f;
        g_bc[i]  = (rand() & 1) ? 'o' : '.';
    }
}

static void bubbles_update(double dt)
{
    for (int i = 0; i < MAX_BUB; ++i) {
        g_by[i] -= g_bvy[i] * (float)dt;
        if (g_by[i] < 1.0f) {
            g_by[i] = (float)(TH - 1);
            g_bx[i] = (float)(rand() % TW);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  fish                                                               */
/* ------------------------------------------------------------------ */
#define MAX_FISH 14
typedef struct { float x, y, vx; int alive; } Fish;
static Fish g_fish[MAX_FISH];

static int fish_alive_count(void)
{
    int n = 0;
    for (int i = 0; i < MAX_FISH; ++i) if (g_fish[i].alive) ++n;
    return n;
}

static void fish_spawn(void)
{
    for (int i = 0; i < MAX_FISH; ++i) {
        if (g_fish[i].alive) continue;
        int from_left = rand() & 1;
        float sp = 3.5f + (float)(rand() % 250) / 100.0f;

        g_fish[i].alive = 1;
        int span = TH - 9;
        if (span < 1) span = 1;
        g_fish[i].y = (float)(3 + rand() % span);
        if (from_left) { g_fish[i].x = -3.0f;    g_fish[i].vx =  sp; }
        else           { g_fish[i].x = (float)TW; g_fish[i].vx = -sp; }
        return;
    }
}

/* ------------------------------------------------------------------ */
/*  the whale                                                          */
/* ------------------------------------------------------------------ */
static const char *WHALE[5] = {
    "      .",
    "     \":\"",
    "   ___:____     |\"\\/\"|",
    " ,'        `.    \\  /",
    " |  O        \\___/  |",
    "~^~^~^~^~^~^~^~^~^~^~^~^~"
};
#define WHALE_ROWS 5
#define WHALE_COLS 22

static float g_wx = 10.0f, g_wy = 10.0f;
static float g_vx = 0.0f,  g_vy = 0.0f;

static int g_score = 0;
static int g_eaten = 0;

static void draw_whale(int ox, int oy)
{
    for (int r = 0; r < WHALE_ROWS; ++r) {
        const char *s = WHALE[r];
        for (int c = 0; s[c]; ++c)
            if (s[c] != ' ')
                px(ox + c, oy + r, s[c], C_WHALE);
    }
}

/* ------------------------------------------------------------------ */
/*  input + update                                                     */
/* ------------------------------------------------------------------ */
static void handle_input(void)
{
    int k;
    while ((k = read_key()) != -1) {
        switch (k) {
        case 'q': case 'Q': case 27:
            g_running = 0;
            break;
        case 'a': case 'A': case K_LEFT:  g_vx -= 1.3f; break;
        case 'd': case 'D': case K_RIGHT: g_vx += 1.3f; break;
        case 'w': case 'W': case K_UP:    g_vy -= 1.1f; break;
        case 's': case 'S': case K_DOWN:  g_vy += 1.1f; break;
        default:
            break;
        }
    }
}

static void update(double dt, double *spawn_timer)
{
    /* ---- whale physics ---- */
    g_vx *= 0.86f;
    g_vy *= 0.86f;

    if (g_vx >  2.4f) g_vx =  2.4f;
    if (g_vx < -2.4f) g_vx = -2.4f;
    if (g_vy >  1.5f) g_vy =  1.5f;
    if (g_vy < -1.5f) g_vy = -1.5f;

    g_wx += g_vx;
    g_wy += g_vy;

    float maxx = (float)(TW - WHALE_COLS - 1);
    float maxy = (float)(TH - WHALE_ROWS - 1);
    if (maxx < 1.0f) maxx = 1.0f;
    if (maxy < 2.0f) maxy = 2.0f;

    if (g_wx < 1.0f)  { g_wx = 1.0f;  g_vx = 0.0f; }
    if (g_wx > maxx)  { g_wx = maxx;  g_vx = 0.0f; }
    if (g_wy < 2.0f)  { g_wy = 2.0f;  g_vy = 0.0f; }
    if (g_wy > maxy)  { g_wy = maxy;  g_vy = 0.0f; }

    /* ---- fish ---- */
    *spawn_timer -= dt;
    if (*spawn_timer <= 0.0) {
        *spawn_timer = 0.45 + (double)(rand() % 90) / 100.0;
        fish_spawn();
    }

    int bx0 = (int)g_wx + 1, bx1 = (int)g_wx + 20;
    int by0 = (int)g_wy + 2, by1 = (int)g_wy + 4;

    for (int i = 0; i < MAX_FISH; ++i) {
        if (!g_fish[i].alive) continue;

        g_fish[i].x += g_fish[i].vx * (float)dt;
        if (g_fish[i].x < -4.0f || g_fish[i].x > (float)TW + 1.0f) {
            g_fish[i].alive = 0;
            continue;
        }

        int fx = (int)g_fish[i].x + 1;   /* centre of "><>" */
        int fy = (int)g_fish[i].y;
        if (fx >= bx0 && fx <= bx1 && fy >= by0 && fy <= by1) {
            g_fish[i].alive = 0;
            g_score += 10;
            g_eaten += 1;
        }
    }

    bubbles_update(dt);
}

/* ------------------------------------------------------------------ */
/*  render                                                             */
/* ------------------------------------------------------------------ */
static void render(double t)
{
    frame_clear();

    /* static water specks */
    for (int y = 1; y < TH - 1; ++y)
        for (int x = 0; x < TW; ++x)
            if (wch[y][x] != ' ')
                px(x, y, wch[y][x], wco[y][x]);

    /* sea floor */
    for (int x = 0; x < TW; ++x)
        px(x, TH - 1, '=', C_SAND);

    /* seaweed */
    int sway = (int)(t * 4.0);
    for (int i = 0; i < g_weed_n; ++i) {
        int x = g_weed_x[i];
        for (int k = 0; k < g_weed_h[i]; ++k) {
            int y = TH - 2 - k;
            if (y < 1) break;
            int ph = sway + i * 2 + k;
            px(x, y, ")(|"[ph % 3], C_WEED);
        }
    }

    /* bubbles */
    for (int i = 0; i < MAX_BUB; ++i)
        px((int)g_bx[i], (int)g_by[i], g_bc[i], C_BUBBLE);

    /* fish */
    for (int i = 0; i < MAX_FISH; ++i) {
        if (!g_fish[i].alive) continue;
        text((int)g_fish[i].x, (int)g_fish[i].y,
             g_fish[i].vx > 0.0f ? "><>" : "<><", C_FISH);
    }

    /* whale */
    draw_whale((int)g_wx, (int)g_wy);

    /* hud */
    char hud[160];
    snprintf(hud, sizeof hud,
             " GAMING WHALE   score %-6d  fish eaten %-4d  time %3ds   "
             "[WASD/arrows] swim   [Q] quit ",
             g_score, g_eaten, (int)t);
    text(0, 0, hud, C_HUD);

    flush_frame();
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */
int main(void)
{
    atexit(term_restore);
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    term_setup();
    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    get_term_size();
    build_water();
    build_weed();
    bubbles_init();

    g_wx = (float)(TW / 4);
    g_wy = (float)(TH / 2);

    double t = 0.0;
    double spawn_timer = 0.5;
    int last_w = TW, last_h = TH;

    while (g_running) {
        double frame_start = now_sec();

        /* handle a resize */
        get_term_size();
        if (TW != last_w || TH != last_h) {
            last_w = TW; last_h = TH;
            build_water();
            build_weed();
            bubbles_init();
            if (write(STDOUT_FILENO, "\x1b[2J", 4) < 0) { /* ignore */ }
        }

        handle_input();
        if (!g_running) break;

        update(FRAME_DT, &spawn_timer);
        render(t);
        t += FRAME_DT;

        sleep_sec(FRAME_DT - (now_sec() - frame_start));
    }

    term_restore();
    printf("\n  Final score: %d   (fish eaten: %d)\n\n", g_score, g_eaten);
    return 0;
}