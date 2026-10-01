/*
 * Hunting: pick a source, listen, and watch the pet eat what it catches.
 *
 * The catch cinematic has four beats:
 *   lock-on   screen flashes, brackets snap onto the signal
 *   fly       the signal orb arcs from the source into the pet's mouth
 *   chomp     munch munch, crumbs fly
 *   card      a result card slides up: new species? rarity? gains?
 */
#include "pet.h"

#define PET_X 108
#define PET_GY 63
#define ZONE_W 90

#define T_WARM 1100 /* "tuning in" intro of every scan */
#define T_LOCK 350
#define T_FLY 1000
#define T_CHOMP 1750

static const char* const src_desc[SrcCount] = {
    "Car keys, gates, bells",
    "Cards, tags, phones",
    "Door fobs, pet chips",
    "TV & AC remotes",
    "Dallas door keys",
};

static const char* const band_names[BandCount] = {"433.92", "868.35", "315.00", "433.92"};

static uint32_t rnd(uint32_t n) {
    return furi_hal_random_get() % n;
}

/* centre of the big graphic of each scan screen */
static void graphic_center(uint8_t src, int32_t* x, int32_t* y) {
    if(src == SrcSubGhz) {
        *x = 24;
        *y = 37;
    } else {
        *x = 45;
        *y = 28;
    }
}

/* ---------------------------------------------------------- picker */

void huntpick_enter(App* app) {
    app->hunt.sel_f = app->hunt.sel;
    app->hunt.ext_ready = radio_probe_external();
    parts_clear(app);
}

void huntpick_input(App* app, InputEvent* ev) {
    HuntState* h = &app->hunt;
    if(ev->type != InputTypeShort && ev->type != InputTypeRepeat) return;
    if(ev->key == InputKeyLeft) {
        h->sel = (h->sel + SrcCount - 1) % SrcCount;
        if(h->sel_f < 0.5f && h->sel == SrcCount - 1) h->sel_f += SrcCount;
        fx_click(app);
        if(h->sel == SrcSubGhz) h->ext_ready = radio_probe_external(); /* board plugged in meanwhile? */
    } else if(ev->key == InputKeyRight) {
        h->sel = (h->sel + 1) % SrcCount;
        if(h->sel == 0) h->sel_f -= SrcCount;
        fx_click(app);
        if(h->sel == SrcSubGhz) h->ext_ready = radio_probe_external();
    } else if(ev->key == InputKeyOk && ev->type == InputTypeShort) {
        fx_click(app);
        app_goto(app, SceneScan, TransStatic);
    } else if(ev->key == InputKeyBack && ev->type == InputTypeShort) {
        fx_back(app);
        app_goto(app, SceneHome, TransIris);
    }
}

void huntpick_update(App* app, uint32_t dt) {
    HuntState* h = &app->hunt;
    float k = (float)dt * 0.016f;
    if(k > 1) k = 1;
    h->sel_f += ((float)h->sel - h->sel_f) * k;
    if(h->sel_f > h->sel - 0.01f && h->sel_f < h->sel + 0.01f) h->sel_f = h->sel;
}

void huntpick_draw(App* app, Canvas* c) {
    HuntState* h = &app->hunt;
    uint32_t t = app->now;
    canvas_set_color(c, ColorBlack);

    for(int32_t i = -1; i <= (int32_t)SrcCount; i++) {
        int32_t src = (i + SrcCount) % SrcCount;
        float o = (float)i - h->sel_f;
        if(o < -2.5f || o > 2.5f) continue;
        int32_t x = 64 + (int32_t)(o * 44.0f) - 12;
        float ao = o < 0 ? -o : o;
        int32_t y = 4 + (int32_t)(ao * 5.0f);
        if(ao < 0.5f) y += (gfx_sin(t / 30) * 1) / 64;
        if(x < 0 || x > 104) continue;
        gfx_bmp(c, x, y, source_big(src));
        if(ao > 0.5f) {
            canvas_set_color(c, ColorWhite);
            gfx_dither(c, x, y, 24, 24, 0);
            canvas_set_color(c, ColorBlack);
        }
    }
    /* frame around the selected one */
    canvas_draw_rframe(c, 47, 1, 34, 31, 4);
    if((t / 400) & 1) {
        gfx_bmp(c, 2, 13, &bmp_arrow_l);
        gfx_bmp(c, 123, 13, &bmp_arrow_r);
    }

    uint8_t s = h->sel;
    canvas_set_font(c, FontPrimary);
    gfx_str_center(c, 64, 42, source_names[s]);
    canvas_set_font(c, FontSecondary);
    gfx_str_center(c, 64, 52, src_desc[s]);

    char buf[48];
    if(s == SrcSubGhz && h->ext_ready) {
        /* a board is plugged in: tell the player it will be used */
        canvas_draw_rbox(c, 0, 56, 62, 8, 2);
        canvas_set_color(c, ColorWhite);
        canvas_draw_str(c, 2, 63, "Board: CC1101");
        canvas_set_color(c, ColorBlack);
    } else {
        snprintf(buf, sizeof(buf), "Dex %u/%u", species_found(app, s), species_total(app, s));
        canvas_draw_str(c, 1, 63, buf);
    }
    gfx_button_hint(c, 103, 63, &bmp_btn_ok, "Go");
    gfx_hline(c, 0, 55, 128);
}

/* ---------------------------------------------------------- scan */

void scan_enter(App* app) {
    HuntState* h = &app->hunt;
    h->scan_t0 = app->now;
    memset(h->rssi_hist, 0, sizeof(h->rssi_hist));
    memset(h->blips, 0, sizeof(h->blips));
    h->rssi_i = 0;
    h->err = !radio_start(app->radio, h->sel, app->save->set.band);
    h->ext_announced = false;
    fx_scan_led(app, h->sel, true);
    parts_clear(app);
    if(radio_external(app->radio) && !(app->save->gadgets & 1)) {
        /* first time with a CC1101 module on the GPIO: a gadget bonus */
        app->save->gadgets |= 1;
        state_add_xp(app, 60);
        state_save(app);
        h->ext_announced = true;
    }
}

void scan_exit(App* app) {
    radio_stop(app->radio);
    fx_scan_led(app, 0, false);
}

void scan_radio_event(App* app) {
    Catch c;
    if(!radio_take(app->radio, &c)) return;
    CatchState* cs = &app->cat_st;
    memset(cs, 0, sizeof(CatchState));
    catch_digest(app, &c, cs);
    cs->t0 = app->now;
    app->scene = SceneCatch;
    fx_catch(app, c.src);
    parts_clear(app);
}

void scan_input(App* app, InputEvent* ev) {
    HuntState* h = &app->hunt;
    if(ev->type != InputTypeShort && ev->type != InputTypeRepeat) return;
    if(ev->key == InputKeyBack && ev->type == InputTypeShort) {
        fx_back(app);
        app_goto(app, SceneHome, TransIris);
    } else if(h->sel == SrcSubGhz && (ev->key == InputKeyLeft || ev->key == InputKeyRight)) {
        uint8_t b = app->save->set.band;
        b = ev->key == InputKeyLeft ? (b + BandCount - 1) % BandCount : (b + 1) % BandCount;
        app->save->set.band = b;
        radio_set_band(app->radio, b);
        fx_click(app);
    } else if(ev->key == InputKeyOk && ev->type == InputTypeShort) {
        static const char* const tips[SrcCount] = {
            "Try a car key or doorbell",
            "Bank, transit, hotel cards",
            "Door fobs, office badges",
            "Any TV or AC remote works",
            "Intercom keys (Dallas)",
        };
        toast_show(app, tips[h->sel], 0, 2200);
    }
}

void scan_update(App* app, uint32_t dt) {
    HuntState* h = &app->hunt;
    uint32_t el = app->now - h->scan_t0;
    uint32_t prev = el > dt ? el - dt : 0;
    if(prev < T_WARM && el >= T_WARM) {
        if(h->ext_announced) {
            toast_show(app, "New gadget! +60 XP", 0, 2400);
            fx_badge(app);
        } else if(h->sel == SrcSubGhz) {
            toast_show(app, "Press a remote nearby!", 0, 2200);
        }
    }
    if(h->sel == SrcSubGhz) {
        if(app->now - h->rssi_t > 70) {
            h->rssi_t = app->now;
            h->rssi_hist[h->rssi_i] = radio_rssi(app->radio);
            h->rssi_i = (h->rssi_i + 1) % 21;
            if(radio_rssi(app->radio) > -72.0f) {
                for(uint8_t i = 0; i < 6; i++) {
                    if(h->blips[i][2] == 0) {
                        int32_t a = rnd(64), r = 4 + rnd(12);
                        h->blips[i][0] = (uint8_t)(24 + gfx_cos(a) * r / 64);
                        h->blips[i][1] = (uint8_t)(37 + gfx_sin(a) * r / 64);
                        h->blips[i][2] = 14;
                        break;
                    }
                }
            }
            for(uint8_t i = 0; i < 6; i++)
                if(h->blips[i][2]) h->blips[i][2]--;
        }
    }
    if(h->sel == SrcIbutton) {
        /* sparks when the key touches the pads */
        int32_t dx = (gfx_sin((int32_t)(app->now / 25)) * 4) / 64;
        bool sensing = radio_status(app->radio) == RadioSensing;
        if((dx >= 3 || sensing) && rnd(3) == 0)
            parts_spawn(app, PartSpark, 64, (float)(24 + (int32_t)rnd(8)), 0, 0, 220, false);
    }
    static RadioStatus last = RadioIdle;
    RadioStatus st = radio_status(app->radio);
    if(st == RadioSensing && last != RadioSensing) fx_sense(app);
    last = st;
}

/* dotted ring clipped to a box */
static void ring(Canvas* c, int32_t cx, int32_t cy, int32_t r, int32_t y0, int32_t y1, uint8_t step) {
    for(int32_t a = 0; a < 128; a += step) {
        int32_t x = cx + (gfx_cos(a / 2) * r) / 64;
        int32_t y = cy + (gfx_sin(a / 2) * r) / 64;
        if(x >= 0 && x < ZONE_W && y >= y0 && y <= y1) canvas_draw_dot(c, x, y);
    }
}

static void draw_header(App* app, Canvas* c, uint8_t src) {
    canvas_set_color(c, ColorBlack);
    gfx_bmp(c, 1, 1, source_icon(src));
    canvas_set_font(c, FontPrimary);
    canvas_draw_str(c, 11, 8, source_names[src]);
    canvas_set_font(c, FontSecondary);
    uint32_t sec = (app->now - app->hunt.scan_t0) / 1000;
    char buf[12];
    snprintf(buf, sizeof(buf), "%lu:%02lu", (unsigned long)(sec / 60 % 100), (unsigned long)(sec % 60));
    gfx_str_right(c, 127, 8, buf);
    /* listening dots */
    for(uint8_t i = 0; i < 3; i++)
        if(((app->now / 250) % 4) > i) canvas_draw_box(c, 96 + i * 3, 6, 2, 2);
    gfx_hline(c, 0, 10, 128);
}

static void draw_subghz(App* app, Canvas* c, bool frozen) {
    HuntState* h = &app->hunt;
    uint32_t t = frozen ? app->cat_st.t0 : app->now;
    int32_t cx = 24, cy = 37, r = 17;
    canvas_draw_circle(c, cx, cy, r);
    ring(c, cx, cy, 9, 0, 63, 8);
    canvas_draw_dot(c, cx, cy);
    for(int32_t i = 3; i <= r - 2; i += 3) {
        canvas_draw_dot(c, cx - i, cy);
        canvas_draw_dot(c, cx + i, cy);
        canvas_draw_dot(c, cx, cy - i);
        canvas_draw_dot(c, cx, cy + i);
    }
    int32_t a = (int32_t)(t / 22);
    canvas_draw_line(c, cx, cy, cx + gfx_cos(a) * (r - 1) / 64, cy + gfx_sin(a) * (r - 1) / 64);
    for(int32_t k = 1; k <= 3; k++) {
        int32_t aa = a - k;
        for(int32_t d = 2 + k; d < r; d += 2) {
            canvas_draw_dot(c, cx + gfx_cos(aa) * d / 64, cy + gfx_sin(aa) * d / 64);
        }
    }
    for(uint8_t i = 0; i < 6; i++) {
        if(h->blips[i][2] && ((h->blips[i][2] > 6) || (t / 100) & 1))
            canvas_draw_box(c, h->blips[i][0] - 1, h->blips[i][1] - 1, 3, 3);
    }

    /* frequency */
    uint8_t band = app->save->set.band;
    char buf[48];
    uint32_t f = radio_freq(app->radio);
    if(f)
        snprintf(buf, sizeof(buf), "%lu.%02lu", (unsigned long)(f / 1000000), (unsigned long)(f % 1000000) / 10000);
    else
        snprintf(buf, sizeof(buf), "%s", band_names[band]);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str(c, 46, 22, buf);
    canvas_set_font(c, FontSecondary);
    canvas_draw_str(c, 46, 31, "MHz");
    if(band == BandHop) {
        canvas_draw_rbox(c, 68, 24, 19, 9, 2);
        canvas_set_color(c, ColorWhite);
        canvas_draw_str(c, 70, 31, "ALL");
        canvas_set_color(c, ColorBlack);
        /* where the sweep is right now: 300 ... 928 MHz */
        for(int32_t x = 46; x <= 88; x += 2)
            canvas_draw_dot(c, x, 35);
        canvas_draw_box(c, 46 + radio_sweep_pos(app->radio) * 40 / 255, 34, 3, 3);
    }

    /* signal strength history */
    int32_t gx = 46, gy = 52;
    for(uint8_t i = 0; i < 21; i++) {
        float v = h->rssi_hist[(h->rssi_i + i) % 21];
        int32_t bh = v == 0 ? 0 : (int32_t)((v + 100.0f) * 16.0f / 60.0f);
        if(bh < 1) bh = 1;
        if(bh > 15) bh = 15;
        canvas_draw_box(c, gx + i * 2, gy - bh + 1, 1, bh);
    }
    gfx_bmp(c, 46, 57, &bmp_arrow_l);
    gfx_bmp(c, 51, 57, &bmp_arrow_r);
    canvas_draw_str(c, 56, 62, "Band");
}

static void draw_hint2(Canvas* c, const char* a, const char* b) {
    canvas_set_font(c, FontSecondary);
    gfx_str_center(c, 45, 54, a);
    gfx_str_center(c, 45, 63, b);
}

static void draw_other(App* app, Canvas* c, uint8_t src, bool frozen) {
    uint32_t t = frozen ? app->cat_st.t0 : app->now;
    bool sensing = !frozen && radio_status(app->radio) == RadioSensing;
    int32_t cx, cy;
    graphic_center(src, &cx, &cy);
    int32_t ix = cx - 12, iy = cy - 12;
    uint32_t speed = sensing ? 10 : 28;

    switch(src) {
    case SrcNfc:
        for(int32_t k = 0; k < 3; k++) {
            int32_t r = 15 + (int32_t)((t / speed + k * 9) % 27);
            ring(c, cx, cy, r, 12, 45, 3);
        }
        gfx_bmp(c, ix, iy + (gfx_sin(t / 40) * 1) / 64, &bmp_big_nfc);
        if(sensing)
            draw_hint2(c, "Card found!", "Reading...");
        else
            draw_hint2(c, "Hold a card to", "Flipper's back");
        break;
    case SrcRfid:
        for(int32_t k = 0; k < 3; k++) {
            int32_t g = (int32_t)((t / speed + k * 7) % 21);
            int32_t w = 26 + g * 2, hh = 26 + g;
            int32_t x0 = cx - w / 2, y0 = cy - hh / 2;
            if(y0 >= 12 && y0 + hh <= 46 && x0 >= 0 && x0 + w <= ZONE_W)
                canvas_draw_rframe(c, x0, y0, w, hh, 4);
            else {
                for(int32_t x = x0; x < x0 + w; x += 3) {
                    if(x < 0 || x >= ZONE_W) continue;
                    if(y0 >= 12) canvas_draw_dot(c, x, y0);
                    if(y0 + hh - 1 <= 46) canvas_draw_dot(c, x, y0 + hh - 1);
                }
                for(int32_t y = y0; y < y0 + hh; y += 3) {
                    if(y < 12 || y > 46) continue;
                    if(x0 >= 0) canvas_draw_dot(c, x0, y);
                    if(x0 + w - 1 < ZONE_W) canvas_draw_dot(c, x0 + w - 1, y);
                }
            }
        }
        gfx_bmp(c, ix, iy, &bmp_big_rfid);
        if(sensing)
            draw_hint2(c, "Tag found!", "Hold still...");
        else
            draw_hint2(c, "Hold a 125 kHz tag", "to Flipper's back");
        break;
    case SrcIr: {
        gfx_bmp(c, ix - 14, iy, &bmp_big_ir);
        /* light pulses travelling right */
        for(int32_t k = 0; k < 4; k++) {
            int32_t x = 36 + (int32_t)((t / 18 + k * 12) % 48);
            if(x + 4 < ZONE_W) {
                gfx_hline(c, x, cy - 3, 4);
                gfx_hline(c, x + 2, cy + 3, 4);
                gfx_hline(c, x + 1, cy, 5);
            }
        }
        draw_hint2(c, "Aim a remote at the", "top, press a button");
        break;
    }
    default: {
        int32_t dx = (gfx_sin(t / 25) * 4) / 64;
        gfx_bmp(c, ix - 6 + dx, iy, &bmp_big_ibutton);
        /* contact pads */
        canvas_draw_box(c, 66, cy - 7, 3, 14);
        canvas_draw_box(c, 71, cy - 4, 3, 8);
        draw_hint2(c, "Touch a key to the", "iButton contacts");
        break;
    }
    }
}

static void scan_pet_pose(App* app, Pose* p, bool sensing) {
    pose_default(p, app);
    p->look = -1;
    p->glow = (uint8_t)((app->now / 300) % 3);
    if(sensing) {
        p->eyes = EyeWide;
        p->mouth = MouthBig;
        p->glow = 2;
    } else {
        p->mouth = (app->now / 2000) % 3 == 0 ? MouthO : MouthSmile;
        if((app->now % 3800) < 130) p->eyes = EyeBlink;
    }
}

/* Intro of every scan: rings collapse onto the source icon. */
static void draw_warmup(App* app, Canvas* c, uint32_t el) {
    uint8_t src = app->hunt.sel;
    canvas_set_color(c, ColorWhite);
    canvas_draw_box(c, 0, 11, 128, 53);
    canvas_set_color(c, ColorBlack);
    for(int32_t k = 0; k < 3; k++) {
        int32_t r = 40 - (int32_t)((el / 12 + k * 13) % 40);
        if(r < 15) continue;
        for(int32_t a = 0; a < 64; a++) {
            int32_t x = 64 + gfx_cos(a) * r / 64, y = 30 + gfx_sin(a) * r * 3 / 4 / 64;
            if(y > 11 && y < 50 && x >= 0 && x < 128 && ((a + k) & 1)) canvas_draw_dot(c, x, y);
        }
    }
    canvas_set_color(c, ColorWhite);
    canvas_draw_disc(c, 64, 30, 15);
    canvas_set_color(c, ColorBlack);
    canvas_draw_circle(c, 64, 30, 15);
    gfx_bmp(c, 52, 18, source_big(src));
    canvas_set_font(c, FontSecondary);
    const char* msg = "Tuning in";
    if(radio_external(app->radio)) msg = "Module found: CC1101";
    char buf[32];
    snprintf(buf, sizeof(buf), "%s%s", msg, (el / 250) % 4 == 0 ? "" : ((el / 250) % 4 == 1 ? "." : ((el / 250) % 4 == 2 ? ".." : "...")));
    int32_t w = canvas_string_width(c, msg);
    canvas_draw_str(c, 64 - w / 2, 57, buf);
    /* progress */
    int32_t pw = (int32_t)(el * 60 / T_WARM);
    if(pw > 60) pw = 60;
    gfx_hline(c, 34, 62, pw);
}

void scan_draw(App* app, Canvas* c) {
    uint8_t src = app->hunt.sel;
    draw_header(app, c, src);
    if(radio_external(app->radio)) {
        canvas_draw_rbox(c, 62, 0, 19, 10, 2);
        canvas_set_color(c, ColorWhite);
        canvas_set_font(c, FontSecondary);
        canvas_draw_str(c, 64, 8, "EXT");
        canvas_set_color(c, ColorBlack);
    }
    uint32_t wel = app->now - app->hunt.scan_t0;
    if(wel < T_WARM) {
        draw_warmup(app, c, wel);
        return;
    }
    if(app->hunt.err) {
        canvas_set_font(c, FontSecondary);
        gfx_str_center(c, 45, 34, "Radio not available");
    } else if(src == SrcSubGhz) {
        draw_subghz(app, c, false);
    } else {
        draw_other(app, c, src, false);
    }
    Pose p;
    scan_pet_pose(app, &p, radio_status(app->radio) == RadioSensing);
    draw_pet(c, PET_X, PET_GY, &p);
    parts_draw(app, c);
}

/* ---------------------------------------------------------- catch */

static void bracket(Canvas* c, int32_t x, int32_t y, int32_t dx, int32_t dy) {
    gfx_hline(c, dx > 0 ? x : x - 4, y, 5);
    gfx_vline(c, x, dy > 0 ? y : y - 4, 5);
}

static void orb(Canvas* c, int32_t x, int32_t y, uint32_t t, uint8_t src) {
    canvas_set_color(c, ColorBlack);
    if(x < 3 || y < 3 || x > 124 || y > 60) return;
    canvas_draw_disc(c, x, y, 2);
    int32_t a = (int32_t)(t / 20);
    for(int32_t k = 0; k < 2; k++) {
        int32_t aa = a + k * 32;
        int32_t ox = x + gfx_cos(aa) * 5 / 64, oy = y + gfx_sin(aa) * 3 / 64;
        if(ox >= 0 && oy >= 0) canvas_draw_dot(c, ox, oy);
    }
    UNUSED(src);
}

static void catch_pose(App* app, Pose* p) {
    CatchState* cs = &app->cat_st;
    uint32_t el = app->now - cs->t0;
    pose_default(p, app);
    p->look = -1;
    if(el < T_LOCK) {
        p->eyes = EyeWide;
        p->mouth = MouthO;
    } else if(el < T_FLY) {
        p->eyes = EyeWide;
        p->mouth = MouthBig;
        p->glow = 2;
    } else if(el < T_CHOMP) {
        uint32_t k = (el - T_FLY) / 120;
        if(cs->kind == CatchStale) {
            p->eyes = EyeSleepy;
            p->mouth = k & 1 ? MouthFlat : MouthChomp;
        } else {
            p->eyes = EyeHappy;
            p->mouth = k & 1 ? MouthBig : MouthChomp;
            p->squash = k & 1 ? 1 : 0;
        }
    } else {
        if(cs->kind == CatchStale) {
            p->eyes = EyeSleepy;
            p->mouth = MouthFrown;
        } else {
            p->eyes = EyeHappy;
            p->mouth = MouthOpen;
            p->blush = true;
            uint32_t k = (el - T_CHOMP) % 900;
            p->lift = k < 300 ? (int8_t)(gfx_sin((int32_t)(k * 32 / 300)) * 5 / 64) : 0;
        }
    }
}

void catch_update(App* app, uint32_t dt) {
    CatchState* cs = &app->cat_st;
    uint32_t el = app->now - cs->t0;
    uint32_t prev = el > dt ? el - dt : 0;
    int32_t my = pet_mouth_y(&(Pose){.stage = app->save->stage}, PET_GY);
    if(prev < T_FLY && el >= T_FLY) {
        if(cs->kind == CatchStale) {
            fx_stale(app);
        } else {
            fx_chomp(app);
            parts_burst(app, PartCrumb, PET_X - 4, (float)my, 6, 40, true);
        }
    }
    if(prev < T_CHOMP && el >= T_CHOMP) {
        cs->card = true;
        if(cs->kind == CatchNewSpecies) {
            fx_new_species(app);
            parts_burst(app, PartStar, PET_X, (float)(my - 10), 6, 34, false);
        } else if(cs->kind != CatchStale) {
            parts_spawn(app, PartHeart, PET_X - 6, (float)(my - 8), -3, -14, 1200, false);
            parts_spawn(app, PartHeart, PET_X + 6, (float)(my - 10), 3, -12, 1200, false);
        }
    }
    if(cs->kind != CatchStale && el > T_CHOMP && (el / 700) != (prev / 700))
        parts_spawn(app, PartHeart, PET_X + (float)((int32_t)rnd(13) - 6), (float)(my - 12), 0, -12, 1000, false);
}

void catch_input(App* app, InputEvent* ev) {
    CatchState* cs = &app->cat_st;
    if(ev->type != InputTypeShort) return;
    uint32_t el = app->now - cs->t0;
    if(el < T_CHOMP + 350) return;
    if(ev->key == InputKeyOk) {
        fx_click(app);
        parts_clear(app);
        if(app->pending_levels || app->pending_evolve) {
            celeb_start(app, SceneScan);
        } else {
            app->scene = SceneScan;
            radio_resume(app->radio);
        }
    } else if(ev->key == InputKeyBack) {
        fx_back(app);
        parts_clear(app);
        if(app->pending_levels || app->pending_evolve) {
            scan_exit(app);
            celeb_start(app, SceneHome);
        } else {
            app_goto(app, SceneHome, TransIris);
        }
    }
}

static void draw_card(App* app, Canvas* c, int32_t y) {
    CatchState* cs = &app->cat_st;
    uint32_t el = app->now - cs->t0;
    const int32_t x = 1, w = 88, h = 52;
    canvas_set_color(c, ColorWhite);
    canvas_draw_rbox(c, x - 1, y - 1, w + 2, h + 2, 4);
    canvas_set_color(c, ColorBlack);
    canvas_draw_rframe(c, x, y, w, h, 4);

    /* banner */
    static const char* const banner[4] = {"NEW SPECIES!", "NEW SIGNAL", "SNACK TIME", "OLD NEWS"};
    canvas_draw_rbox(c, x + 2, y + 2, w - 4, 11, 3);
    canvas_set_color(c, ColorWhite);
    canvas_set_font(c, FontPrimary);
    gfx_str_center(c, x + w / 2, y + 10, banner[cs->kind % 4]);
    if(cs->kind == CatchNewSpecies && ((el / 150) & 1)) {
        canvas_draw_dot(c, x + 6, y + 5);
        canvas_draw_dot(c, x + w - 7, y + 9);
    }
    canvas_set_color(c, ColorBlack);

    /* species name */
    char name[28];
    snprintf(name, sizeof(name), "%s", cs->c.proto);
    canvas_set_font(c, FontPrimary);
    if(canvas_string_width(c, name) > w - 8) canvas_set_font(c, FontSecondary);
    gfx_fit_str(c, name, sizeof(name), w - 8);
    gfx_str_center(c, x + w / 2, y + 22, name);

    /* source, dex number, rarity */
    canvas_set_font(c, FontSecondary);
    gfx_bmp(c, x + 4, y + 25, source_icon(cs->c.src));
    char no[12];
    if(cs->dex_no > 0)
        snprintf(no, sizeof(no), "#%02d", cs->dex_no);
    else
        snprintf(no, sizeof(no), "#??");
    canvas_draw_str(c, x + 14, y + 31, no);
    gfx_stars(c, x + w - 33, y + 26, cs->rarity);

    if(cs->kind == CatchStale) {
        gfx_str_center(c, x + w / 2, y + 41, "Just ate that one!");
        gfx_str_center(c, x + w / 2, y + 50, "Try a new one!");
        return;
    }

    /* detail */
    char det[40];
    snprintf(det, sizeof(det), "%s", cs->c.detail);
    gfx_fit_str(c, det, sizeof(det), w - 8);
    gfx_str_center(c, x + w / 2, y + 41, det);

    /* gains, counting up */
    float k = el > T_CHOMP + 300 ? (float)(el - T_CHOMP - 300) / 500.0f : 0;
    if(k > 1) k = 1;
    char g[16];
    gfx_bmp(c, x + 4, y + 45, &bmp_star5);
    snprintf(g, sizeof(g), "+%d XP", (int)(cs->g_xp * k + 0.5f));
    canvas_draw_str(c, x + 11, y + 50, g);
    if(cs->messy) {
        /* static halves the XP: say why */
        gfx_bmp(c, x + 46, y + 44, &bmp_glitch2);
        canvas_draw_str(c, x + 55, y + 50, "messy");
    } else {
        /* progress to the next level */
        SaveData* s = app->save;
        uint32_t need = state_xp_need(s->level);
        gfx_bar(c, x + 49, y + 45, 34, 5, (uint8_t)(s->level >= 99 ? 100 : s->xp * 100 / (need ? need : 1)));
    }
}

void catch_draw(App* app, Canvas* c) {
    CatchState* cs = &app->cat_st;
    uint32_t el = app->now - cs->t0;
    uint8_t src = cs->c.src;
    int32_t gx, gy;
    graphic_center(src, &gx, &gy);

    draw_header(app, c, src);
    if(el < T_CHOMP) {
        if(src == SrcSubGhz)
            draw_subghz(app, c, true);
        else
            draw_other(app, c, src, true);
    }

    Pose p;
    catch_pose(app, &p);
    draw_pet(c, PET_X, PET_GY, &p);
    int32_t my = pet_mouth_y(&p, PET_GY);

    if(el < T_LOCK) {
        /* brackets converge on the signal */
        float k = ease_out((float)el / (float)T_LOCK);
        int32_t d = 10 + (int32_t)((1.0f - k) * 30.0f);
        if(gx - d >= 0) {
            bracket(c, gx - d, gy - d, 1, 1);
            bracket(c, gx + d, gy - d, -1, 1);
            bracket(c, gx - d, gy + d, 1, -1);
            bracket(c, gx + d, gy + d, -1, -1);
        }
    } else if(el < T_FLY) {
        float k = ease_in_out((float)(el - T_LOCK) / (float)(T_FLY - T_LOCK));
        float u = 1.0f - k;
        /* quadratic bezier: source -> high arc -> mouth */
        float cx1 = (gx + PET_X) / 2.0f, cy1 = 6.0f;
        for(int32_t tr = 3; tr >= 0; tr--) {
            float kk = k - tr * 0.06f;
            if(kk < 0) continue;
            float uu = 1.0f - kk;
            int32_t ox = (int32_t)(uu * uu * gx + 2 * uu * kk * cx1 + kk * kk * (PET_X - 3));
            int32_t oy = (int32_t)(uu * uu * gy + 2 * uu * kk * cy1 + kk * kk * my);
            if(tr == 0)
                orb(c, ox, oy, app->now, src);
            else if(ox >= 0 && oy >= 0 && ox < 128 && oy < 64)
                canvas_draw_dot(c, ox, oy);
        }
        UNUSED(u);
    }

    parts_draw(app, c);

    if(cs->card) {
        float k = ease_back((float)(el - T_CHOMP) / 320.0f);
        int32_t y = 64 - (int32_t)(k * 52.0f);
        draw_card(app, c, y);
        if(el > T_CHOMP + 400) {
            /* the timer makes room for a hint */
            canvas_set_color(c, ColorWhite);
            canvas_draw_box(c, 90, 0, 38, 10);
            canvas_set_color(c, ColorBlack);
            canvas_set_font(c, FontSecondary);
            gfx_button_hint(c, 99, 8, &bmp_btn_ok, "Next");
        }
    }

    /* flash at the moment of the catch */
    if(el < 60 || (el > 110 && el < 170)) gfx_invert(c, 0, 0, 128, 64);
}
