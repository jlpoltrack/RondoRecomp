/* Widescreen, drawn beside the game and never in it (docs/runtime.md, "Widescreen").
 * No SDL here: rondo_headless links it too. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pce/pce.h"

/* The widest sides wide_extent gives: the game brings objects in 64 px out, and from 48
 * columns their pieces show in the sides (docs/runtime.md, "Widescreen"). */
#define WIDE_MAX 46

typedef struct Wide {
    int want;                  /* columns asked for each side (0: widescreen off) */
    bool any_room;             /* diagnostics: every scrolling room, listed or not */
    int left, right;           /* the columns this frame's picture has */
    bool live;                 /* this frame has sides: the sprite builder runs for them */
    /* diagnostics, counted while sides may be drawn */
    long builder_runs, builder_fails, tables_full;
} Wide;

/* Columns each side for a screen of `screen_aspect` (w/h) and a picture whose 256 x fb_h
 * pixels show at `picture_aspect`; 0 for screens no wider than the picture. */
int wide_extent(double screen_aspect, double picture_aspect);
/* After a state load, a rewind or a restart: the sides start again from none. */
void wide_reset(Wide *w);
/* A new `want` (window shape, option): the sides start again from none. */
void wide_set_want(Wide *w, int want);
void wide_before_frame(Wide *w, Pce *p);
/* The last picture with its sides at `out`, 0 (no alpha) where not drawn. Returns its width,
 * 256 + 2 * want, or 0 for a picture not 256 wide or with widescreen off (use vdc.fb). */
int wide_compose(const Wide *w, const Pce *p, uint32_t *out, int pitch);
