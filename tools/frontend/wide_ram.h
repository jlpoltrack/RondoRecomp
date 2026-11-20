/* Work RAM that widescreen and its audit read (docs/findings.md, "Camera, scrolling and objects"). */
#pragma once

#define CAM_XL 0x1E08          /* layer 0 (the camera) X low byte */
#define CAM_PAGE 0x1E96        /* its X high byte, mod 16 */
#define CAM_YL 0x1E20          /* its Y, near enough (sub $1E18, high $1E28) */
#define CAM_YH 0x1E28
#define LIMIT_LEFT 0x1EBE      /* the camera's leftmost page */
#define LIMIT_RIGHT 0x1EC6     /* its rightmost X: page, low byte */
#define AREA 0xA2
#define ROOM 0x83
#define OBJ_SLOTS 0x58
#define OBJ_KIND 0x0750
#define OBJ_FRAME 0x0A68       /* animation frame, 0: not shown */
#define OBJ_X_HI 0x0B70        /* position in the picture, as moves.c */
#define OBJ_X 0x0BC8
#define OBJ_FLAGS 0x10F0       /* bit 0: fixed to the screen */
