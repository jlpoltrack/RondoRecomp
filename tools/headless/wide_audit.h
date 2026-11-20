/* rondo_headless --wide-audit: per room, side map cells against those seen in the middle,
 * and objects starting or ending inside a side (docs/runtime.md, "Widescreen"). */
#pragma once

#include <stdio.h>

#include "pce/pce.h"
#include "../frontend/wide.h"

void audit_frame(const Wide *w, const Pce *p, const uint32_t *canvas, int canvas_w, const char *png_dir);
void audit_write(FILE *f);
