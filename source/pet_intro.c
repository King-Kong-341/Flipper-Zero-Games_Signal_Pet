/*
 * Cinematics: the boot animation, hatching the egg, level ups and
 * evolutions.
 */
#include "pet.h"

#define HATCH_TAPS 8
#define GROUND_Y 50

static uint32_t rnd(uint32_t n) {
    return furi_hal_random_get() % n;
}

/* ================================================================ boot */

/* Timeline (ms):
 *    0- 300  CRT power-on: a line of light grows across the dark screen
 *  300- 520  the picture opens up from that line
 *  520-1500  the antenna logo assembles itself from flying pixels
 * 1500-      radio waves, the title drops in letter by letter
 * 2150-3350  status messages + loading bar with a running mini pet  */
#define BOOT_END 3600
#define BOOT_SKIP_TO 3300

static uint32_t boot_hash(uint32_t i) {
    uint32_t h = i * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return h;
}

void boot_enter(App* app) {
    app->intro.t0 = app->now;
    app->intro.hatched = false;
    parts_clear(app);
}

void boot_input(App* app, InputEvent* ev) {
    if(ev->type != InputTypeShort) return;
    uint32_t el = app->now - app->intro.t0;
    if(el < BOOT_SKIP_TO) app->intro.t0 = app->now - BOOT_SKIP_TO;
}

void boot_update(App* app, uint32_t dt) {
    uint32_t el = app->now - app->intro.t0;
    uint32_t prev = el > dt ? el - dt : 0;
    if(prev < 280 && el >= 280) fx_click(app);
    if(prev < 1500 && el >= 1500) fx_boot(app);
    if(el >= BOOT_END && !app->intro.hatched) {
        app->intro.hatched = true; /* reused as "left boot" flag */
        app_goto(app, app->save->stage == StEgg ? SceneHatch : SceneHome, TransIris);
    }
}

static void boot_logo(Canvas* c, uint32_t el) {
    const Bmp* b = &bmp_big_subghz;
    const int32_t tx0 = 52, ty0 = 4;
    if(el >= 1500) {
        gfx_bmp(c, tx0, ty0, b);
        return;
    }
    uint32_t i = 0;
    int32_t stride = (b->w + 7) / 8;
    for(int32_t y = 0; y < b->h; y++) {
        for(int32_t x = 0; x < b->w; x++) {
            if(!(b->data[y * stride + x / 8] & (1 << (x % 8)))) continue;
            uint32_t h = boot_hash(++i);
            int32_t sx = (int32_t)(h % 128), sy = (int32_t)((h >> 8) % 64);
            int32_t d = (int32_t)((h >> 16) % 350);
            float k = ((float)el - 520.0f - (float)d) / 600.0f;
            if(k < 0) k = 0;
            if(k > 1) k = 1;
            k = ease_out(k);
            int32_t px = sx + (int32_t)((float)(tx0 + x - sx) * k);
            int32_t py = sy + (int32_t)((float)(ty0 + y - sy) * k);
            if(px >= 0 && px < 128 && py >= 0 && py < 64) canvas_draw_dot(c, px, py);
        }
    }
}

void boot_draw(App* app, Canvas* c) {
    uint32_t el = app->now - app->intro.t0;
    canvas_set_color(c, ColorBlack);

    /* CRT power-on */
    if(el < 520) {
        canvas_draw_box(c, 0, 0, 128, 64);
        canvas_set_color(c, ColorWhite);
        if(el < 300) {
            int32_t w = (int32_t)(128.0f * ease_out((float)el / 300.0f));
            canvas_draw_box(c, 64 - w / 2, 31, w, 2);
        } else {
            int32_t h = 2 + (int32_t)(62.0f * ease_in_out((float)(el - 300) / 220.0f));
            canvas_draw_box(c, 0, 32 - h / 2, 128, h);
        }
        canvas_set_color(c, ColorBlack);
        return;
    }

    boot_logo(c, el);

    /* radio waves around the finished logo */
    if(el > 1500) {
        for(int32_t w = 0; w < 3; w++) {
            int32_t r = (int32_t)(((el - 1500) / 14 + w * 22) % 66);
            if(r < 16) continue;
            for(int32_t a = 0; a < 128; a += 2) {
                int32_t x = 64 + gfx_cos(a / 2) * r / 64;
                int32_t y = 12 + gfx_sin(a / 2) * r * 2 / 3 / 64;
                if(y >= 0 && y < 30 && x >= 0 && x < 128 && (x < 46 || x > 81)) canvas_draw_dot(c, x, y);
            }
        }
    }

    /* title letter by letter */
    const char* title = "SIGNAL PET";
    canvas_set_font(c, FontPrimary);
    int32_t tw = canvas_string_width(c, title);
    int32_t x0 = 64 - tw / 2;
    char pre[12];
    for(int32_t i = 0; title[i]; i++) {
        int32_t t0 = 1600 + i * 60;
        if((int32_t)el < t0) break;
        /* x of letter i = advance of the prefix = width(prefix + ch) - width(ch) */
        memcpy(pre, title, i + 1);
        pre[i + 1] = '\0';
        char ch[2] = {title[i], '\0'};
        int32_t x = x0 + canvas_string_width(c, pre) - canvas_string_width(c, ch);
        float kk = ease_back((float)(el - t0) / 220.0f);
        int32_t y = 40 - (int32_t)((1.0f - kk) * 8.0f);
        if(title[i] != ' ') canvas_draw_str(c, x, y, ch);
    }

    /* status + loading bar with a little runner */
    if(el > 2150) {
        static const char* const msgs[4] = {
            "Warming up antennas", "Calibrating radio", "Sniffing the airwaves", "Waking up"};
        uint32_t m = (el - 2150) / 300;
        if(m > 3) m = 3;
        canvas_set_font(c, FontSecondary);
        char buf[32];
        if(m == 3)
            snprintf(buf, sizeof(buf), "%s %s", msgs[3], app->save->stage == StEgg ? "the egg" : app->save->name);
        else
            snprintf(buf, sizeof(buf), "%s", msgs[m]);
        gfx_str_center(c, 64, 50, buf);

        float p = (float)(el - 2150) / 1200.0f;
        if(p > 1) p = 1;
        p = ease_in_out(p);
        canvas_draw_rframe(c, 14, 53, 100, 11, 3);
        int32_t fw = (int32_t)(86.0f * p);
        if(fw > 0) canvas_draw_box(c, 16, 55, fw, 7);
        const Bmp* runner = ((el / 120) & 1) ? &bmp_mini_pet : &bmp_mini_pet2;
        gfx_bmp(c, 16 + fw + 1, 55, runner);
    }
}

/* ================================================================ hatch */

void hatch_enter(App* app) {
    IntroState* s = &app->intro;
    s->t0 = app->now;
    s->taps = 0;
    s->wobble_t0 = 0;
    s->hatch_t0 = 0;
    s->hatched = false;
    parts_clear(app);
    pet_anim_reset(app);
}

static void do_hatch(App* app) {
    SaveData* s = app->save;
    s->stage = StBaby;
    s->level = 1;
    s->last_catch_ts = state_now_ts();
    s->xp = 0;
    s->born_ts = state_now_ts();
    s->last_ts = s->born_ts;
    s->food = 60000;
    s->joy = 85000;
    s->energy = 90000;
    state_badges_check(app);
    state_save(app);
}

void hatch_input(App* app, InputEvent* ev) {
    IntroState* s = &app->intro;
    if(ev->type != InputTypeShort) return;
    if(ev->key == InputKeyBack) {
        app->running = false;
        return;
    }
    if(ev->key != InputKeyOk || s->hatch_t0) return;
    s->taps++;
    s->wobble_t0 = app->now;
    fx_crack(app);
    parts_burst(app, PartSpark, 64 + (float)((int32_t)rnd(13) - 6), 34, 2, 18, false);
    if(s->taps >= HATCH_TAPS) s->hatch_t0 = app->now;
}

void hatch_update(App* app, uint32_t dt) {
    IntroState* s = &app->intro;
    if(!s->hatch_t0) return;
    uint32_t el = app->now - s->hatch_t0;
    uint32_t prev = el > dt ? el - dt : 0;
    if(prev < 1000 && el >= 1000) {
        do_hatch(app);
        s->hatched = true;
        fx_hatch(app);
        for(uint8_t i = 0; i < 12; i++) {
            int32_t a = 40 + (int32_t)rnd(48); /* upwards */
            float sp = 40.0f + (float)rnd(40);
            parts_spawn(
                app,
                PartShell,
                64,
                36,
                (float)gfx_cos(a) / 64.0f * sp,
                (float)gfx_sin(a) / 64.0f * sp - 30,
                1400,
                true);
        }
        parts_burst(app, PartStar, 64, 30, 6, 40, false);
    }
    if(s->hatched && prev < 2200 && el >= 2200) {
        parts_burst(app, PartHeart, 64, 26, 3, 20, false);
    }
    if(s->hatched && prev < 3600 && el >= 3600) {
        name_enter(app, true);
        app_goto(app, SceneName, TransBlinds);
    }
}

static void draw_nest(Canvas* c) {
    canvas_set_color(c, ColorBlack);
    for(int32_t i = 0; i < 7; i++) {
        int32_t x = 48 + i * 5;
        canvas_draw_line(c, x, GROUND_Y - 3, x + 6, GROUND_Y);
        canvas_draw_line(c, x + 6, GROUND_Y - 3, x, GROUND_Y);
    }
    canvas_draw_line(c, 46, GROUND_Y - 4, 82, GROUND_Y - 4);
}

void hatch_draw(App* app, Canvas* c) {
    IntroState* s = &app->intro;
    uint32_t t = app->now;
    home_scene_backdrop(app, c, true);

    canvas_set_font(c, FontPrimary);
    canvas_set_color(c, ColorWhite);
    canvas_draw_box(c, 0, 0, 128, 12);
    canvas_set_color(c, ColorBlack);

    if(!s->hatched) {
        gfx_str_center(c, 64, 10, s->taps ? "It's moving!" : "A mysterious egg!");
        Pose p;
        memset(&p, 0, sizeof(p));
        p.stage = StEgg;
        p.cracks = (uint8_t)(s->taps * 4 / HATCH_TAPS);
        int32_t sh = 0, x = 64;
        if(s->hatch_t0) {
            uint32_t el = t - s->hatch_t0;
            sh = ((el / 45) & 1) ? 3 : -3;
            x += ((el / 60) & 1) ? 1 : -1;
            if(el > 900) gfx_invert(c, 0, 12, 128, 52);
        } else if(s->wobble_t0 && t - s->wobble_t0 < 450) {
            uint32_t el = t - s->wobble_t0;
            int32_t amp = 3 - (int32_t)(el / 150);
            sh = ((el / 60) & 1) ? amp : -amp;
        } else if((t % 2600) < 300) {
            sh = ((t / 75) & 1) ? 1 : -1;
        }
        p.shear = (int8_t)sh;
        draw_nest(c);
        draw_pet(c, x, GROUND_Y - 3, &p);
        canvas_set_color(c, ColorWhite);
        canvas_draw_box(c, 0, 53, 128, 11);
        canvas_set_color(c, ColorBlack);
        canvas_set_font(c, FontSecondary);
        if(!s->hatch_t0) {
            gfx_button_hint(c, 22, 62, &bmp_btn_ok, "Press OK to warm it up");
            /* warmth meter */
            canvas_set_color(c, ColorWhite);
            canvas_draw_box(c, 30, 12, 68, 5);
            canvas_set_color(c, ColorBlack);
            for(uint8_t i = 0; i < HATCH_TAPS; i++) {
                if(i < s->taps)
                    canvas_draw_box(c, 33 + i * 8, 14, 6, 2);
                else
                    canvas_draw_dot(c, 35 + i * 8, 14);
            }
        }
    } else {
        uint32_t el = t - s->hatch_t0 - 1000;
        gfx_str_center(c, 64, 10, "It hatched!");
        draw_nest(c);
        Pose p;
        pose_default(&p, app);
        if(el < 600) {
            p.eyes = EyeWide;
            p.mouth = MouthO;
        } else if(el < 800) {
            p.eyes = EyeBlink;
            p.mouth = MouthO;
        } else {
            p.eyes = EyeHappy;
            p.mouth = MouthOpen;
            p.blush = true;
            uint32_t k = (el - 800) % 700;
            p.lift = k < 350 ? (int8_t)(gfx_sin((int32_t)(k * 32 / 350)) * 6 / 64) : 0;
        }
        draw_pet(c, 64, GROUND_Y - 3, &p);
    }
    parts_draw(app, c);
}

/* ================================================================ celebrate */

bool celeb_pending(App* app) {
    return app->pending_levels || app->pending_evolve;
}

void celeb_start(App* app, Scene ret) {
    CelebState* cs = &app->celeb;
    SaveData* s = app->save;
    cs->ret = ret;
    cs->done = false;
    if(app->pending_levels) {
        cs->kind = CelebLevel;
        cs->level = s->level;
        app->pending_levels = 0;
        fx_levelup(app);
    } else {
        cs->kind = CelebEvolve;
        cs->old_stage = s->stage;
        cs->old_form = s->form;
        if(s->stage == StBaby) {
            s->stage = StTeen;
        } else if(s->stage == StTeen) {
            s->stage = StAdult;
            s->form = state_pick_form(app);
        }
        app->pending_evolve = false;
        state_check_evolve(app);
        state_badges_check(app);
        state_save(app);
    }
    cs->t0 = app->now;
    parts_clear(app);
    if(app->scene == SceneCelebrate)
        app->scene = SceneCelebrate;
    else if(app->scene == SceneCatch)
        app->scene = SceneCelebrate;
    else
        app_goto(app, SceneCelebrate, TransBlinds);
    if(app->scene != SceneCelebrate) cs->t0 = app->now + 160;
}

#define EVO_REVEAL 4300

void celeb_input(App* app, InputEvent* ev) {
    CelebState* cs = &app->celeb;
    if(ev->type != InputTypeShort || (ev->key != InputKeyOk && ev->key != InputKeyBack)) return;
    int32_t el = (int32_t)(app->now - cs->t0);
    if(el < (cs->kind == CelebLevel ? 900 : EVO_REVEAL + 700)) return;
    fx_click(app);
    if(app->pending_levels || app->pending_evolve) {
        celeb_start(app, cs->ret);
        return;
    }
    parts_clear(app);
    if(cs->ret == SceneScan) {
        app->scene = SceneScan;
        radio_resume(app->radio);
    } else {
        app_goto(app, cs->ret, TransBlinds);
    }
}

void celeb_update(App* app, uint32_t dt) {
    CelebState* cs = &app->celeb;
    int32_t el = (int32_t)(app->now - cs->t0);
    int32_t prev = el - (int32_t)dt;
    if(el < 0) return;
    if(cs->kind == CelebLevel) {
        if((el / 180) != (prev / 180) && el < 2400)
            parts_spawn(app, PartStar, 24 + (float)rnd(80), 20 + (float)rnd(20), 0, -6, 900, false);
    } else {
        if(prev < EVO_REVEAL && el >= EVO_REVEAL) {
            fx_evolve(app);
            parts_burst(app, PartStar, 64, 30, 10, 50, false);
            parts_burst(app, PartSpark, 64, 30, 6, 30, false);
        }
        if(el > EVO_REVEAL && (el / 300) != (prev / 300) && el < EVO_REVEAL + 3000)
            parts_spawn(app, PartHeart, 50 + (float)rnd(28), 30, 0, -12, 1100, false);
    }
}

void celeb_draw(App* app, Canvas* c) {
    CelebState* cs = &app->celeb;
    SaveData* s = app->save;
    int32_t el = (int32_t)(app->now - cs->t0);
    if(el < 0) el = 0;
    canvas_set_color(c, ColorBlack);
    Pose p;
    pose_default(&p, app);

    if(cs->kind == CelebLevel) {
        gfx_burst(c, 64, 36, 18, 80, el / 45, 12);
        p.eyes = EyeHappy;
        p.mouth = MouthOpen;
        p.blush = true;
        uint32_t k = (uint32_t)el % 650;
        p.lift = k < 380 ? (int8_t)(gfx_sin((int32_t)(k * 32 / 380)) * 7 / 64) : 0;
        p.squash = p.lift > 2 ? -1 : (p.lift == 0 ? 1 : 0);
        draw_pet(c, 64, 52, &p);
        parts_draw(app, c);

        /* banner */
        float kb = ease_back((float)el / 400.0f);
        int32_t by = -14 + (int32_t)(kb * 16.0f);
        canvas_set_color(c, ColorBlack);
        canvas_draw_rbox(c, 30, by, 68, 13, 3);
        canvas_set_color(c, ColorWhite);
        canvas_set_font(c, FontPrimary);
        gfx_str_center(c, 64, by + 10, "LEVEL UP!");
        canvas_set_color(c, ColorBlack);

        char buf[40];
        canvas_set_font(c, FontSecondary);
        snprintf(buf, sizeof(buf), "%s is now Lv %u", s->name, cs->level);
        if(canvas_string_width(c, buf) > 120) snprintf(buf, sizeof(buf), "Now level %u!", cs->level);
        int32_t w = canvas_string_width(c, buf) + 8;
        canvas_set_color(c, ColorWhite);
        canvas_draw_box(c, 64 - w / 2, 54, w, 10);
        canvas_set_color(c, ColorBlack);
        gfx_str_center(c, 64, 62, buf);
        return;
    }

    /* evolution */
    Pose old = p;
    old.stage = cs->old_stage;
    old.form = cs->old_form;
    canvas_set_font(c, FontSecondary);
    if(el < EVO_REVEAL) {
        char buf[48];
        snprintf(buf, sizeof(buf), "What? %s is evolving!", s->name);
        if(canvas_string_width(c, buf) > 120) snprintf(buf, sizeof(buf), "%s is evolving!", s->name);
        bool show_new = false;
        if(el < 1400) {
            old.eyes = EyeWide;
            old.mouth = MouthO;
            old.shear = (int8_t)(((el / 90) & 1) ? 1 : -1);
        } else {
            int32_t e = el - 1400;
            int32_t period = 420 - e * 380 / (EVO_REVEAL - 1400);
            if(period < 40) period = 40;
            show_new = ((e / period) & 1) != 0;
            gfx_burst(c, 64, 36, 20, 30 + e / 40, e / 30, 10);
            old.eyes = EyeSleep;
            old.mouth = MouthFlat;
        }
        if(show_new) {
            p.eyes = EyeSleep;
            p.mouth = MouthFlat;
            draw_pet(c, 64, 58, &p);
        } else {
            draw_pet(c, 64, 58, &old);
        }
        int32_t tw = canvas_string_width(c, buf) + 8;
        canvas_set_color(c, ColorWhite);
        canvas_draw_box(c, 64 - tw / 2, 0, tw, 10);
        canvas_set_color(c, ColorBlack);
        gfx_str_center(c, 64, 8, buf);
        if(el > EVO_REVEAL - 300) gfx_invert(c, 0, 0, 128, 64);
        return;
    }

    int32_t e = el - EVO_REVEAL;
    gfx_burst(c, 64, 36, 22, 80, e / 50, 14);
    p.eyes = e < 500 ? EyeWide : EyeHappy;
    p.mouth = e < 500 ? MouthO : MouthOpen;
    p.blush = e >= 500;
    draw_pet(c, 64, 56, &p);
    parts_draw(app, c);

    const char* fname = state_form_name(s->stage, s->form);
    char up[16];
    size_t i = 0;
    for(; fname[i] && i < sizeof(up) - 2; i++)
        up[i] = (fname[i] >= 'a' && fname[i] <= 'z') ? fname[i] - 32 : fname[i];
    up[i++] = '!';
    up[i] = '\0';
    canvas_set_font(c, FontPrimary);
    int32_t w = canvas_string_width(c, up) + 12;
    float kb = ease_back((float)e / 400.0f);
    int32_t by = -14 + (int32_t)(kb * 16.0f);
    canvas_set_color(c, ColorBlack);
    canvas_draw_rbox(c, 64 - w / 2, by, w, 13, 3);
    canvas_set_color(c, ColorWhite);
    gfx_str_center(c, 64, by + 10, up);
    canvas_set_color(c, ColorBlack);

    if(e > 600) {
        char buf[48];
        if(s->stage == StAdult && s->form != FormOmnix)
            snprintf(buf, sizeof(buf), "Raised on %s signals", source_short[s->form % SrcCount]);
        else if(s->stage == StAdult)
            snprintf(buf, sizeof(buf), "A balanced diet!");
        else
            snprintf(buf, sizeof(buf), "%s grew up a bit!", s->name);
        canvas_set_font(c, FontSecondary);
        int32_t bw = canvas_string_width(c, buf) + 8;
        canvas_set_color(c, ColorWhite);
        canvas_draw_box(c, 64 - bw / 2, 56, bw, 8);
        canvas_set_color(c, ColorBlack);
        gfx_str_center(c, 64, 63, buf);
    }
}
