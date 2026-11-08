#pragma once
/*
 * HUD renderer: a small software rasteriser that draws straight into a 128x128 frame buffer in
 * the panels' pixel format (big-endian RGB565). A rect covers [x0, x1) x [y0, y1); shapes use
 * continuous coordinates with pixel centres at i + 0.5; text is pre-rendered (tools/gen_fonts.py).
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HUD_W 128
#define HUD_H 128

typedef uint16_t hud_fb_t;            /* HUD_W * HUD_H pixels, big-endian RGB565 */
typedef uint32_t hud_color_t;         /* 0xRRGGBB */

typedef struct {
    uint32_t off;                     /* into the font's 8-bit coverage bitmap */
    uint8_t w, h;
    int8_t ox;                        /* left edge relative to the pen */
    int8_t top;                       /* top edge relative to the baseline (negative = above) */
    uint16_t adv16;                   /* advance, 1/16 px */
} hud_glyph_t;

typedef struct {
    int16_t ascent, descent;
    const hud_glyph_t *glyphs;        /* ASCII 0x20..0x7E, then ° · – — … */
    const uint8_t *bitmap;
} hud_font_t;

typedef struct {
    uint16_t w, h;
    const uint8_t *px;                /* big-endian RGB565, row by row */
} hud_image_t;

/* The palette, tuned for the LCDs: true black, bright inks, the teal brand ramp */
#define C_CANVAS   0x000000u
#define C_INK      0xFFFFFFu
#define C_INK2     0xE4E5E7u
#define C_CAPTION  0xB8BABEu
#define C_DISABLED 0x8C8E92u
#define C_P300     0xA6D6E7u
#define C_P400     0x78BED9u
#define C_P500     0x42A7CDu
#define C_P600     0x2A8CB2u
#define C_P700     0x206F8Du
#define C_P900     0x164E63u
#define C_HUD      C_P600
#define C_HUD_DIM  0x143E4Eu
#define C_OFF      0x222426u
#define C_SCAN     0x3C7891u
#define C_SCAN_DIM 0x284654u
#define C_SUCCESS  0x34D399u
#define C_WARNING  0xFBBF24u
#define C_DANGER   0xFF0000u   /* a pure red: the web page's softer #F87171 reads pink on the LCDs */
#define C_ORANGE   0xFB923Cu
#define HUD_AUTO   0xFF000000u   /* "use the default colour" */
#define C_ACCENT   C_P400   /* BRAND_ACCENT */
#define C_LEGIBLE  C_P300   /* BRAND_LEGIBLE */

hud_color_t level_color(float frac);             /* teal, amber >= 0.75, red >= 0.9 */
hud_color_t temp_color(float celsius);           /* white, amber >= 75, red >= 88 */

void hud_clear(hud_fb_t *fb, hud_color_t c);
void hud_rect(hud_fb_t *fb, int x0, int y0, int x1, int y1, hud_color_t c);
void hud_line(hud_fb_t *fb, float x0, float y0, float x1, float y1, float width, hud_color_t c);
void hud_circle(hud_fb_t *fb, float cx, float cy, float r, hud_color_t c);
void hud_image(hud_fb_t *fb, const hud_image_t *img, int x, int y);

/* Text with a PIL-style anchor: [l|m|r][a|t|m|s|b|d] (s = baseline). `spacing` = extra px between
 * letters, each glyph then placed on a whole pixel (theme.py Canvas.text). UTF-8. */
void hud_text(hud_fb_t *fb, int x, int y, const char *text, const hud_font_t *font, hud_color_t c,
              const char *anchor, int spacing);
int hud_text_w(const char *text, const hud_font_t *font, int spacing);
void hud_ellipsize(char *out, size_t n, const char *text, const hud_font_t *font, int max_w, int spacing);

/* Cockpit pieces (theme.py Canvas.header / dial / segbar) */
/* The panel frame: chamfered corner brackets in `c`; tick rulers up both sides and three activity
 * dashes at the bottom centre in `dim`, the dash `phase % 3` lit in `c`. All 1 px, on the grid. */
void hud_frame(hud_fb_t *fb, hud_color_t c, hud_color_t dim, int phase);

/* A dotted bezel round a dial (same 270-degree sweep as hud_dial): one pixel every `step` degrees
 * at radius r, and short index marks at the start and the end, and at the top unless `top_gap`
 * (degrees either side of the top left empty, e.g. under a title) is set. */
void hud_bezel(hud_fb_t *fb, float cx, float cy, float r, float step, float top_gap, hud_color_t dots,
               hud_color_t marks);
void hud_header(hud_fb_t *fb, const hud_font_t *font, const char *title, int y, int spacing, char led,
                hud_color_t led_color, hud_color_t title_color, hud_color_t rule_color);
void hud_dial(hud_fb_t *fb, float cx, float cy, float r, float frac, int ticks, float len, float width,
              bool tip_glow, float scan /* 0..1, <0 = none */, hud_color_t lit_color /* HUD_AUTO = by level */);
void hud_segbar(hud_fb_t *fb, int x0, int y0, int x1, int y1, float frac, int segs, int gap,
                int scan /* lit segment to brighten, -1 = none */);
