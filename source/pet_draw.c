/*
 * The procedural pet renderer.
 *
 * The body is an outlined ellipse with a dithered shadow crescent, so it
 * can breathe (squash/stretch), lean (shear) and hop smoothly. Eyes,
 * mouth, feet, antennas and the six adult accessories are drawn on top.
 * Mirrored 1:1 in tools/flipper_preview/ppet.py.
 */
#include "pet.h"

static void box(Canvas* c, int32_t x, int32_t y, int32_t w, int32_t h) {
    for(int32_t j = 0; j < h; j++)
        gfx_hline(c, x, y + j, w);
}

static void dot(Canvas* c, int32_t x, int32_t y) {
    if(x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H) canvas_draw_dot(c, x, y);
}

/* u8g2 line algorithm, via clipped dots */
static void line(Canvas* c, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    bool swap = false;
    int32_t dx = abs(x2 - x1), dy = abs(y2 - y1), t;
    if(dy > dx) {
        swap = true;
        t = dx, dx = dy, dy = t;
        t = x1, x1 = y1, y1 = t;
        t = x2, x2 = y2, y2 = t;
    }
    if(x1 > x2) {
        t = x1, x1 = x2, x2 = t;
        t = y1, y1 = y2, y2 = t;
    }
    int32_t err = dx >> 1;
    int32_t ystep = y2 > y1 ? 1 : -1;
    int32_t y = y1;
    for(int32_t x = x1; x <= x2; x++) {
        if(swap)
            dot(c, y, x);
        else
            dot(c, x, y);
        err -= dy;
        if(err < 0) {
            y += ystep;
            err += dx;
        }
    }
}

static void frame(Canvas* c, int32_t x, int32_t y, int32_t w, int32_t h) {
    gfx_hline(c, x, y, w);
    gfx_hline(c, x, y + h - 1, w);
    gfx_vline(c, x, y, h);
    gfx_vline(c, x + w - 1, y, h);
}

static const int8_t body_rx[4] = {8, 8, 10, 12};
static const int8_t body_ry[4] = {10, 7, 9, 10};

int32_t pet_height(uint8_t stage) {
    switch(stage) {
    case StEgg:
        return 22;
    case StBaby:
        return 23;
    case StTeen:
        return 27;
    default:
        return 33;
    }
}

static void ell_rows(Canvas* c, int32_t cx, int32_t cy, int32_t rx, int32_t ry, int32_t shear) {
    for(int32_t dy = -ry; dy <= ry; dy++) {
        int32_t w = gfx_ellipse_half(rx, ry, dy);
        int32_t ox = ry ? (shear * -dy) / ry : 0;
        gfx_hline(c, cx + ox - w, cy + dy, 2 * w + 1);
    }
}

static void body(Canvas* c, int32_t cx, int32_t cy, int32_t rx, int32_t ry, int32_t shear) {
    canvas_set_color(c, ColorBlack);
    ell_rows(c, cx, cy, rx + 1, ry + 1, shear);
    canvas_set_color(c, ColorWhite);
    ell_rows(c, cx, cy, rx, ry, shear);
    canvas_set_color(c, ColorBlack);
    int32_t k = rx >= 9 ? 2 : 1;
    for(int32_t dy = -ry; dy <= ry; dy++) {
        int32_t w = gfx_ellipse_half(rx, ry, dy);
        int32_t ox = ry ? (shear * -dy) / ry : 0;
        int32_t sw = gfx_ellipse_half(rx, ry, dy + k);
        int32_t x0 = cx + ox - w, x1 = cx + ox + w;
        int32_t sx0, sx1;
        if(sw < 0) {
            sx0 = 1;
            sx1 = 0;
        } else {
            sx0 = cx + ox - k - sw;
            sx1 = cx + ox - k + sw;
        }
        for(int32_t x = x0; x <= x1; x++) {
            if(x >= sx0 && x <= sx1) continue;
            if(((x + cy + dy) & 1) == 0) dot(c, x, cy + dy);
        }
    }
}

static void eye(Canvas* c, int32_t x, int32_t y, uint8_t kind, bool big, int8_t look) {
    int32_t w = big ? 3 : 2, h = big ? 4 : 3;
    canvas_set_color(c, ColorBlack);
    switch(kind) {
    case EyeOpen:
    case EyeSad:
    case EyeSleepy:
        x += look;
        if(kind == EyeSleepy) {
            box(c, x, y + h / 2, w, h - h / 2);
            gfx_hline(c, x, y + h / 2 - 1, w);
            return;
        }
        box(c, x, y, w, h);
        if(big) {
            canvas_set_color(c, ColorWhite);
            dot(c, x, y);
            canvas_set_color(c, ColorBlack);
        }
        break;
    case EyeBlink:
        gfx_hline(c, x - (big ? 1 : 0), y + h - 2, w + (big ? 2 : 0));
        break;
    case EyeHappy:
        if(big) {
            dot(c, x - 1, y + 2);
            dot(c, x, y + 1);
            dot(c, x + 1, y);
            dot(c, x + 2, y + 1);
            dot(c, x + 3, y + 2);
        } else {
            dot(c, x - 1, y + 2);
            dot(c, x, y + 1);
            dot(c, x + 1, y + 1);
            dot(c, x + 2, y + 2);
        }
        break;
    case EyeSleep:
        if(big) {
            dot(c, x - 1, y + 1);
            gfx_hline(c, x, y + 2, 3);
            dot(c, x + 3, y + 1);
        } else {
            dot(c, x - 1, y + 1);
            gfx_hline(c, x, y + 2, 2);
            dot(c, x + 2, y + 1);
        }
        break;
    case EyeWide:
        frame(c, x - 1, y - 1, w + 2, h + 2);
        if(big) box(c, x + 1, y + 1, 1, 2);
        break;
    case EyeDizzy:
        line(c, x - 1, y, x + w, y + h - 1);
        line(c, x - 1, y + h - 1, x + w, y);
        break;
    default:
        break;
    }
}

static void mouth(Canvas* c, int32_t x, int32_t y, uint8_t kind, bool big) {
    canvas_set_color(c, ColorBlack);
    switch(kind) {
    case MouthSmile:
        if(big) {
            dot(c, x - 2, y);
            gfx_hline(c, x - 1, y + 1, 3);
            dot(c, x + 2, y);
        } else {
            dot(c, x - 1, y);
            dot(c, x, y + 1);
            dot(c, x + 1, y);
        }
        break;
    case MouthFrown:
        if(big) {
            dot(c, x - 2, y + 1);
            gfx_hline(c, x - 1, y, 3);
            dot(c, x + 2, y + 1);
        } else {
            dot(c, x - 1, y + 1);
            dot(c, x, y);
            dot(c, x + 1, y + 1);
        }
        break;
    case MouthFlat:
        gfx_hline(c, x - 1, y, 3);
        break;
    case MouthOpen:
        if(big) {
            box(c, x - 2, y, 5, 2);
            gfx_hline(c, x - 1, y + 2, 3);
        } else {
            box(c, x - 1, y, 3, 2);
        }
        break;
    case MouthBig:
        if(big) {
            box(c, x - 2, y - 1, 5, 4);
            gfx_hline(c, x - 1, y + 3, 3);
            gfx_hline(c, x - 1, y - 2, 3);
        } else {
            box(c, x - 1, y - 1, 3, 3);
            gfx_hline(c, x - 2, y, 5);
        }
        break;
    case MouthO:
        if(big)
            frame(c, x - 1, y, 3, 3);
        else
            box(c, x - 1, y, 2, 2);
        break;
    case MouthChomp:
        gfx_hline(c, big ? x - 2 : x - 1, y + 1, big ? 5 : 3);
        dot(c, x - (big ? 3 : 2), y);
        dot(c, x + (big ? 3 : 2), y);
        break;
    case MouthTongue:
        if(big) {
            dot(c, x - 2, y);
            gfx_hline(c, x - 1, y + 1, 3);
            dot(c, x + 2, y);
            box(c, x, y + 2, 2, 2);
        } else {
            dot(c, x - 1, y);
            dot(c, x, y + 1);
            dot(c, x + 1, y);
            dot(c, x, y + 2);
        }
        break;
    default:
        break;
    }
}

static bool diamond_tips = false; /* tier 1+: antenna tips become diamonds */

static void antenna(Canvas* c, int32_t x, int32_t y, int32_t h, int32_t dx, uint8_t glow) {
    canvas_set_color(c, ColorBlack);
    int32_t tx = x + dx, ty = y - h;
    line(c, x, y, tx, ty);
    box(c, tx - 1, ty - 1, 3, 3);
    if(diamond_tips) {
        dot(c, tx, ty - 2);
        dot(c, tx - 2, ty);
        dot(c, tx + 2, ty);
    }
    if(glow >= 1) {
        dot(c, tx - 3, ty);
        dot(c, tx + 3, ty);
        dot(c, tx - 3, ty - 1);
        dot(c, tx + 3, ty - 1);
    }
    if(glow >= 2) {
        for(int32_t k = -1; k <= 1; k++) {
            dot(c, tx - 5, ty + k);
            dot(c, tx + 5, ty + k);
        }
    }
}

static void draw_egg(Canvas* c, int32_t cx, int32_t gy, const Pose* p) {
    const int32_t rx = 8, ry = 10;
    int32_t cy = gy - ry - 1;
    body(c, cx, cy, rx, ry, p->shear);
    canvas_set_color(c, ColorBlack);
    static const int8_t spots[4][2] = {{-4, -6}, {2, -7}, {-5, 3}, {3, 4}};
    for(uint8_t i = 0; i < 4; i++) {
        int32_t ox = (p->shear * -spots[i][1]) / ry;
        box(c, cx + spots[i][0] + ox, cy + spots[i][1], 2, 2);
    }
    for(int32_t i = -7; i <= 7; i++) {
        int32_t yy = cy - 1 + ((i & 1) ? 1 : 0);
        int32_t ox = (p->shear * 1) / ry;
        if(abs(i) <= gfx_ellipse_half(rx, ry, yy - cy)) dot(c, cx + i + ox, yy);
    }
    static const int8_t cr[8][4] = {
        {0, -9, 1, -7},
        {1, -7, -1, -5},
        {-1, -5, 2, -3},
        {2, -3, 0, -1},
        {-1, -5, -4, -4},
        {2, -3, 5, -4},
        {0, -1, -3, 1},
        {0, -1, 3, 2},
    };
    int32_t n = p->cracks * 2;
    if(n > 8) n = 8;
    for(int32_t i = 0; i < n; i++) {
        line(
            c,
            cx + cr[i][0] + (p->shear * -cr[i][1]) / ry,
            cy + cr[i][1],
            cx + cr[i][2] + (p->shear * -cr[i][3]) / ry,
            cy + cr[i][3]);
    }
}

static void irix_eye(Canvas* c, int32_t x, int32_t y, const Pose* p) {
    canvas_set_color(c, ColorBlack);
    canvas_draw_rbox(c, x - 7, y - 4, 15, 7, 2);
    canvas_set_color(c, ColorWhite);
    if(p->eyes == EyeBlink || p->eyes == EyeSleep) {
        gfx_hline(c, x - 5, y - 1, 11);
    } else if(p->eyes == EyeHappy) {
        dot(c, x - 2, y);
        dot(c, x - 1, y - 1);
        dot(c, x, y - 2);
        dot(c, x + 1, y - 1);
        dot(c, x + 2, y);
    } else {
        int32_t px = x - 1 + p->look * 3;
        box(c, px, y - 2, 3, 3);
        canvas_set_color(c, ColorBlack);
        dot(c, px + 1, y - 1);
    }
    canvas_set_color(c, ColorBlack);
}

static void accessories_back(Canvas* c, int32_t cx, int32_t cy, int32_t rx, int32_t ry, const Pose* p) {
    canvas_set_color(c, ColorBlack);
    switch(p->form) {
    case FormWavern:
        for(int32_t i = 0; i < 5; i++)
            gfx_vline(c, cx - rx - 1 - i, cy + 1 - i, 2 + i);
        break;
    case FormKeybo: {
        int32_t tx = cx + rx, ty = cy + ry - 3;
        box(c, tx, ty, 7, 2);
        box(c, tx + 3, ty + 2, 1, 2);
        box(c, tx + 5, ty + 2, 1, 3);
        break;
    }
    case FormOmnix:
        for(int32_t s = -1; s <= 1; s += 2) {
            int32_t x = cx + s * (rx + 1);
            line(c, x, cy - 2, x + s * 5, cy - 7);
            line(c, x + s * 5, cy - 7, x + s * 6, cy - 1);
            line(c, x + s * 6, cy - 1, x + s * 1, cy + 2);
            line(c, x + s * 3, cy - 4, x + s * 4, cy);
        }
        break;
    default:
        break;
    }
}

static void accessories_front(Canvas* c, int32_t cx, int32_t cy, int32_t rx, int32_t ry, const Pose* p) {
    UNUSED(rx);
    int32_t top = cy - ry;
    canvas_set_color(c, ColorBlack);
    switch(p->form) {
    case FormWavern: {
        uint8_t g = 1 + ((p->t / 400) & 1);
        antenna(c, cx + 2, top, 9, 1, p->glow > g ? p->glow : g);
        break;
    }
    case FormTapkin:
        for(int32_t s = -1; s <= 1; s += 2) {
            int32_t bx = cx + s * 6;
            canvas_set_color(c, ColorBlack);
            line(c, bx - s * 3, top + 2, bx + s * 1, top - 6);
            line(c, bx + s * 1, top - 6, bx + s * 4, top + 3);
            canvas_set_color(c, ColorWhite);
            line(c, bx - s * 1, top + 2, bx + s * 1, top - 3);
        }
        canvas_set_color(c, ColorBlack);
        frame(c, cx - 2, cy + 4, 5, 4);
        dot(c, cx, cy + 5);
        break;
    case FormCoilbit:
        for(int32_t s = -1; s <= 1; s += 2) {
            int32_t hx = cx + s * 7, hy = top - 1;
            canvas_set_color(c, ColorBlack);
            canvas_draw_disc(c, hx, hy, 3);
            canvas_set_color(c, ColorWhite);
            canvas_draw_disc(c, hx, hy, 2);
            canvas_set_color(c, ColorBlack);
            canvas_draw_disc(c, hx, hy, 1);
            canvas_set_color(c, ColorWhite);
            dot(c, hx, hy);
        }
        canvas_set_color(c, ColorBlack);
        canvas_draw_circle(c, cx, cy + 5, 2);
        break;
    case FormIrix:
        antenna(c, cx, top, 4, 0, p->glow);
        break;
    case FormKeybo:
        canvas_draw_disc(c, cx, top - 2, 4);
        canvas_set_color(c, ColorWhite);
        canvas_draw_disc(c, cx, top - 2, 2);
        canvas_set_color(c, ColorBlack);
        dot(c, cx, top - 2);
        box(c, cx - 2, top + 1, 5, 1);
        break;
    case FormOmnix:
        gfx_bmp(c, cx - 4, top - 4, &bmp_crown);
        break;
    default:
        break;
    }
    canvas_set_color(c, ColorBlack);
}

static void foot(Canvas* c, int32_t x, int32_t y, int32_t w) {
    canvas_set_color(c, ColorBlack);
    box(c, x, y, w, 2);
}

/* ---------------------------------------------------------- tier effects */

static void wings(Canvas* c, int32_t cx, int32_t cy, int32_t rx, uint32_t t) {
    int32_t f = gfx_sin((int32_t)(t / 70)) * 3 / 64; /* flap */
    canvas_set_color(c, ColorBlack);
    for(int32_t s = -1; s <= 1; s += 2) {
        int32_t bx = cx + s * (rx - 2), by = cy - 2;
        line(c, bx, by, bx + s * 10, by - 7 + f);
        line(c, bx + s * 10, by - 7 + f, bx + s * 13, by - 2 + f);
        line(c, bx + s * 13, by - 2 + f, bx + s * 10, by + 2 + f / 2);
        line(c, bx + s * 10, by + 2 + f / 2, bx + s * 2, by + 4);
        line(c, bx + s * 4, by - 2, bx + s * 10, by - 3 + f);
        line(c, bx + s * 4, by + 1, bx + s * 9, by + f / 2);
    }
}

static void aura(Canvas* c, int32_t cx, int32_t cy, int32_t rx, int32_t ry, uint32_t t, int32_t grow) {
    int32_t ph = (int32_t)(t / 60);
    int32_t ax = rx + 5 + grow, ay = ry + 4 + grow;
    canvas_set_color(c, ColorBlack);
    for(int32_t a = 0; a < 64; a++) {
        if(((a + ph) & 3) >= 2) continue;
        dot(c, cx + gfx_cos(a) * ax / 64, cy + gfx_sin(a) * ay / 64);
    }
}

/* signal orbs circling the pet; front == only the ones in front of it */
static void satellites(Canvas* c, int32_t cx, int32_t cy, int32_t rx, int32_t ry, uint32_t t, uint8_t n, bool front) {
    canvas_set_color(c, ColorBlack);
    for(uint8_t k = 0; k < n; k++) {
        int32_t a = (int32_t)(t / 28) + k * 64 / n;
        bool in_front = gfx_sin(a) > 0;
        if(in_front != front) continue;
        int32_t x = cx + gfx_cos(a) * (rx + 9) / 64;
        int32_t y = cy + gfx_sin(a) * (ry / 2 + 3) / 64 - 2;
        if(in_front) {
            box(c, x - 1, y - 1, 3, 3);
        } else {
            dot(c, x, y);
            dot(c, x + 1, y);
        }
    }
}

static void halo(Canvas* c, int32_t cx, int32_t top, uint32_t t) {
    int32_t y = top - 16 + gfx_sin((int32_t)(t / 120)) * 1 / 64;
    if(y < 12) y = 12;
    canvas_set_color(c, ColorBlack);
    for(int32_t a = 0; a < 64; a++)
        dot(c, cx + gfx_cos(a) * 7 / 64, y + gfx_sin(a) * 2 / 64);
}

static void legend_sparkles(Canvas* c, int32_t cx, int32_t cy, int32_t rx, int32_t ry, uint32_t t) {
    uint32_t slot = t / 260;
    for(uint32_t k = 0; k < 3; k++) {
        uint32_t h = (slot * 3 + k) * 2654435761u;
        h ^= h >> 13;
        int32_t a = (int32_t)(h % 64);
        int32_t r = 4 + (int32_t)((h >> 8) % 6);
        int32_t x = cx + gfx_cos(a) * (rx + r) / 64, y = cy + gfx_sin(a) * (ry + r) / 64;
        dot(c, x, y);
        if(((t / 130) + k) & 1) {
            dot(c, x - 1, y);
            dot(c, x + 1, y);
            dot(c, x, y - 1);
            dot(c, x, y + 1);
        }
    }
}

static uint8_t sat_count(uint8_t tier) {
    if(tier >= 10) return 3;
    if(tier >= 6) return 2;
    if(tier >= 5) return 1;
    return 0;
}

void draw_pet(Canvas* c, int32_t cx, int32_t gy, const Pose* p) {
    if(p->stage == StEgg) {
        draw_egg(c, cx, gy, p);
        return;
    }
    int32_t rx = body_rx[p->stage] + p->squash;
    int32_t ry = body_ry[p->stage] - p->squash;
    bool big = p->stage == StAdult;
    int32_t cy = gy - 2 - ry - p->lift;
    int32_t sh = p->shear;

    uint8_t sats = sat_count(p->tier);
    diamond_tips = p->tier >= 1;
    if(p->tier >= 9) wings(c, cx, cy, rx, p->t);
    if(p->tier >= 7) aura(c, cx, cy, rx, ry, p->t, 0);
    if(sats) satellites(c, cx, cy, rx, ry, p->t, sats, false);
    if(p->stage == StAdult) accessories_back(c, cx, cy, rx, ry, p);

    int32_t fw = p->stage == StBaby ? 3 : 4;
    int32_t fx = rx / 2 + 1;
    foot(c, cx - fx - fw / 2, gy - 2 - p->lift - (p->step == 1 ? 1 : 0), fw);
    foot(c, cx + fx - fw / 2 + (fw & 1), gy - 2 - p->lift - (p->step == 2 ? 1 : 0), fw);

    body(c, cx, cy, rx, ry, sh);

    if(p->stage >= StTeen) {
        int32_t ay = cy + 2;
        canvas_set_color(c, ColorBlack);
        box(c, cx - rx - 2, ay, 2, 3);
        box(c, cx + rx + 1, ay, 2, 3);
    }

    int32_t top = cy - ry;
    if(p->stage == StBaby) {
        antenna(c, cx + sh, top, 5, sh / 2, p->glow);
    } else if(p->stage == StTeen) {
        antenna(c, cx - 4 + sh, top + 1, 5, -2 + sh / 2, p->glow);
        antenna(c, cx + 4 + sh, top + 1, 5, 2 + sh / 2, 0);
    } else {
        accessories_front(c, cx, cy, rx, ry, p);
    }

    int32_t fy = cy - ry / 4 - 1;
    int32_t fsh = (sh * (cy - fy)) / ry;
    int32_t ex, my;
    if(big) {
        ex = 5;
        if(p->form == FormIrix) {
            irix_eye(c, cx + fsh, fy, p);
        } else {
            eye(c, cx + fsh - ex - 2, fy - 2, p->eyes, true, p->look);
            eye(c, cx + fsh + ex - 1, fy - 2, p->eyes, true, p->look);
        }
        my = fy + 4;
    } else {
        ex = p->stage == StBaby ? 3 : 4;
        eye(c, cx + fsh - ex - 1, fy - 1, p->eyes, false, p->look);
        eye(c, cx + fsh + ex, fy - 1, p->eyes, false, p->look);
        my = fy + 3;
    }
    mouth(c, cx + fsh + (p->eyes == EyeOpen ? p->look : 0), my, p->mouth, big);

    canvas_set_color(c, ColorBlack);
    if(p->blush) {
        int32_t bx = ex + (big ? 4 : 3);
        int32_t by = my - 1;
        for(int32_t s = -1; s <= 1; s += 2) {
            int32_t x0 = cx + fsh + s * bx;
            dot(c, x0 - 1, by);
            dot(c, x0 + 1, by);
            dot(c, x0, by + 1);
        }
    }
    if(p->tear) {
        int32_t tx = cx + fsh + ex + 2;
        int32_t ty = fy + 2 + (int32_t)((p->t / 120) % 5);
        dot(c, tx, ty);
        dot(c, tx, ty + 1);
    }
    if(p->tier >= 3 && p->form != FormTapkin && p->form != FormCoilbit) {
        /* tier 3+: a little signal-wave badge on the belly */
        int32_t by = cy + ry / 2 + 1;
        dot(c, cx - 2, by);
        dot(c, cx - 1, by - 1);
        dot(c, cx, by);
        dot(c, cx + 1, by + 1);
        dot(c, cx + 2, by);
    }
    if(sats) satellites(c, cx, cy, rx, ry, p->t, sats, true);
    if(p->tier >= 8) halo(c, cx, top, p->t);
    if(p->tier >= 10) legend_sparkles(c, cx, cy, rx, ry, p->t);
}

/* Same pet, forced to another stage (mini games use the baby size). */
void draw_pet_sized(Canvas* c, int32_t cx, int32_t gy, const Pose* p, uint8_t stage) {
    Pose q = *p;
    q.stage = stage;
    q.tier = 0;
    draw_pet(c, cx, gy, &q);
}

/* y of the mouth for a pose standing on gy (food flies there). */
int32_t pet_mouth_y(const Pose* p, int32_t gy) {
    if(p->stage == StEgg) return gy - 11;
    int32_t ry = body_ry[p->stage] - p->squash;
    int32_t cy = gy - 2 - ry - p->lift;
    int32_t fy = cy - ry / 4 - 1;
    return fy + (p->stage == StAdult ? 4 : 3);
}

void pose_default(Pose* p, App* app) {
    memset(p, 0, sizeof(Pose));
    p->stage = app->save->stage;
    p->form = app->save->form;
    p->eyes = EyeOpen;
    p->mouth = MouthSmile;
    p->tier = app->save->stage == StEgg ? 0 : pet_tier(app->save->level);
    p->t = app->now;
}
