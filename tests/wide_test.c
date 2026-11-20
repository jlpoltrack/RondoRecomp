/* Widescreen changes nothing in the game (docs/runtime.md, "Widescreen"): wide_test <game.cue> runs
 * two machines with 64-column sides in every room, one with the sprite hook; states and pictures match. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pce/pce.h"
#include "pce/state.h"
#include "recomp/capture_script.h"
#include "frontend/wide.h"

#define FRAMES 21000   /* the Prologue's chase, its end and Stage 1's first room */
#define FROM 16000     /* compared from here (the Prologue starts at 16098) */

/* The machine states (all but host-side fields) and the pictures, or NULL if the same. */
static const char *differs(const Pce *a, const Pce *b, uint8_t *sa, uint8_t *sb, size_t n)
{
    pce_state_capture(a, sa);
    pce_state_capture(b, sb);
    if (memcmp(sa, sb, n)) return "machine state";
    for (int y = 0; y < a->vdc.fb_h; y++)
        if (memcmp(&a->vdc.fb[y * PCE_FB_MAX_W], &b->vdc.fb[y * PCE_FB_MAX_W], (size_t)a->vdc.fb_w * 4))
            return "picture";
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: wide_test <game.cue>\n"); return 2; }
    static Pce a, b;
    static Wide w = {.want = VDC_SIDE_MAX, .any_room = true};
    char err[256];
    if (!pce_init(&a, argv[1], err, sizeof err) || !pce_boot(&a, err, sizeof err) ||
        !pce_init(&b, argv[1], err, sizeof err) || !pce_boot(&b, err, sizeof err)) { puts(err); return 1; }
    wide_reset(&w);
    size_t n = pce_state_size();
    uint8_t *sa = malloc(n), *sb = malloc(n);
    if (!sa || !sb) return 1;
    long wide_frames = 0;
    for (int f = 0; f < FRAMES; f++) {
        a.pad = b.pad = capture_pad(f);
        pce_run_frame(&a);
        wide_before_frame(&w, &b);
        pce_run_frame(&b);
        if (w.left || w.right) wide_frames++;
        const char *d = f >= FROM ? differs(&a, &b, sa, sb, n) : NULL;
        if (d) { printf("  FAIL frame %d: %s differs\n", f, d); return 1; }
    }
    printf("  ok   %d frames the same; %ld with sides, the builder run %ld times (%ld failed)\n", FRAMES - FROM,
           wide_frames, w.builder_runs, w.builder_fails);
    return wide_frames && w.builder_runs && !w.builder_fails ? 0 : 1;
}
