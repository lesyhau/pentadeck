#include "gfx/hud.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ pixels */

static inline uint16_t pack_be(hud_color_t c)
{
    uint16_t v = (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
    return (uint16_t)((v >> 8) | (v << 8));
}

static inline void blend(hud_fb_t *p, hud_color_t c, uint32_t a)
{
    if (a == 0) return;
    if (a >= 255) { *p = pack_be(c); return; }
    uint16_t v = (uint16_t)((*p >> 8) | (*p << 8));
    uint32_t dr = (v >> 11) & 0x1F, dg = (v >> 5) & 0x3F, db = v & 0x1F;
    dr = (dr << 3) | (dr >> 2);
    dg = (dg << 2) | (dg >> 4);
    db = (db << 3) | (db >> 2);
    uint32_t na = 255 - a;
    uint32_t r = (((c >> 16) & 0xFF) * a + dr * na + 127) / 255;
    uint32_t g = (((c >> 8) & 0xFF) * a + dg * na + 127) / 255;
    uint32_t b = ((c & 0xFF) * a + db * na + 127) / 255;
    *p = pack_be((r << 16) | (g << 8) | b);
}

static inline float clampf01(float v)
{
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

/* ------------------------------------------------------------------ colour */

static const hud_ramp_t TEAL = { 0xA6D6E7u, 0x78BED9u, 0x42A7CDu, 0x2A8CB2u, 0x206F8Du, 0x164E63u, 0x143E4Eu,
                                 0x3C7891u, 0x284654u };
hud_ramp_t hud_ramp = { 0xA6D6E7u, 0x78BED9u, 0x42A7CDu, 0x2A8CB2u, 0x206F8Du, 0x164E63u, 0x143E4Eu,
                        0x3C7891u, 0x284654u };
static uint32_t s_accent = HUD_TEAL;
static float s_dh, s_ks = 1;               /* the accent's hue shift (turns) and saturation factor from teal */

static void rgb_hsl(uint32_t c, float *h, float *s, float *l)
{
    float r = (c >> 16 & 0xFF) / 255.0f, g = (c >> 8 & 0xFF) / 255.0f, b = (c & 0xFF) / 255.0f;
    float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), d = mx - mn;
    *l = (mx + mn) / 2;
    if (d < 1e-6f) { *h = 0; *s = 0; return; }
    *s = d / (1 - fabsf(2 * *l - 1));
    float hh = mx == r ? fmodf((g - b) / d + 6, 6) : mx == g ? (b - r) / d + 2 : (r - g) / d + 4;
    *h = hh / 6;
}

static uint32_t hsl_rgb(float h, float s, float l)
{
    h -= floorf(h);
    s = clampf01(s);
    float c = (1 - fabsf(2 * l - 1)) * s, hp = h * 6, x = c * (1 - fabsf(fmodf(hp, 2) - 1)), m = l - c / 2;
    float r = 0, g = 0, b = 0;
    switch ((int)hp % 6) {
    case 0: r = c; g = x; break;
    case 1: r = x; g = c; break;
    case 2: g = c; b = x; break;
    case 3: g = x; b = c; break;
    case 4: r = x; b = c; break;
    default: r = c; b = x; break;
    }
    uint32_t R = (uint32_t)lroundf(clampf01(r + m) * 255), G = (uint32_t)lroundf(clampf01(g + m) * 255),
             B = (uint32_t)lroundf(clampf01(b + m) * 255);
    return R << 16 | G << 8 | B;
}

/* a teal colour in the accent's hue */
static uint32_t to_accent(uint32_t c)
{
    float h, s, l;
    rgb_hsl(c, &h, &s, &l);
    return hsl_rgb(h + s_dh, s * s_ks, l);
}

void hud_set_accent(uint32_t rgb)
{
    rgb &= 0xFFFFFFu;
    if (rgb == s_accent) return;
    s_accent = rgb;
    if (rgb == HUD_TEAL) {
        s_dh = 0;
        s_ks = 1;
        hud_ramp = TEAL;
        return;
    }
    float ha, sa, la, ht, st, lt;
    rgb_hsl(rgb, &ha, &sa, &la);
    rgb_hsl(HUD_TEAL, &ht, &st, &lt);
    s_dh = ha - ht;
    s_ks = sa / st;
    const uint32_t *src = &TEAL.p300;
    uint32_t out[sizeof TEAL / sizeof(uint32_t)];
    for (size_t i = 0; i < sizeof out / sizeof out[0]; i++) out[i] = to_accent(src[i]);
    memcpy(&hud_ramp, out, sizeof hud_ramp);
}

uint32_t hud_accent(void) { return s_accent; }

hud_color_t hud_mix(hud_color_t a, hud_color_t b, float t)
{
    t = clampf01(t);
    uint32_t k = (uint32_t)(t * 256), n = 256 - k;
    uint32_t r = ((a >> 16 & 0xFF) * n + (b >> 16 & 0xFF) * k) >> 8;
    uint32_t g = ((a >> 8 & 0xFF) * n + (b >> 8 & 0xFF) * k) >> 8;
    uint32_t bl = ((a & 0xFF) * n + (b & 0xFF) * k) >> 8;
    return r << 16 | g << 8 | bl;
}

hud_color_t hud_scale(hud_color_t c, float k)
{
    return hud_mix(0, c, k);
}

hud_color_t hud_hsv(float h, float s, float v)
{
    h -= floorf(h);
    s = clampf01(s);
    v = clampf01(v);
    float hp = h * 6, f = hp - floorf(hp);
    float p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    float r, g, b;
    switch ((int)hp % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    return (uint32_t)(r * 255) << 16 | (uint32_t)(g * 255) << 8 | (uint32_t)(b * 255);
}

hud_color_t level_color(float frac)
{
    return frac >= 0.9f ? C_DANGER : frac >= 0.75f ? C_WARNING : C_ACCENT;
}

hud_color_t temp_color(float c)
{
    return c >= 88.0f ? C_DANGER : c >= 75.0f ? C_WARNING : C_INK;
}

void hud_clear(hud_fb_t *fb, hud_color_t c)
{
    uint16_t v = pack_be(c);
    for (int i = 0; i < HUD_W * HUD_H; i++) fb[i] = v;
}

void hud_rect(hud_fb_t *fb, int x0, int y0, int x1, int y1, hud_color_t c)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > HUD_W) x1 = HUD_W;
    if (y1 > HUD_H) y1 = HUD_H;
    if (x1 <= x0 || y1 <= y0) return;
    uint16_t v = pack_be(c);
    for (int y = y0; y < y1; y++) {
        hud_fb_t *row = fb + y * HUD_W;
        for (int x = x0; x < x1; x++) row[x] = v;
    }
}

/* Anti-aliased thick line with flat ends (PIL's line), by coverage from the distance to the segment. */
void hud_line(hud_fb_t *fb, float x0, float y0, float x1, float y1, float width, hud_color_t c)
{
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-3f) return;
    float ux = dx / len, uy = dy / len, hw = width / 2;
    int bx0 = (int)floorf(fminf(x0, x1) - hw - 1), bx1 = (int)ceilf(fmaxf(x0, x1) + hw + 1);
    int by0 = (int)floorf(fminf(y0, y1) - hw - 1), by1 = (int)ceilf(fmaxf(y0, y1) + hw + 1);
    if (bx0 < 0) bx0 = 0;
    if (by0 < 0) by0 = 0;
    if (bx1 > HUD_W) bx1 = HUD_W;
    if (by1 > HUD_H) by1 = HUD_H;
    for (int py = by0; py < by1; py++) {
        for (int px = bx0; px < bx1; px++) {
            float rx = px + 0.5f - x0, ry = py + 0.5f - y0;
            float t = rx * ux + ry * uy;
            float d = fabsf(-rx * uy + ry * ux);
            float cov = clampf01(hw + 0.5f - d) * clampf01(t + 0.5f) * clampf01(len - t + 0.5f);
            if (cov > 0.004f) blend(&fb[py * HUD_W + px], c, (uint32_t)(cov * 255 + 0.5f));
        }
    }
}

void hud_circle(hud_fb_t *fb, float cx, float cy, float r, hud_color_t c)
{
    int bx0 = (int)floorf(cx - r - 1), bx1 = (int)ceilf(cx + r + 1);
    int by0 = (int)floorf(cy - r - 1), by1 = (int)ceilf(cy + r + 1);
    if (bx0 < 0) bx0 = 0;
    if (by0 < 0) by0 = 0;
    if (bx1 > HUD_W) bx1 = HUD_W;
    if (by1 > HUD_H) by1 = HUD_H;
    for (int py = by0; py < by1; py++) {
        for (int px = bx0; px < bx1; px++) {
            float ddx = px + 0.5f - cx, ddy = py + 0.5f - cy;
            float cov = clampf01(r + 0.5f - sqrtf(ddx * ddx + ddy * ddy));
            if (cov > 0.004f) blend(&fb[py * HUD_W + px], c, (uint32_t)(cov * 255 + 0.5f));
        }
    }
}

void hud_image(hud_fb_t *fb, const hud_image_t *img, int x, int y)
{
    for (int j = 0; j < img->h; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= HUD_H) continue;
        int i0 = x < 0 ? -x : 0, i1 = x + img->w > HUD_W ? HUD_W - x : img->w;
        if (i1 <= i0) continue;
        memcpy(&fb[yy * HUD_W + x + i0], img->px + (j * img->w + i0) * 2, (size_t)(i1 - i0) * 2);
    }
}

void hud_image_accent(hud_fb_t *fb, const hud_image_t *img, int x, int y)
{
    if (s_accent == HUD_TEAL) { hud_image(fb, img, x, y); return; }
    /* the images use a handful of colours: remember the last few conversions */
    static uint16_t memo_in[8], memo_out[8];
    static uint32_t memo_for = 0xFFFFFFFFu;
    static int memo_n;
    if (memo_for != s_accent) { memo_for = s_accent; memo_n = 0; }
    for (int j = 0; j < img->h; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= HUD_H) continue;
        for (int i = 0; i < img->w; i++) {
            int xx = x + i;
            if (xx < 0 || xx >= HUD_W) continue;
            const uint8_t *p = img->px + (j * img->w + i) * 2;
            uint16_t be = (uint16_t)(p[0] | p[1] << 8);       /* as stored: big-endian */
            uint16_t v = (uint16_t)(p[0] << 8 | p[1]);
            uint32_t r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
            r = (r << 3) | (r >> 2);
            g = (g << 2) | (g >> 4);
            b = (b << 3) | (b >> 2);
            uint32_t mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
            if (mx - mn < 12) { fb[yy * HUD_W + xx] = be; continue; }   /* black, greys, white */
            int k;
            for (k = 0; k < memo_n && memo_in[k] != be; k++) {}
            if (k == memo_n) {
                if (k == 8) k = memo_n = 0;                    /* full: start over */
                memo_in[k] = be;
                memo_out[k] = pack_be(to_accent(r << 16 | g << 8 | b));
                memo_n++;
            }
            fb[yy * HUD_W + xx] = memo_out[k];
        }
    }
}

void hud_ring(hud_fb_t *fb, float cx, float cy, float r, float width, hud_color_t c)
{
    float hw = width / 2, ro = r + hw + 1, ri = r - hw - 1;
    if (r <= 0 || cx + ro < 0 || cx - ro > HUD_W || cy + ro < 0 || cy - ro > HUD_H) return;
    if (ri > 0) {                                     /* the whole screen inside the hole: nothing to draw */
        float fx = fmaxf(fabsf(cx), fabsf(cx - HUD_W)), fy = fmaxf(fabsf(cy), fabsf(cy - HUD_H));
        if (fx * fx + fy * fy < ri * ri) return;
    }
    int y0 = (int)floorf(cy - ro), y1 = (int)ceilf(cy + ro);
    if (y0 < 0) y0 = 0;
    if (y1 > HUD_H) y1 = HUD_H;
    for (int py = y0; py < y1; py++) {
        float dy = py + 0.5f - cy;
        if (fabsf(dy) > ro) continue;
        float xo = sqrtf(ro * ro - dy * dy);
        float xi = ri > 0 && fabsf(dy) < ri ? sqrtf(ri * ri - dy * dy) : -1;
        for (int side = 0; side < 2; side++) {
            float a, b;
            if (xi < 0) { if (side) break; a = cx - xo; b = cx + xo; }
            else if (side == 0) { a = cx - xo; b = cx - xi; }
            else { a = cx + xi; b = cx + xo; }
            int px0 = (int)floorf(a), px1 = (int)ceilf(b);
            if (px0 < 0) px0 = 0;
            if (px1 > HUD_W) px1 = HUD_W;
            for (int px = px0; px < px1; px++) {
                float dx = px + 0.5f - cx;
                float cov = clampf01(hw + 0.5f - fabsf(sqrtf(dx * dx + dy * dy) - r));
                if (cov > 0.004f) blend(&fb[py * HUD_W + px], c, (uint32_t)(cov * 255 + 0.5f));
            }
        }
    }
}

/* -------------------------------------------------------------------- text */

static uint32_t next_cp(const char **s)
{
    const uint8_t *p = (const uint8_t *)*s;
    uint32_t cp;
    if (p[0] < 0x80) { cp = p[0]; *s += 1; }
    else if ((p[0] & 0xE0) == 0xC0 && p[1]) { cp = ((p[0] & 0x1Fu) << 6) | (p[1] & 0x3F); *s += 2; }
    else if ((p[0] & 0xF0) == 0xE0 && p[1] && p[2]) {
        cp = ((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3F);
        *s += 3;
    } else { cp = '?'; *s += 1; }
    return cp;
}

static const hud_glyph_t *glyph_of(const hud_font_t *f, uint32_t cp)
{
    int i;
    if (cp >= 0x20 && cp <= 0x7E) i = (int)cp - 0x20;
    else if (cp == 0xB0) i = 95;     /* ° */
    else if (cp == 0xB7) i = 96;     /* · */
    else if (cp == 0x2013) i = 97;   /* – */
    else if (cp == 0x2014) i = 98;   /* — */
    else if (cp == 0x2026) i = 99;   /* … */
    else i = '?' - 0x20;
    return &f->glyphs[i];
}

static void draw_glyph(hud_fb_t *fb, const hud_font_t *f, const hud_glyph_t *g, int pen_x, int baseline, hud_color_t c)
{
    int left = pen_x + g->ox, top = baseline + g->top;
    const uint8_t *cov = f->bitmap + g->off;
    for (int j = 0; j < g->h; j++) {
        int y = top + j;
        if (y < 0 || y >= HUD_H) continue;
        for (int i = 0; i < g->w; i++) {
            int x = left + i;
            if (x < 0 || x >= HUD_W) continue;
            blend(&fb[y * HUD_W + x], c, cov[j * g->w + i]);
        }
    }
}

int hud_text_w(const char *text, const hud_font_t *f, int spacing)
{
    const char *s = text;
    if (spacing) {
        int w = 0, n = 0;
        while (*s) { w += (glyph_of(f, next_cp(&s))->adv16 + 8) >> 4; n++; }
        return w + spacing * (n > 0 ? n - 1 : 0);
    }
    uint32_t w16 = 0;
    while (*s) w16 += glyph_of(f, next_cp(&s))->adv16;
    return (int)((w16 + 8) >> 4);
}

void hud_text(hud_fb_t *fb, int x, int y, const char *text, const hud_font_t *f, hud_color_t c,
              const char *anchor, int spacing)
{
    if (!text || !text[0]) return;
    int baseline;
    switch (anchor[1]) {
    case 's': baseline = y; break;
    case 'a': case 't': baseline = y + f->ascent; break;
    case 'b': case 'd': baseline = y - f->descent; break;
    default:  baseline = y + (int)lroundf((f->ascent - f->descent) / 2.0f); break;   /* 'm' */
    }
    const char *s = text;
    if (spacing) {
        int total = hud_text_w(text, f, spacing);
        int cx = (int)lroundf(x - (anchor[0] == 'm' ? total / 2.0f : anchor[0] == 'r' ? (float)total : 0));
        while (*s) {
            const hud_glyph_t *g = glyph_of(f, next_cp(&s));
            draw_glyph(fb, f, g, cx, baseline, c);
            cx += ((g->adv16 + 8) >> 4) + spacing;
        }
        return;
    }
    int32_t total16 = 0;
    for (const char *t = text; *t;) total16 += glyph_of(f, next_cp(&t))->adv16;
    int32_t start16 = x * 16 - (anchor[0] == 'm' ? total16 / 2 : anchor[0] == 'r' ? total16 : 0);
    int32_t pen16 = 0;
    while (*s) {
        const hud_glyph_t *g = glyph_of(f, next_cp(&s));
        int32_t gx16 = start16 + pen16 + 8;
        draw_glyph(fb, f, g, gx16 >= 0 ? gx16 / 16 : -((15 - gx16) / 16), baseline, c);
        pen16 += g->adv16;
    }
}

void hud_ellipsize(char *out, size_t n, const char *text, const hud_font_t *font, int max_w, int spacing)
{
    strlcpy(out, text, n);
    if (hud_text_w(out, font, spacing) <= max_w) return;
    size_t len = strlen(out);
    while (len > 0) {
        len--;
        while (len > 0 && out[len - 1] == ' ') len--;
        snprintf(out + len, n - len, "\xE2\x80\xA6");   /* … */
        if (hud_text_w(out, font, spacing) <= max_w) return;
        out[len] = 0;
    }
}

/* ------------------------------------------------------- cockpit pieces */

static inline void dot(hud_fb_t *fb, int x, int y, hud_color_t c)
{
    if (x >= 0 && x < HUD_W && y >= 0 && y < HUD_H) fb[y * HUD_W + x] = pack_be(c);
}

void hud_frame(hud_fb_t *fb, hud_color_t c, hud_color_t dim, int phase)
{
    const int in = 2, cut = 3, arm = 8;
    for (int corner = 0; corner < 4; corner++) {
        bool mx = corner & 1, my = corner & 2;
#define P(x, y) dot(fb, mx ? HUD_W - 1 - (x) : (x), my ? HUD_H - 1 - (y) : (y), c)
        for (int k = 0; k <= arm; k++) {
            P(in + cut + k, in);                          /* along the top / bottom edge */
            P(in, in + cut + k);                          /* along the side */
        }
        for (int k = 1; k < cut; k++) P(in + k, in + cut - k);   /* the 45-degree cut */
#undef P
    }
    /* rulers: a 2 px tick every 8 px between the corners, the middle one 4 px long and brighter */
    for (int y = 24; y <= 104; y += 8) {
        bool mid = y == 64;
        int len = mid ? 4 : 2;
        hud_rect(fb, in, y, in + len, y + 1, mid ? c : dim);
        hud_rect(fb, HUD_W - in - len, y, HUD_W - in, y + 1, mid ? c : dim);
    }
    /* activity: three 5 px dashes at the bottom centre, one lit */
    for (int i = 0; i < 3; i++) {
        int x = 54 + i * 8;
        hud_rect(fb, x, HUD_H - in - 1, x + 5, HUD_H - in, i == ((phase % 3) + 3) % 3 ? c : dim);
    }
}

void hud_bezel(hud_fb_t *fb, float cx, float cy, float r, float step, float top_gap, hud_color_t dots,
               hud_color_t marks)
{
    const float start = 135.0f, sweep = 270.0f;
    int n = (int)lroundf(sweep / step);
    for (int i = 0; i <= n; i++) {
        float deg = start + sweep * i / n;
        if (top_gap > 0 && fabsf(deg - 270.0f) <= top_gap) continue;
        float a = deg * (float)M_PI / 180.0f;
        bool mark = i == 0 || i == n || i * 2 == n;
        float ca = cosf(a), sa = sinf(a);
        if (mark) {
            for (int k = 0; k < 3; k++) dot(fb, (int)floorf(cx + ca * (r + k)), (int)floorf(cy + sa * (r + k)), marks);
        } else {
            dot(fb, (int)floorf(cx + ca * r), (int)floorf(cy + sa * r), dots);
        }
    }
}

void hud_header(hud_fb_t *fb, const hud_font_t *font, const char *title, int y, int spacing, char led,
                hud_color_t led_color, hud_color_t title_color, hud_color_t rule_color)
{
    int tw = hud_text_w(title, font, spacing);
    hud_text(fb, 64, y, title, font, title_color, "mm", spacing);
    int x0 = (int)lroundf(64 - tw / 2.0f) - 5, x1 = (int)lroundf(64 + tw / 2.0f) + 5;
    int left = led == 'l' ? 20 : 14, right = led == 'r' ? HUD_W - 20 : HUD_W - 14;   /* led 0 = none */
    if (x0 - left >= 7) {                                                   /* no stubs beside a wide title */
        hud_rect(fb, left + 3, y, x0, y + 1, rule_color);
        hud_rect(fb, left, y, left + 2, y + 1, title_color);                /* end node */
    }
    if (right - x1 >= 7) {
        hud_rect(fb, x1, y, right - 3, y + 1, rule_color);
        hud_rect(fb, right - 2, y, right, y + 1, title_color);
    }
    if (led == 'l' || led == 'r') {
        float lx = led == 'l' ? 14 : HUD_W - 14;
        hud_circle(fb, lx, y + 0.5f, 2.5f, led_color);
    }
}

void hud_dial(hud_fb_t *fb, float cx, float cy, float r, float frac, int ticks, float len, float width,
              bool tip_glow, float scan, hud_color_t lit_color)
{
    const float start = 135.0f, sweep = 270.0f;
    frac = clampf01(frac);
    int lit = (int)lroundf(frac * ticks);
    if (lit < 1) lit = 1;
    int scan_i = scan < 0 ? -100 : (int)lroundf(scan * (ticks - 1));
    for (int i = 0; i < ticks; i++) {
        float a = (start + sweep * i / (ticks - 1)) * (float)M_PI / 180.0f;
        hud_color_t col = i >= lit ? C_OFF : lit_color != HUD_AUTO ? lit_color : level_color((float)i / (ticks - 1));
        if (scan_i > -100 && i >= lit && abs(i - scan_i) <= 1) col = i == scan_i ? C_SCAN : C_SCAN_DIM;
        if (tip_glow && i == lit - 1) col = C_INK;
        float ca = cosf(a), sa = sinf(a);
        hud_line(fb, cx + ca * (r - len), cy + sa * (r - len), cx + ca * r, cy + sa * r, width, col);
    }
}

void hud_segbar(hud_fb_t *fb, int x0, int y0, int x1, int y1, float frac, int segs, int gap, int scan)
{
    frac = clampf01(frac);
    float w = (float)(x1 - x0 - gap * (segs - 1)) / segs;
    int lit = (int)lroundf(frac * segs);
    for (int i = 0; i < segs; i++) {
        int sx = (int)lroundf(x0 + i * (w + gap));
        int ex = (int)lroundf(x0 + i * (w + gap) + w);
        hud_color_t col = i < lit ? level_color((float)i / (segs > 1 ? segs - 1 : 1)) : C_OFF;
        if (scan >= 0 && i == scan && i < lit) col = C_P300;
        hud_rect(fb, sx, y0, ex, y1, col);
    }
}
