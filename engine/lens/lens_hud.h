#pragma once
// Presented image: 320x200 3D view stacked over a 320x40 HUD strip, shown at Doom's 1.2 pixel aspect.
#define HUD_W 320
#define HUD_H 40
#define OUT_W 320
#define OUT_H (200 + HUD_H)

// Face: its own canvas (shown large under the HUD). Camera input and canvas share this size;
// state overlays are authored on Doom's 24x29 status-face grid and scaled onto it.
#define FACE_W 56
#define FACE_H 62
#define MASK_W 24
#define MASK_H 29
