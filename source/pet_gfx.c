/*
 * Drawing helpers shared by all screens.
 *
 * Everything that may touch the screen edge is clipped here: the u8g2
 * primitives take unsigned coordinates, so a box starting at x = -3 would
 * otherwise vanish completely. Integer math only, so the PC preview
 * (tools/flipper_preview/pgfx.py) produces the very same pixels.
 */
#include "pet.h"

int32_t gfx_isqrt(int32_t n) {
    if(n <= 0) return 0;
    int32_t x = n;
    int32_t y = (x + 1) / 2;
    while(y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

/* sin(k * 90 / 16 deg) * 64 for k = 0..15 */
static const int8_t sin64[16] = {0, 6, 12, 19, 24, 30, 36, 41, 45, 49, 53, 56, 59, 61, 63, 64};

int16_t gfx_sin(int32_t a) {
    a &= 63;
    if(a < 16) return sin64[a];
    if(a < 32) return a == 16 ? 64 : sin64[32 - a];
    if(a < 48) return -sin64[a - 32];
    return a == 48 ? -64 : -sin64[64 - a];
}

int16_t gfx_cos(int32_t a) {
    return gfx_sin(a + 16);
}

int16_t gfx_ellipse_half(int16_t rx, int16_t ry, int16_t dy) {
    if(ry <= 0) return rx;
    if(dy < 0) dy = -dy;
    if(dy > ry) return -1;
    int32_t w = gfx_isqrt((int32_t)rx * rx * (ry * ry - dy * dy + ry) / (ry * ry));
    return w > rx ? rx : (int16_t)w;
}

void gfx_hline(Canvas* c, int32_t x, int32_t y, int32_t w) {
    if(y < 0 || y >= SCREEN_H || w <= 0) return;
    if(x < 0) {
        w += x;
        x = 0;
    }
    if(x + w > SCREEN_W) w = SCREEN_W - x;
    if(w <= 0) return;
    canvas_draw_box(c, x, y, w, 1);
}

void gfx_vline(Canvas* c, int32_t x, int32_t y, int32_t h) {
    if(x < 0 || x >= SCREEN_W || h <= 0) return;
    if(y < 0) {
        h += y;
        y = 0;
    }
    if(y + h > SCREEN_H) h = SCREEN_H - y;
    if(h <= 0) return;
    canvas_draw_box(c, x, y, 1, h);
}

void gfx_bmp(Canvas* c, int32_t x, int32_t y, const Bmp* b) {
    canvas_draw_xbm(c, x, y, b->w, b->h, b->data);
}

void gfx_bmp_color(Canvas* c, int32_t x, int32_t y, const Bmp* b, Color col) {
    canvas_set_color(c, col);
    canvas_draw_xbm(c, x, y, b->w, b->h, b->data);
    canvas_set_color(c, ColorBlack);
}

void gfx_dither(Canvas* c, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t phase) {
    for(int32_t j = 0; j < h; j++) {
        int32_t yy = y + j;
        if(yy < 0 || yy >= SCREEN_H) continue;
        for(int32_t i = 0; i < w; i++) {
            int32_t xx = x + i;
            if(xx < 0 || xx >= SCREEN_W) continue;
            if(((xx + yy + phase) & 1) == 0) canvas_draw_dot(c, xx, yy);
        }
    }
}

void gfx_sparse(Canvas* c, int32_t x, int32_t y, int32_t w, int32_t h) {
    for(int32_t j = 0; j < h; j++) {
        int32_t yy = y + j;
        if(yy < 0 || yy >= SCREEN_H || (yy & 1)) continue;
        for(int32_t i = 0; i < w; i++) {
            int32_t xx = x + i;
            if(xx < 0 || xx >= SCREEN_W || (xx & 1)) continue;
            if(((xx >> 1) + (yy >> 1)) & 1) canvas_draw_dot(c, xx, yy);
        }
    }
}

void gfx_invert(Canvas* c, int32_t x, int32_t y, int32_t w, int32_t h) {
    if(x < 0) {
        w += x;
        x = 0;
    }
    if(y < 0) {
        h += y;
        y = 0;
    }
    if(x + w > SCREEN_W) w = SCREEN_W - x;
    if(y + h > SCREEN_H) h = SCREEN_H - y;
    if(w <= 0 || h <= 0) return;
    canvas_set_color(c, ColorXOR);
    canvas_draw_box(c, x, y, w, h);
    canvas_set_color(c, ColorBlack);
}

void gfx_str_center(Canvas* c, int32_t cx, int32_t baseline, const char* s) {
    int32_t w = canvas_string_width(c, s);
    canvas_draw_str(c, cx - w / 2, baseline, s);
}

void gfx_str_right(Canvas* c, int32_t rx, int32_t baseline, const char* s) {
    int32_t w = canvas_string_width(c, s);
    canvas_draw_str(c, rx - w, baseline, s);
}

/* Rounded progress bar: 1 px frame, 1 px gap, fill. */
void gfx_bar(Canvas* c, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t pct) {
    canvas_draw_rframe(c, x, y, w, h, 1);
    int32_t inner = w - 2;
    int32_t f = (inner * pct + 50) / 100;
    if(f > inner) f = inner;
    if(f > 0) canvas_draw_box(c, x + 1, y + 1, f, h - 2);
}

void gfx_stars(Canvas* c, int32_t x, int32_t y, uint8_t rarity) {
    for(uint8_t i = 0; i < 5; i++) {
        gfx_bmp(c, x + i * 6, y, i < rarity ? &bmp_star5 : &bmp_star5_empty);
    }
}

void gfx_button_hint(Canvas* c, int32_t x, int32_t baseline, const Bmp* icon, const char* label) {
    gfx_bmp(c, x, baseline - icon->h, icon);
    canvas_draw_str(c, x + icon->w + 2, baseline, label);
}

void gfx_scrollbar(Canvas* c, int32_t x, int32_t y, int32_t h, int32_t pos, int32_t total, int32_t vis) {
    if(total <= vis) return;
    for(int32_t j = 0; j < h; j += 2)
        canvas_draw_dot(c, x + 1, y + j);
    int32_t th = h * vis / total;
    if(th < 4) th = 4;
    int32_t ty = y + (h - th) * pos / (total - vis);
    canvas_draw_box(c, x, ty, 3, th);
}

/* Screen title: bold text, centred, with a thin rule underneath. */
void gfx_title(Canvas* c, const char* title) {
    canvas_set_font(c, FontPrimary);
    gfx_str_center(c, 64, 9, title);
    int32_t w = canvas_string_width(c, title);
    gfx_hline(c, 0, 12, 64 - w / 2 - 4);
    gfx_hline(c, 64 + w / 2 + 4, 12, 64);
    canvas_draw_dot(c, 64 - w / 2 - 6, 12);
    canvas_draw_dot(c, 64 + w / 2 + 5, 12);
}

/* Page indicator dots. */
void gfx_dots(Canvas* c, int32_t cx, int32_t y, uint8_t n, uint8_t sel) {
    int32_t x = cx - (n * 5 - 2) / 2;
    for(uint8_t i = 0; i < n; i++) {
        if(i == sel)
            canvas_draw_box(c, x + i * 5, y, 3, 3);
        else
            canvas_draw_dot(c, x + i * 5 + 1, y + 1);
    }
}

/* Rotating light rays (level up / evolution). */
void gfx_burst(Canvas* c, int32_t cx, int32_t cy, int32_t r0, int32_t r1, int32_t rot, uint8_t rays) {
    for(uint8_t i = 0; i < rays; i++) {
        int32_t a = rot + i * 64 / rays;
        int32_t x0 = cx + gfx_cos(a) * r0 / 64;
        int32_t y0 = cy + gfx_sin(a) * r0 / 64;
        int32_t x1 = cx + gfx_cos(a) * r1 / 64;
        int32_t y1 = cy + gfx_sin(a) * r1 / 64;
        /* draw with dots so off-screen ends are clipped safely */
        int32_t dx = abs(x1 - x0), dy = abs(y1 - y0);
        int32_t n = dx > dy ? dx : dy;
        for(int32_t k = 0; k <= n; k++) {
            int32_t x = x0 + (x1 - x0) * k / (n ? n : 1);
            int32_t y = y0 + (y1 - y0) * k / (n ? n : 1);
            if(x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H) canvas_draw_dot(c, x, y);
        }
    }
}

/* Shortens buf with ".." until it fits max_w in the current font. */
void gfx_fit_str(Canvas* c, char* buf, size_t len, int32_t max_w) {
    size_t n = strlen(buf);
    if(canvas_string_width(c, buf) <= max_w) return;
    while(n > 2) {
        n--;
        buf[n] = '\0';
        char tmp[48];
        snprintf(tmp, sizeof(tmp), "%s..", buf);
        if(canvas_string_width(c, tmp) <= max_w || n <= 2) {
            strncpy(buf, tmp, len - 1);
            buf[len - 1] = '\0';
            return;
        }
    }
}

float ease_out(float t) {
    if(t <= 0) return 0;
    if(t >= 1) return 1;
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}

float ease_in_out(float t) {
    if(t <= 0) return 0;
    if(t >= 1) return 1;
    return t < 0.5f ? 4 * t * t * t : 1.0f - (-2 * t + 2) * (-2 * t + 2) * (-2 * t + 2) / 2;
}

float ease_back(float t) {
    if(t <= 0) return 0;
    if(t >= 1) return 1;
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

const Bmp* source_icon(uint8_t src) {
    switch(src) {
    case SrcSubGhz:
        return &bmp_src_subghz;
    case SrcNfc:
        return &bmp_src_nfc;
    case SrcRfid:
        return &bmp_src_rfid;
    case SrcIr:
        return &bmp_src_ir;
    default:
        return &bmp_src_ibutton;
    }
}

const Bmp* source_big(uint8_t src) {
    switch(src) {
    case SrcSubGhz:
        return &bmp_big_subghz;
    case SrcNfc:
        return &bmp_big_nfc;
    case SrcRfid:
        return &bmp_big_rfid;
    case SrcIr:
        return &bmp_big_ir;
    default:
        return &bmp_big_ibutton;
    }
}
