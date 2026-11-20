/* See wide_audit.h. */
#include "wide_audit.h"

#include <string.h>

#include "util/png.h"
#include "../frontend/wide_ram.h"

typedef struct RoomStats {
    int area, room;
    long frames, wide_frames, sum_l, sum_r;
    long cells, mismatches, unverified, band_rows;
    long popins, vanishes;
    bool shot[2];              /* first mismatch, pop-in saved */
} RoomStats;

static RoomStats rooms[128];
static int n_rooms;
static int cur_area = -1, cur_room = -1;
static int prev_cam = -1, prev_cam_y;
static uint16_t truth[512][512];  /* map entry by world cell X, Y */
static bool known[512][512];
static uint8_t prev_kind[OBJ_SLOTS], prev_frame[OBJ_SLOTS];
static int prev_x[OBJ_SLOTS];

static RoomStats *room_stats(int area, int room)
{
    for (int i = 0; i < n_rooms; i++)
        if (rooms[i].area == area && rooms[i].room == room) return &rooms[i];
    if (n_rooms == (int)(sizeof rooms / sizeof rooms[0])) return &rooms[n_rooms - 1];
    rooms[n_rooms] = (RoomStats){.area = area, .room = room};
    return &rooms[n_rooms++];
}

static void shot(RoomStats *s, int kind, const char *what, const Pce *p, const uint32_t *canvas, int cw, const char *dir)
{
    if (s->shot[kind] || !dir || !cw) return;
    s->shot[kind] = true;
    char path[512];
    snprintf(path, sizeof path, "%s/audit_%02X_%02X_%s_%llu.png", dir, s->area, s->room, what,
             (unsigned long long)p->frame);
    png_write_argb_alpha(path, canvas, cw, p->vdc.fb_h, 256 + 2 * VDC_SIDE_MAX);
}

void audit_frame(const Wide *w, const Pce *p, const uint32_t *canvas, int cw, const char *dir)
{
    const uint8_t *r = p->wram;
    int area = r[AREA], room = r[ROOM];
    if (area != cur_area || room != cur_room) {
        memset(known, 0, sizeof known);
        cur_area = area;
        cur_room = room;
        prev_cam = -1;
    }
    RoomStats *s = room_stats(area, room);
    s->frames++;
    /* the sides this frame drew (wide_before_frame set them before it ran) */
    int l = w->left, rt = w->right;
    if (l || rt) { s->wide_frames++; s->sum_l += l; s->sum_r += rt; }

    /* background: rows drawn with the camera's scroll, as it was when the picture began */
    const uint16_t *scroll = vdc_row_scroll(), *rowy = vdc_row_y();
    bool bad = false;
    int mw = vdc_map_w(&p->vdc), mh = (p->vdc.reg[R_MWR] & 0x40) ? 64 : 32, map_h = mh * 8;
    /* (not while the background is off: rooms are drawn then, unseen) */
    if (prev_cam >= 0 && p->vdc.fb_w == 256 && (p->vdc.reg[R_CR] & 0x80)) {
        for (int y = 0; y < p->vdc.fb_h; y += 4) {
            if (((scroll[y] - prev_cam) & (mw * 8 - 1)) != 0) { s->band_rows++; continue; }
            /* the world Y nearest the camera's Y + y that the map line drawn matches */
            int near = prev_cam_y + y, d = (rowy[y] - near) & (map_h - 1);
            int wy = (near + (d >= map_h / 2 ? d - map_h : d)) & 0xFFF;
            int mrow = (rowy[y] >> 3) & (mh - 1), cy = wy >> 3;
            for (int x = -VDC_SIDE_MAX; x < 256 + VDC_SIDE_MAX; x += 8) {
                int wx = (prev_cam + x) & 0xFFF;
                int cx = wx >> 3;
                int bx = ((scroll[y] + x) & (mw * 8 - 1)) >> 3;
                uint16_t e = p->vdc.vram[mrow * mw + bx];
                if (x >= 0 && x < 256) {
                    if ((wx & 7) == 0 || x == 0) { truth[cx][cy] = e; known[cx][cy] = true; }
                    continue;
                }
                bool in_l = x < 0 && x >= -l, in_r = x >= 256 && x < 256 + rt;
                if (!in_l && !in_r) continue;
                s->cells++;
                if (!known[cx][cy]) s->unverified++;
                else if (truth[cx][cy] != e) { s->mismatches++; bad = true; }
            }
        }
    }
    if (bad) shot(s, 0, "mismatch", p, canvas, cw, dir);

    /* objects starting or ending inside a side */
    bool pop = false;
    for (int i = 0x10; i < OBJ_SLOTS; i++) {
        int x = (int16_t)(r[OBJ_X_HI + i] << 8 | r[OBJ_X + i]);
        bool shown = r[OBJ_KIND + i] && r[OBJ_FRAME + i];
        bool was = prev_kind[i] && prev_frame[i];
        bool in_side = (x < 0 && x >= -l - 16) || (x >= 256 && x < 256 + rt + 16);
        bool was_side = (prev_x[i] < 0 && prev_x[i] >= -l - 16) || (prev_x[i] >= 256 && prev_x[i] < 256 + rt + 16);
        if (shown && !was && in_side && (l || rt)) { s->popins++; pop = true; }
        if (!shown && was && was_side && (l || rt)) s->vanishes++;
        prev_kind[i] = r[OBJ_KIND + i];
        prev_frame[i] = r[OBJ_FRAME + i];
        prev_x[i] = x;
    }
    if (pop) shot(s, 1, "popin", p, canvas, cw, dir);

    prev_cam = (r[CAM_PAGE] & 0x0F) << 8 | r[CAM_XL];
    prev_cam_y = r[CAM_YH] << 8 | r[CAM_YL];
}

void audit_write(FILE *f)
{
    fprintf(f, "area room  frames  wide  avg_l avg_r    cells mismatch unverified band_rows  popins vanishes\n");
    for (int i = 0; i < n_rooms; i++) {
        const RoomStats *s = &rooms[i];
        double wf = s->wide_frames ? (double)s->wide_frames : 1.0;
        fprintf(f, "  %02X   %02X  %6ld %5ld  %5.1f %5.1f %8ld %8ld %10ld %9ld %7ld %8ld\n", s->area, s->room,
                s->frames, s->wide_frames, (double)s->sum_l / wf, (double)s->sum_r / wf, s->cells, s->mismatches,
                s->unverified, s->band_rows, s->popins, s->vanishes);
    }
}
