/* ============================================================
 *  gaming_whale.c — five aquatic environments
 *
 *  You are a whale.  Travel between 5 habitats by pressing
 *  1-5.  Each habitat has its own water colour, currents,
 *  vegetation, fish, and hazards.
 *
 *   1  LAKE    calm, lily pads, slow perch
 *   2  RIVER   leftward current, driftwood, salmon
 *   3  STREAM  fast rightward flow, rocks, minnows
 *   4  SEA     gentle swell, kelp, jellyfish
 *   5  OCEAN   deep, big waves, sharks, bioluminescence
 *
 *  build: cc -O2 -std=c11 -o gaming_whale gaming_whale.c -lm
 *  run:   ./gaming_whale
 *
 *  keys:  WASD / arrows  swim
 *         1 2 3 4 5      travel to another environment
 *         Q / Esc        quit
 * ============================================================ */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------ */
#define SCR_W 240
#define SCR_H 70
#define FPS   30
#define FRAME_DT (1.0 / (double)FPS)

#define MAX_FISH 20
#define MAX_HAZ  14
#define MAX_BUB  90
#define MAX_WEED 60
#define MAX_GLOW 90

/* ------------------------------------------------------------ */
/*  colours                                                      */
/* ------------------------------------------------------------ */
enum {
    C_DEF, C_WHALE, C_FISH, C_BUBBLE, C_WATER, C_WEED, C_HUD, C_SAND,
    C_HAZ, C_HAZ2, C_ROCK, C_LILY, C_CORAL, C_GLOW, C_WAVE, C_SURF,
    C_BAD, C_NCOLOR
};

static const char *COL[C_NCOLOR] = {
    "\x1b[0m",          /* 0  default   */
    "\x1b[97m",         /* 1  whale     */
    "\x1b[93m",         /* 2  fish      */
    "\x1b[96m",         /* 3  bubble    */
    "\x1b[34m",         /* 4  water     */
    "\x1b[92m",         /* 5  weed      */
    "\x1b[95m",         /* 6  hud       */
    "\x1b[33m",         /* 7  sand      */
    "\x1b[91m",         /* 8  hazard    */
    "\x1b[35m",         /* 9  hazard 2  */
    "\x1b[90m",         /* 10 rock      */
    "\x1b[32m",         /* 11 lily      */
    "\x1b[38;5;209m",   /* 12 coral     */
    "\x1b[38;5;51m",    /* 13 glow      */
    "\x1b[97m",         /* 14 wave      */
    "\x1b[38;5;39m",    /* 15 surf      */
    "\x1b[31m"          /* 16 bad flash */
};

/* ------------------------------------------------------------ */
/*  terminal plumbing                                            */
/* ------------------------------------------------------------ */
static struct termios g_saved_term;
static int  g_term_saved = 0;
static int  g_alt_screen = 0;
static int  g_running    = 1;

static void term_restore(void)
{
    if (g_alt_screen) {
        const char *s = "\x1b[0m\x1b[?25h\x1b[?1049l";
        if (write(STDOUT_FILENO, s, strlen(s)) < 0) { }
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
    if (fl != -1) fcntl(STDIN_FILENO, F_SETFL, fl | O_NONBLOCK);

    const char *s = "\x1b[?1049h\x1b[?25l\x1b[2J";
    if (write(STDOUT_FILENO, s, strlen(s)) < 0) { }
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

/* ------------------------------------------------------------ */
/*  keyboard                                                     */
/* ------------------------------------------------------------ */
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

/* ------------------------------------------------------------ */
/*  drawing surface                                              */
/* ------------------------------------------------------------ */
static int TW = 80, TH = 24;

static char          gch[SCR_H][SCR_W];
static unsigned char gco[SCR_H][SCR_W];

static char          wch[SCR_H][SCR_W];
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
        if (*s != ' ') px(x, y, *s, col);
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
    static char obuf[1 << 19];
    size_t n = 0;
    int cur = -1;
    for (int y = 0; y < TH; ++y) {
        char pos[24];
        int m = snprintf(pos, sizeof pos, "\x1b[%d;1H", y + 1);
        if (m > 0) { memcpy(obuf + n, pos, (size_t)m); n += (size_t)m; }
        int last = (y == TH - 1) ? TW - 1 : TW;
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
    if (write(STDOUT_FILENO, obuf, n) < 0) { }
}

static void get_term_size(void)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 &&
        ws.ws_col > 0 && ws.ws_row > 0) {
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

/* ============================================================
 *  environments
 * ============================================================ */
typedef enum {
    ENV_LAKE, ENV_RIVER, ENV_STREAM, ENV_SEA, ENV_OCEAN, ENV_COUNT
} EnvId;

typedef struct {
    const char   *name;
    const char   *blurb;
    unsigned char col_water;   /* suspended particles         */
    unsigned char col_weed;    /* vegetation                  */
    unsigned char col_bot;     /* sea floor                   */
    unsigned char col_surf;    /* surface waves               */
    unsigned char col_glow;    /* bioluminescence / sparkle   */
    float current_x;           /* horizontal drift (cells/fr) */
    float current_y;
    float fish_smin, fish_smax;
    float fish_spawn;          /* seconds between spawns      */
    float haz_spawn;
    int   fish_max;
    int   haz_max;
    int   wave_amp;            /* surface wave amplitude      */
    const char *bottom;        /* bottom row decoration       */
    int   points;              /* points per fish here        */
} Env;

static const Env ENVS[ENV_COUNT] = {
    /* ---- LAKE ---- calm, no current, lily pads, slow perch ---- */
    { "LAKE", "still fresh water",
      C_WATER, C_WEED, C_SAND, C_SURF, C_GLOW,
      0.00f, 0.00f,
      1.2f, 2.2f, 1.10f, 3.5f,
      8, 3, 0, "=", 10 },

    /* ---- RIVER ---- leftward current, logs, salmon ---- */
    { "RIVER", "flowing downstream",
      C_SAND, C_WEED, C_SAND, C_SURF, C_GLOW,
      -0.55f, 0.00f,
      2.5f, 4.0f, 0.90f, 3.0f,
      10, 3, 1, "~", 15 },

    /* ---- STREAM ---- fast rightward flow, rocks, minnows ---- */
    { "STREAM", "rushing shallow water",
      C_GLOW, C_WEED, C_ROCK, C_WAVE, C_GLOW,
      0.95f, 0.00f,
      3.5f, 5.5f, 0.65f, 2.2f,
      12, 4, 2, "/\\", 20 },

    /* ---- SEA ---- gentle swell, kelp, jellyfish ---- */
    { "SEA", "open coastal water",
      C_WATER, C_WEED, C_SAND, C_SURF, C_CORAL,
      0.25f, 0.00f,
      1.8f, 3.2f, 0.85f, 3.0f,
      12, 4, 2, "=", 25 },

    /* ---- OCEAN ---- deep, strong current, sharks ---- */
    { "OCEAN", "the deep blue",
      C_WATER, C_GLOW, C_ROCK, C_SURF, C_GLOW,
      -0.40f, 0.00f,
      2.2f, 4.5f, 0.80f, 2.8f,
      14, 5, 3, "___", 30 },
};

/* fish art per environment: [0]=swimming right, [1]=swimming left */
static const char *FISH_ART[ENV_COUNT][2] = {
    { "><>",   "<><"   },   /* LAKE   perch        */
    { ">=>",   "<=<"   },   /* RIVER  salmon       */
    { "><>",   "<><"   },   /* STREAM minnow       */
    { "><>>",  "<<><"  },   /* SEA    mackerel     */
    { ">===>", "<===<" },   /* OCEAN  tuna         */
};

/* ============================================================
 *  static scenery (water specks, weeds, glows)
 * ============================================================ */
static int g_weed_x[MAX_WEED];
static int g_weed_h[MAX_WEED];
static int g_weed_n = 0;

typedef struct { float x, y, phase; } Glow;
static Glow g_glow[MAX_GLOW];
static int  g_glow_n = 0;

static EnvId g_env = ENV_LAKE;

static void build_water(const Env *E)
{
    for (int y = 2; y < TH - 1; ++y) {
        for (int x = 0; x < TW; ++x) {
            int r = rand() % 100;
            if (r < 4)       { wch[y][x] = '.';  wco[y][x] = E->col_water; }
            else if (r < 6)  { wch[y][x] = '`';  wco[y][x] = E->col_water; }
            else if (r < 7)  { wch[y][x] = '\''; wco[y][x] = E->col_water; }
            else             { wch[y][x] = ' ';  wco[y][x] = C_DEF;        }
        }
    }
}

static void build_weed(const Env *E, EnvId env)
{
    (void)E;
    g_weed_n = 0;
    int count = TW / 7;
    if (count > MAX_WEED) count = MAX_WEED;
    for (int i = 0; i < count; ++i) {
        g_weed_x[g_weed_n] = rand() % (TW > 0 ? TW : 1);
        int h;
        switch (env) {
        case ENV_LAKE:   h = 3 + rand() % 6; break;   /* tall reeds   */
        case ENV_RIVER:  h = 1 + rand() % 3; break;   /* sparse       */
        case ENV_STREAM: h = 1 + rand() % 2; break;   /* very short   */
        case ENV_SEA:    h = 4 + rand() % 7; break;   /* tall kelp    */
        case ENV_OCEAN:  h = 5 + rand() % 8; break;   /* giant kelp   */
        default:         h = 2 + rand() % 4; break;
        }
        g_weed_h[g_weed_n] = h;
        ++g_weed_n;
    }
}

static void build_glow(const Env *E, EnvId env)
{
    (void)E;
    g_glow_n = 0;
    if (env != ENV_OCEAN && env != ENV_SEA) return;
    int count = (env == ENV_OCEAN) ? 60 : 25;
    if (count > MAX_GLOW) count = MAX_GLOW;
    for (int i = 0; i < count; ++i) {
        g_glow[i].x     = (float)(rand() % (TW > 0 ? TW : 1));
        g_glow[i].y     = (float)(2 + rand() % (TH > 4 ? TH - 4 : 1));
        g_glow[i].phase = (float)(rand() % 628) / 100.0f;
    }
    g_glow_n = count;
}

static void build_scenery(void)
{
    const Env *E = &ENVS[g_env];
    build_water(E);
    build_weed(E, g_env);
    build_glow(E, g_env);
}

/* ============================================================
 *  bubbles
 * ============================================================ */
static float g_bx[MAX_BUB], g_by[MAX_BUB], g_bvy[MAX_BUB];
static char  g_bc[MAX_BUB];

static void bubbles_init(void)
{
    for (int i = 0; i < MAX_BUB; ++i) {
        g_bx[i]  = (float)(rand() % (TW > 0 ? TW : 1));
        g_by[i]  = (float)(2 + rand() % (TH > 3 ? TH - 3 : 1));
        g_bvy[i] = 1.5f + (float)(rand() % 250) / 100.0f;
        g_bc[i]  = (rand() & 1) ? 'o' : '.';
    }
}

static void bubbles_update(double dt)
{
    for (int i = 0; i < MAX_BUB; ++i) {
        g_by[i] -= g_bvy[i] * (float)dt;
        if (g_by[i] < 2.0f) {
            g_by[i] = (float)(TH - 1);
            g_bx[i] = (float)(rand() % (TW > 0 ? TW : 1));
        }
    }
}

/* ============================================================
 *  fish
 * ============================================================ */
typedef struct { float x, y, vx; int alive; } Fish;
static Fish g_fish[MAX_FISH];

static void fish_spawn(const Env *E)
{
    for (int i = 0; i < MAX_FISH; ++i) {
        if (g_fish[i].alive) continue;
        int from_left = rand() & 1;
        float sp = E->fish_smin +
                   (float)(rand() % 100) / 100.0f * (E->fish_smax - E->fish_smin);

        g_fish[i].alive = 1;
        int span = TH - 9;
        if (span < 1) span = 1;
        g_fish[i].y = (float)(3 + rand() % span);
        if (from_left) { g_fish[i].x = -5.0f;      g_fish[i].vx =  sp; }
        else           { g_fish[i].x = (float)TW;  g_fish[i].vx = -sp; }
        return;
    }
}

/* ============================================================
 *  hazards
 * ============================================================ */
typedef enum {
    HZ_NONE, HZ_LILY, HZ_LOG, HZ_ROCK, HZ_JELLY, HZ_SHARK
} HazKind;

typedef struct {
    float x, y, vx, vy;
    int w, h;
    int alive;
    float phase;
    HazKind kind;
} Hazard;

static Hazard g_haz[MAX_HAZ];

static HazKind env_hazard_kind(EnvId e)
{
    switch (e) {
    case ENV_LAKE:   return HZ_LILY;
    case ENV_RIVER:  return HZ_LOG;
    case ENV_STREAM: return HZ_ROCK;
    case ENV_SEA:    return HZ_JELLY;
    case ENV_OCEAN:  return HZ_SHARK;
    default:         return HZ_NONE;
    }
}

static void haz_spawn(const Env *E)
{
    (void)E;
    for (int i = 0; i < MAX_HAZ; ++i) {
        if (g_haz[i].alive) continue;
        Hazard *h = &g_haz[i];
        h->alive = 1;
        h->kind  = env_hazard_kind(g_env);
        h->phase = (float)(rand() % 628) / 100.0f;

        switch (h->kind) {
        case HZ_LILY:
            h->w = 4; h->h = 1;
            h->x = (float)(rand() % (TW > 8 ? TW - 8 : 1));
            h->y = (float)(2 + rand() % 2);
            h->vx = 0.0f; h->vy = 0.0f;
            break;

        case HZ_LOG:
            h->w = 6; h->h = 1;
            h->y = (float)(TH - 5 - rand() % 4);
            if (rand() & 1) { h->x = -7.0f;      h->vx =  1.1f; }
            else            { h->x = (float)TW;  h->vx = -1.1f; }
            h->vy = 0.0f;
            break;

        case HZ_ROCK:
            h->w = 3; h->h = 2;
            h->x = (float)(rand() % (TW > 6 ? TW - 6 : 1) + 2);
            h->y = (float)(TH - 4 - rand() % 2);
            h->vx = 0.0f; h->vy = 0.0f;
            break;

        case HZ_JELLY:
            h->w = 3; h->h = 3;
            h->x = (float)(rand() % (TW > 6 ? TW - 6 : 1) + 1);
            h->y = 4.0f + (float)(rand() % (TH > 10 ? TH - 10 : 1));
            h->vx = 0.0f;
            h->vy = (rand() & 1) ? -0.35f : 0.35f;
            break;

        case HZ_SHARK:
            h->w = 6; h->h = 1;
            h->y = 3.0f + (float)(rand() % (TH > 8 ? TH - 8 : 1));
            if (rand() & 1) {
                h->x = -7.0f;
                h->vx =  3.5f + (float)(rand() % 150) / 100.0f;
            } else {
                h->x = (float)TW;
                h->vx = -3.5f - (float)(rand() % 150) / 100.0f;
            }
            h->vy = 0.0f;
            break;

        default:
            h->alive = 0;
            break;
        }
        return;
    }
}

static void haz_update(double dt)
{
    for (int i = 0; i < MAX_HAZ; ++i) {
        Hazard *h = &g_haz[i];
        if (!h->alive) continue;
        h->phase += (float)dt * 2.5f;
        h->x += h->vx * (float)dt * 4.0f;
        h->y += h->vy * (float)dt;

        if (h->kind == HZ_JELLY) {
            if (h->y < 2.0f)            { h->y = 2.0f;             h->vy = -h->vy; }
            if (h->y > (float)(TH - 4)) { h->y = (float)(TH - 4);  h->vy = -h->vy; }
        }
        if ((h->kind == HZ_LOG || h->kind == HZ_SHARK) &&
            (h->x < -9.0f || h->x > (float)TW + 2.0f)) {
            h->alive = 0;
        }
    }
}

static void draw_hazard(const Hazard *h)
{
    int x = (int)h->x, y = (int)h->y;
    switch (h->kind) {
    case HZ_LILY:
        text(x, y, "(@@)", C_LILY);
        break;
    case HZ_LOG:
        text(x, y, "======", C_SAND);
        break;
    case HZ_ROCK:
        text(x, y,     ".@.", C_ROCK);
        text(x, y + 1, "@@@", C_ROCK);
        break;
    case HZ_JELLY:
        text(x, y,     " _ ", C_HAZ2);
        text(x, y + 1, "(o)", C_HAZ2);
        text(x, y + 2, " ' ", C_HAZ2);
        break;
    case HZ_SHARK:
        if (h->vx > 0.0f) text(x, y, ">>=>",  C_HAZ);
        else              text(x, y, "<=<<",  C_HAZ);
        break;
    default: break;
    }
}

/* ============================================================
 *  whale + game state
 * ============================================================ */
static const char *WHALE_ART[5] = {
    "      .",
    "     \":\"",
    "   ___:____     |\"\\/\"|",
    " ,'        `.    \\  /",
    " |  O        \\___/  |"
};
#define WHALE_ROWS 5
#define WHALE_COLS 22

static float g_wx = 10.0f, g_wy = 10.0f;
static float g_vx = 0.0f,  g_vy = 0.0f;

static int   g_score   = 0;
static int   g_eaten   = 0;
static int   g_health  = 100;
static float g_iframes = 0.0f;   /* invulnerability seconds  */
static float g_flash   = 0.0f;   /* red damage flash seconds */

static float g_banner_t = 0.0f;  /* environment banner timer */

static void draw_whale(int ox, int oy, unsigned char col)
{
    for (int r = 0; r < WHALE_ROWS; ++r) {
        const char *s = WHALE_ART[r];
        for (int c = 0; s[c]; ++c)
            if (s[c] != ' ')
                px(ox + c, oy + r, s[c], col);
    }
}

/* ============================================================
 *  input
 * ============================================================ */
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

        case '1': case '2': case '3': case '4': case '5': {
            EnvId want = (EnvId)(k - '1');
            if (want != g_env) {
                g_env = want;
                build_scenery();
                bubbles_init();
                /* clear creatures, let them respawn */
                memset(g_fish, 0, sizeof g_fish);
                memset(g_haz,  0, sizeof g_haz);
                g_banner_t = 2.0f;
            }
            break;
        }
        default:
            break;
        }
    }
}

/* ============================================================
 *  update
 * ============================================================ */
static void update(double dt, double *fish_timer, double *haz_timer)
{
    const Env *E = &ENVS[g_env];

    /* ---- whale physics ---- */
    g_vx *= 0.86f;
    g_vy *= 0.86f;

    if (g_vx >  2.4f) g_vx =  2.4f;
    if (g_vx < -2.4f) g_vx = -2.4f;
    if (g_vy >  1.5f) g_vy =  1.5f;
    if (g_vy < -1.5f) g_vy = -1.5f;

    /* current pushes the whale */
    g_wx += g_vx + E->current_x;
    g_wy += g_vy + E->current_y;

    float maxx = (float)(TW - WHALE_COLS - 1);
    float maxy = (float)(TH - WHALE_ROWS - 1);
    if (maxx < 1.0f) maxx = 1.0f;
    if (maxy < 2.0f) maxy = 2.0f;

    if (g_wx < 1.0f) { g_wx = 1.0f; g_vx = 0.0f; }
    if (g_wx > maxx) { g_wx = maxx; g_vx = 0.0f; }
    if (g_wy < 2.0f) { g_wy = 2.0f; g_vy = 0.0f; }
    if (g_wy > maxy) { g_wy = maxy; g_vy = 0.0f; }

    /* ---- timers ---- */
    if (g_iframes > 0.0f) g_iframes -= (float)dt;
    if (g_flash   > 0.0f) g_flash   -= (float)dt;
    if (g_banner_t > 0.0f) g_banner_t -= (float)dt;

    /* slow regeneration */
    if (g_health < 100 && g_iframes <= 0.0f) {
        static float regen_acc = 0.0f;
        regen_acc += (float)dt;
        if (regen_acc >= 1.0f) {
            regen_acc -= 1.0f;
            g_health += 1;
            if (g_health > 100) g_health = 100;
        }
    }

    /* ---- fish spawning ---- */
    *fish_timer -= dt;
    if (*fish_timer <= 0.0) {
        *fish_timer = E->fish_spawn * (0.7 + (double)(rand() % 60) / 100.0);
        fish_spawn(E);
    }

    /* ---- hazard spawning ---- */
    *haz_timer -= dt;
    if (*haz_timer <= 0.0) {
        *haz_timer = E->haz_spawn * (0.8 + (double)(rand() % 60) / 100.0);
        haz_spawn(E);
    }

    /* ---- fish update + eat check ---- */
    int bx0 = (int)g_wx + 1, bx1 = (int)g_wx + 20;
    int by0 = (int)g_wy,     by1 = (int)g_wy + 4;

    for (int i = 0; i < MAX_FISH; ++i) {
        if (!g_fish[i].alive) continue;

        g_fish[i].x += g_fish[i].vx * (float)dt;
        if (g_fish[i].x < -8.0f || g_fish[i].x > (float)TW + 4.0f) {
            g_fish[i].alive = 0;
            continue;
        }

        const char *art = FISH_ART[g_env][g_fish[i].vx > 0 ? 0 : 1];
        int flen = (int)strlen(art);
        int fx0 = (int)g_fish[i].x;
        int fx1 = fx0 + flen - 1;
        int fy  = (int)g_fish[i].y;

        if (fx1 >= bx0 && fx0 <= bx1 && fy >= by0 && fy <= by1) {
            g_fish[i].alive = 0;
            g_score += E->points;
            g_eaten += 1;
            if (g_health < 100) g_health += 2;
            if (g_health > 100) g_health = 100;
        }
    }

    /* ---- hazard update + hit check ---- */
    haz_update(dt);

    if (g_iframes <= 0.0f) {
        for (int i = 0; i < MAX_HAZ; ++i) {
            Hazard *h = &g_haz[i];
            if (!h->alive) continue;

            int hx0 = (int)h->x, hx1 = hx0 + h->w - 1;
            int hy0 = (int)h->y, hy1 = hy0 + h->h - 1;

            if (hx1 >= bx0 && hx0 <= bx1 && hy1 >= by0 && hy0 <= by1) {
                g_health -= 15;
                g_iframes = 1.5f;
                g_flash   = 0.35f;
                if (g_health <= 0) {
                    g_health = 0;
                    g_running = 0;
                }
                break;
            }
        }
    }

    bubbles_update(dt);
}

/* ============================================================
 *  render
 * ============================================================ */
static void render(double t)
{
    const Env *E = &ENVS[g_env];
    frame_clear();

    /* --- water specks --- */
    for (int y = 2; y < TH - 1; ++y)
        for (int x = 0; x < TW; ++x)
            if (wch[y][x] != ' ')
                px(x, y, wch[y][x], wco[y][x]);

    /* --- surface waves --- */
    for (int x = 0; x < TW; ++x) {
        int wy = 1;
        if (E->wave_amp > 0) {
            float s = sinf((float)x * 0.18f + (float)t * 2.4f);
            wy = 1 + (int)(s * (float)E->wave_amp * 0.5f);
        }
        if (wy < 1) wy = 1;
        px(x, wy, '~', E->col_surf);
    }

    /* --- sea floor --- */
    int botlen = (int)strlen(E->bottom);
    for (int x = 0; x < TW; ++x)
        px(x, TH - 1, E->bottom[x % botlen], E->col_bot);

    /* --- seaweed / kelp / reeds --- */
    int sway = (int)(t * 4.0);
    for (int i = 0; i < g_weed_n; ++i) {
        int x = g_weed_x[i];
        for (int k = 0; k < g_weed_h[i]; ++k) {
            int y = TH - 2 - k;
            if (y < 2) break;
            int ph = sway + i * 2 + k;
            px(x, y, ")(|"[ph % 3], E->col_weed);
        }
    }

    /* --- bioluminescent sparkles (sea / ocean) --- */
    for (int i = 0; i < g_glow_n; ++i) {
        float f = sinf(g_glow[i].phase + (float)t * 1.7f);
        if (f > 0.2f) {
            char c = (f > 0.8f) ? '*' : '.';
            px((int)g_glow[i].x, (int)g_glow[i].y, c, E->col_glow);
        }
    }

    /* --- bubbles --- */
    for (int i = 0; i < MAX_BUB; ++i)
        px((int)g_bx[i], (int)g_by[i], g_bc[i], C_BUBBLE);

    /* --- hazards --- */
    for (int i = 0; i < MAX_HAZ; ++i)
        if (g_haz[i].alive) draw_hazard(&g_haz[i]);

    /* --- fish --- */
    for (int i = 0; i < MAX_FISH; ++i) {
        if (!g_fish[i].alive) continue;
        const char *art = FISH_ART[g_env][g_fish[i].vx > 0 ? 0 : 1];
        text((int)g_fish[i].x, (int)g_fish[i].y, art, C_FISH);
    }

    /* --- whale (blinks while invulnerable) --- */
    int blink = (g_iframes > 0.0f) && (((int)(g_iframes * 18.0f) & 1) != 0);
    unsigned char wcol = (g_flash > 0.0f) ? C_BAD : C_WHALE;
    if (!blink)
        draw_whale((int)g_wx, (int)g_wy, wcol);

    /* --- HUD line --- */
    char hpbar[16];
    int hpfull = g_health / 10;
    for (int i = 0; i < 10; ++i)
        hpbar[i] = (i < hpfull) ? '#' : '-';
    hpbar[10] = 0;

    char hud[256];
    const char *flow = "";
    if      (E->current_x < -0.3f) flow = "  current <<<";
    else if (E->current_x >  0.3f) flow = "  current >>>";

    snprintf(hud, sizeof hud,
             " WHALE | %-6s | score %-5d | hp[%s] | fish %-3d |%s  | 1-5 travel  Q quit ",
             E->name, g_score, hpbar, g_eaten, flow);
    text(0, 0, hud, C_HUD);

    /* --- environment banner on transition --- */
    if (g_banner_t > 0.0f && TW > 20) {
        char buf[160];
        snprintf(buf, sizeof buf, " ~~  %s  —  %s  ~~ ",
                 E->name, E->blurb);
        int len = (int)strlen(buf);
        int x = (TW - len) / 2;
        if (x < 0) x = 0;
        int y = TH / 2 - 1;
        if (y < 3) y = 3;
        for (int i = 0; i < len; ++i) {
            px(x + i, y - 1, '-', C_HUD);
            px(x + i, y + 1, '-', C_HUD);
        }
        text(x, y, buf, C_HUD);
    }

    flush_frame();
}

/* ============================================================
 *  main
 * ============================================================ */
int main(void)
{
    atexit(term_restore);
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    term_setup();
    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    get_term_size();
    g_env = ENV_LAKE;
    build_scenery();
    bubbles_init();

    g_wx = (float)(TW / 4);
    g_wy = (float)(TH / 2);
    g_health = 100;

    double t = 0.0;
    double fish_timer = 0.4;
    double haz_timer  = 2.0;
    int last_w = TW, last_h = TH;

    while (g_running) {
        double frame_start = now_sec();

        get_term_size();
        if (TW != last_w || TH != last_h) {
            last_w = TW; last_h = TH;
            build_scenery();
            bubbles_init();
            if (write(STDOUT_FILENO, "\x1b[2J", 4) < 0) { }
        }

        handle_input();
        if (!g_running) break;

        update(FRAME_DT, &fish_timer, &haz_timer);
        render(t);
        t += FRAME_DT;

        sleep_sec(FRAME_DT - (now_sec() - frame_start));
    }

    term_restore();
    printf("\n  Whale's journey ended.\n"
           "  score : %d\n"
           "  fish  : %d\n"
           "  last habitat: %s\n"
           "  time  : %.1fs\n\n",
           g_score, g_eaten, ENVS[g_env].name, t);
    return 0;
}