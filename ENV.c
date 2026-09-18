typedef enum {
    ENV_LAKE, ENV_RIVER, ENV_STREAM, ENV_SEA, ENV_OCEAN, ENV_COUNT
} EnvId;

typedef struct {
    const char   *name;
    const char   *blurb;
    unsigned char col_water;   /* suspended particles       */
    unsigned char col_weed;    /* vegetation colour         */
    unsigned char col_bot;     /* sea floor colour          */
    unsigned char col_surf;    /* surface waves colour      */
    unsigned char col_glow;    /* bioluminescence colour    */
    float current_x;           /* horizontal drift          */
    float current_y;
    float fish_smin, fish_smax;
    float fish_spawn;          /* seconds between fish      */
    float haz_spawn;           /* seconds between hazards   */
    int   fish_max;
    int   haz_max;
    int   wave_amp;            /* surface wave amplitude    */
    const char *bottom;        /* bottom-row decoration     */
    int   points;              /* score per fish            */
} Env;

static const Env ENVS[ENV_COUNT] = {
/* name      blurb                    water  weed   bot    surf   glow   cur_x   cur_y  fmin fmax fspawn hspawn fm hm wa  bottom  pts */
{ "LAKE",   "still fresh water",      C_WATER,C_WEED,C_SAND,C_SURF,C_GLOW, 0.00f, 0.00f, 1.2f,2.2f,1.10f,3.5f, 8, 3, 0, "=",    10 },
{ "RIVER",  "flowing downstream",     C_SAND, C_WEED,C_SAND,C_SURF,C_GLOW,-0.55f, 0.00f, 2.5f,4.0f,0.90f,3.0f,10, 3, 1, "~",    15 },
{ "STREAM", "rushing shallow water",  C_GLOW, C_WEED,C_ROCK,C_WAVE,C_GLOW, 0.95f, 0.00f, 3.5f,5.5f,0.65f,2.2f,12, 4, 2, "/\\",   20 },
{ "SEA",    "open coastal water",     C_WATER,C_WEED,C_SAND,C_SURF,C_CORAL,0.25f, 0.00f, 1.8f,3.2f,0.85f,3.0f,12, 4, 2, "=",    25 },
{ "OCEAN",  "the deep blue",          C_WATER,C_GLOW,C_ROCK,C_SURF,C_GLOW,-0.40f, 0.00f, 2.2f,4.5f,0.80f,2.8f,14, 5, 3, "___",  30 },
};

static const char *FISH_ART[ENV_COUNT][2] = {
    { "><>",   "<><"   },   /* LAKE   perch        */
    { ">=>",   "<=<"   },   /* RIVER  salmon       */
    { "><>",   "<><"   },   /* STREAM minnow       */
    { "><>>",  "<<><"  },   /* SEA    mackerel     */
    { ">===>", "<===<" },   /* OCEAN  tuna         */
};

typedef enum { HZ_NONE, HZ_LILY, HZ_LOG, HZ_ROCK, HZ_JELLY, HZ_SHARK } HazKind;

static HazKind env_hazard_kind(EnvId e)
{
    switch (e) {
    case ENV_LAKE:   return HZ_LILY;    /* (@@)   */
    case ENV_RIVER:  return HZ_LOG;     /* ====== */
    case ENV_STREAM: return HZ_ROCK;    /* .@. @@@ */
    case ENV_SEA:    return HZ_JELLY;   /* _ (o) ' */
    case ENV_OCEAN:  return HZ_SHARK;   /* >>=> / <=<< */
    default:         return HZ_NONE;
    }
}

static void draw_hazard(const Hazard *h)
{
    int x = (int)h->x, y = (int)h->y;
    switch (h->kind) {
    case HZ_LILY:  text(x, y,   "(@@)",   C_LILY); break;
    case HZ_LOG:   text(x, y,   "======", C_SAND); break;
    case HZ_ROCK:  text(x, y,   ".@.",    C_ROCK);
                   text(x, y+1, "@@@",    C_ROCK); break;
    case HZ_JELLY: text(x, y,   " _ ",    C_HAZ2);
                   text(x, y+1, "(o)",    C_HAZ2);
                   text(x, y+2, " ' ",    C_HAZ2); break;
    case HZ_SHARK: if (h->vx > 0) text(x, y, ">>=>", C_HAZ);
                   else           text(x, y, "<=<<", C_HAZ); break;
    }
}

static void build_weed(const Env *E, EnvId env)
{
    g_weed_n = 0;
    int count = TW / 7;
    for (int i = 0; i < count; ++i) {
        g_weed_x[g_weed_n] = rand() % TW;
        switch (env) {
        case ENV_LAKE:   g_weed_h[g_weed_n] = 3 + rand() % 6; break; /* tall reeds */
        case ENV_RIVER:  g_weed_h[g_weed_n] = 1 + rand() % 3; break; /* sparse     */
        case ENV_STREAM: g_weed_h[g_weed_n] = 1 + rand() % 2; break; /* very short */
        case ENV_SEA:    g_weed_h[g_weed_n] = 4 + rand() % 7; break; /* tall kelp  */
        case ENV_OCEAN:  g_weed_h[g_weed_n] = 5 + rand() % 8; break; /* giant kelp */
        }
        ++g_weed_n;
    }
}

static void build_glow(const Env *E, EnvId env)
{
    g_glow_n = 0;
    if (env != ENV_OCEAN && env != ENV_SEA) return;   /* only these two glow */
    int count = (env == ENV_OCEAN) ? 60 : 25;         /* ocean glows more    */
    for (int i = 0; i < count; ++i) {
        g_glow[i].x     = rand() % TW;
        g_glow[i].y     = 2 + rand() % (TH - 4);
        g_glow[i].phase = (rand() % 628) / 100.0f;
    }
    g_glow_n = count;
}

g_wx += g_vx + E->current_x;   /* E->current_x drifts the whale */
g_wy += g_vy + E->current_y;
if (E->wave_amp > 0) {
    float s = sinf((float)x * 0.18f + (float)t * 2.4f);
    wy = 1 + (int)(s * (float)E->wave_amp * 0.5f);
}
px(x, wy, '~', E->col_surf);