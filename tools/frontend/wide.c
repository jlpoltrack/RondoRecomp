/* See wide.h. */
#include "wide.h"

#include <string.h>

#include "game.h"
#include "wide_ram.h"

#define SUB_PLAYING 0x01
#define BUILDER 0x8000         /* the sprite builder, 72:8000 */
#define BUILDER_BANK 0x72
#define SATB_WORDS 256
#define GROW 2                 /* columns a frame the sides grow by */

/* the builder's first bytes, the same on the Japanese and English discs */
static const uint8_t builder_sig[16] = {0x20, 0x2A, 0x40, 0x23, 0x00, 0x20, 0x21, 0x40,
                                        0x13, 0x00, 0x23, 0x10, 0x20, 0x27, 0x40, 0xA9};

/* Rooms checked with rondo_headless --wide-audit and by eye (docs/runtime.md, "Widescreen"):
 * each area's first rooms, from save states and the route on (tools/wide_audit_all.py). */
static const struct { uint8_t area, room; } rooms[] = {
    {0x10, 0x00}, {0x10, 0x02}, {0x10, 0x07},   /* Stage 1: the town, the dead trees, the red hall */
    {0x0E, 0x00}, {0x0E, 0x01}, {0x0E, 0x02}, {0x0E, 0x0B},   /* Stage 2: gate, halls, behemoth */
    {0x12, 0x00}, {0x12, 0x01}, {0x12, 0x04}, {0x12, 0x09},   /* Stage 2': the bridges */
    {0x02, 0x00}, {0x02, 0x04},                 /* Stage 3: golden pillars, candles */
    {0x0C, 0x00}, {0x0C, 0x01},                 /* Stage 3': the graveyard, the dragon's bones */
    {0x06, 0x00}, {0x06, 0x02}, {0x06, 0x10}, {0x06, 0x12},   /* Stage 4: the halls */
    {0x16, 0x00}, {0x16, 0x01}, {0x16, 0x02},   /* Stage 4': the cave, the ferryman */
    {0x1A, 0x00}, {0x1A, 0x01}, {0x1A, 0x02},   /* Stage 5: the docks (and through $30) */
    {0x30, 0x00}, {0x30, 0x01}, {0x30, 0x02},
    {0x22, 0x0A}, {0x1C, 0x0A},                 /* Stage 7 past the ramparts (whose bats pop in) */
    {0x34, 0x00},                               /* the Final stage: the staircase */
    {0x00, 0x00}, {0x08, 0x00},                 /* boss areas (no stage title): pillared halls */
    {0x04, 0x00}, {0x04, 0x01}, {0x14, 0x00}, {0x14, 0x01},   /* brick halls, the minotaur */
    {0x18, 0x00}, {0x28, 0x00},                 /* blue halls */
    {0x2C, 0x00},                               /* grey-block room with armour statues */
};

int wide_extent(double screen_aspect, double picture_aspect)
{
    if (picture_aspect <= 0 || screen_aspect <= picture_aspect) return 0;
    int e = (int)(256.0 * (screen_aspect / picture_aspect - 1.0) / 2.0);
    return e > WIDE_MAX ? WIDE_MAX : e;
}

static void sides_off(Wide *w)
{
    w->left = w->right = 0;
    w->live = false;
    vdc_set_side(0, 0);
    vdc_drop_side_sprites();
}

void wide_reset(Wide *w) { sides_off(w); }

void wide_set_want(Wide *w, int want)
{
    if (want == w->want) return;
    w->want = want;
    sides_off(w);
}

static bool room_allowed(const Wide *w, const Pce *p)
{
    const uint8_t *r = p->wram;
    if (game_state(p) != RONDO_PLAYING || r[RONDO_SUB] != SUB_PLAYING || p->vdc.fb_w != 256) return false;
    if (w->any_room) return true;
    for (size_t i = 0; i < sizeof rooms / sizeof rooms[0]; i++)
        if (rooms[i].area == r[AREA] && rooms[i].room == r[ROOM]) return true;
    return false;
}

/* ---- sprites: the builder run on a copy of the machine --------------------------------- */

static Pce saved;   /* the machine as it was, put back after each run */

/* Objects fixed to the screen (the HUD, titles, a boss's gauge, generators) stay put. */
static bool moves_with_world(const uint8_t *r, int i)
{
    return i < 0x10 || (r[OBJ_KIND + i] && !(r[OBJ_FLAGS + i] & 1));
}

/* The builder's table with world objects `shift` px over, on `p` (put back by the caller). */
static bool builder_table(Wide *w, Pce *p, int shift, uint16_t *out)
{
    uint8_t *r = p->wram;
    for (int i = 0; i < OBJ_SLOTS; i++) {
        /* screen-fixed objects go far off, where the builder drops their pieces */
        int x = moves_with_world(r, i) ? (int16_t)(r[OBJ_X_HI + i] << 8 | r[OBJ_X + i]) + shift : 0x400;
        r[OBJ_X_HI + i] = (uint8_t)(x >> 8);
        r[OBJ_X + i] = (uint8_t)x;
    }
    uint16_t sat = p->vdc.reg[R_DVSSR];
    for (int k = 0; k < SATB_WORDS; k++) p->vdc.vram[(sat + k) & 0x7FFF] = 0;
    bool ok = pce_call(p, BUILDER, p->cpu.a, 200000);
    if (ok) {
        sat = p->vdc.reg[R_DVSSR];
        for (int k = 0; k < SATB_WORDS; k++) out[k] = p->vdc.vram[(sat + k) & 0x7FFF];
        if (out[63 * 4] || out[63 * 4 + 1]) w->tables_full++;
    }
    return ok;
}

static void on_entry(Pce *p, void *ctx)
{
    Wide *w = ctx;
    HuC6280 *c = &p->cpu;
    if (c->pc != BUILDER || c->mpr[BUILDER >> 13] != BUILDER_BANK || !w->live) return;
    if (!p->page[BUILDER_BANK] || memcmp(p->page[BUILDER_BANK], builder_sig, sizeof builder_sig)) return;
    static uint16_t left[SATB_WORDS], right[SATB_WORDS];
    memcpy(&saved, p, sizeof *p);
    bool l = builder_table(w, p, 256, left);
    memcpy(p, &saved, sizeof *p);
    bool rt = builder_table(w, p, -256, right);
    memcpy(p, &saved, sizeof *p);
    w->builder_runs++;
    if (!l || !rt) w->builder_fails++;
    vdc_set_side_sprites(l ? left : NULL, -256, rt ? right : NULL, 256);
}

/* ---- the sides, each frame --------------------------------------------------------------- */

/* signed distance in the camera's 4096-pixel space */
static int wrap(int d) { d &= 0xFFF; return d >= 0x800 ? d - 0x1000 : d; }

/* growing glides; shrinking is at once, never past the camera's limits */
static int toward(int cur, int target) { return cur + GROW < target ? cur + GROW : target; }

void wide_before_frame(Wide *w, Pce *p)
{
    if (!p->on_entry && w->want) {
        p->on_entry = on_entry;
        p->on_entry_ctx = w;
    }
    int tl = 0, tr = 0;
    bool allowed = w->want && room_allowed(w, p);
    if (allowed) {
        const uint8_t *r = p->wram;
        int cam = (r[CAM_PAGE] & 0x0F) << 8 | r[CAM_XL];
        int lo = r[LIMIT_LEFT] << 8, hi = r[LIMIT_RIGHT] << 8 | r[LIMIT_RIGHT + 1];
        int room_l = wrap(cam - lo), room_r = wrap(hi - cam);   /* how far the walls are */
        tl = room_l < 0 ? 0 : room_l < w->want ? room_l : w->want;
        tr = room_r < 0 ? 0 : room_r < w->want ? room_r : w->want;
    }
    /* the builder runs for this frame's sides only; without any, their tables are dropped so
     * that none come back stale */
    bool live = tl || tr;
    if (!live && w->live) vdc_drop_side_sprites();
    w->live = live;
    w->left = toward(w->left, tl);
    w->right = toward(w->right, tr);
    vdc_set_side(w->left, w->right);
}

int wide_compose(const Wide *w, const Pce *p, uint32_t *out, int pitch)
{
    if (!w->want || p->vdc.fb_w != 256) return 0;
    const uint32_t *side = vdc_side_pixels();
    int e = w->want;
    for (int y = 0; y < p->vdc.fb_h; y++) {
        uint32_t *o = out + (size_t)y * (size_t)pitch;
        const uint32_t *s = side + (size_t)y * 2 * VDC_SIDE_MAX;
        memcpy(o, s + VDC_SIDE_MAX - e, (size_t)e * sizeof *o);
        memcpy(o + e, &p->vdc.fb[y * PCE_FB_MAX_W], 256 * sizeof *o);
        memcpy(o + e + 256, s + VDC_SIDE_MAX, (size_t)e * sizeof *o);
    }
    return 256 + 2 * e;
}
