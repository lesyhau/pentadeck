#pragma once
/*
 * Visual effects from the microphone, for the AUDIO screen and for the VISUAL mode across all five
 * screens. An effect draws on a canvas of any size: the AUDIO screen's middle (110 x 72) or one
 * 640 x 128 picture over the five screens, each screen showing its own 128 px of it.
 *
 *   SPECTRUM   the 16 bands as LED bars, with falling peak caps
 *   MIRROR     the spectrum mirrored both ways: bass in the middle, treble out at the edges
 *   SCOPE      the sound's waveform, like an oscilloscope (held still on a rising zero crossing)
 *   WATERFALL  the spectrum over time, bass at the bottom; the newest at the right edge, or in the
 *              middle running out both ways when the canvas is wide
 *   RIPPLE     rings sent out from the middle by the beat, a core that swells with the bass
 *   RADIAL     the spectrum round a circle; on the wide canvas, five of them from bass to treble
 *
 * Colours (settings' viz_palette): the accent's ramp, a rainbow, or fire.
 */
#include <stdbool.h>
#include <stdint.h>

#include "gfx/hud.h"

typedef enum { VIZ_SPECTRUM, VIZ_MIRROR, VIZ_SCOPE, VIZ_WATERFALL, VIZ_RIPPLE, VIZ_RADIAL, VIZ_COUNT } viz_effect_t;

/* The canvas is w x h; its (0, 0) sits at (ox, oy) on the screen drawn (ox < 0 for screens to the
 * right of the canvas's first). */
typedef struct { int ox, oy, w, h; } viz_view_t;

void viz_update(uint32_t ms);                 /* every frame (UI task): take the mic's new data */
void viz_draw(hud_fb_t *fb, viz_effect_t fx, const viz_view_t *v, int palette);
const char *viz_name(viz_effect_t fx);        /* "SPECTRUM", "MIRROR", ... */
bool viz_from_key(const char *key, viz_effect_t *fx);   /* the settings' names: "mirror" -> VIZ_MIRROR */
uint32_t viz_beat_ms(void);                   /* when the last beat was heard (esp_timer ms), 0 = none yet */
