/*
 * Sound, vibration and RGB LED effects, particles and toast messages.
 *
 * Three tiny non-blocking sequencers (tones, vibration, LED) are driven
 * from the main loop, each one respecting its own switch in the settings.
 */
#include "pet.h"

/* ---------------------------------------------------------- tones */

static void tone_next(App* app, uint32_t now) {
    Fx* fx = &app->fx;
    if(fx->tone_i >= fx->tone_n) {
        if(fx->speaker) furi_hal_speaker_stop();
        fx->tone_busy = false;
        return;
    }
    const Tone* t = &fx->tones[fx->tone_i++];
    if(fx->speaker) {
        if(t->f)
            furi_hal_speaker_start((float)t->f, (float)t->vol / 100.0f);
        else
            furi_hal_speaker_stop();
    }
    fx->tone_end = now + t->ms;
    fx->tone_busy = true;
}

static void play(App* app, const Tone* tones, uint8_t n) {
    if(!app->save->set.sound) return;
    Fx* fx = &app->fx;
    if(!fx->speaker) {
        uint32_t now = furi_get_tick();
        if(fx->speaker_retry && (int32_t)(now - fx->speaker_retry) < 0) return;
        fx->speaker = furi_hal_speaker_acquire(30);
        if(!fx->speaker) {
            fx->speaker_retry = now + 2000;
            return;
        }
    }
    if(n > FX_MAX_TONES) n = FX_MAX_TONES;
    memcpy(fx->tones, tones, sizeof(Tone) * n);
    fx->tone_n = n;
    fx->tone_i = 0;
    tone_next(app, furi_get_tick());
}

/* ---------------------------------------------------------- vibration */

static void vib_next(App* app, uint32_t now) {
    Fx* fx = &app->fx;
    if(fx->vib_i >= fx->vib_n) {
        furi_hal_vibro_on(false);
        fx->vib_busy = false;
        return;
    }
    furi_hal_vibro_on((fx->vib_i % 2) == 0);
    fx->vib_end = now + fx->vib[fx->vib_i++];
    fx->vib_busy = true;
}

static void vibrate(App* app, const uint16_t* pattern, uint8_t n) {
    if(!app->save->set.vibro) return;
    Fx* fx = &app->fx;
    if(n > FX_MAX_VIB) n = FX_MAX_VIB;
    memcpy(fx->vib, pattern, sizeof(uint16_t) * n);
    fx->vib_n = n;
    fx->vib_i = 0;
    vib_next(app, furi_get_tick());
}

/* ---------------------------------------------------------- LED */

static void led_set(uint8_t r, uint8_t g, uint8_t b) {
    furi_hal_light_set(LightRed, r);
    furi_hal_light_set(LightGreen, g);
    furi_hal_light_set(LightBlue, b);
}

static void led(App* app, const LedStep* steps, uint8_t n, bool loop) {
    Fx* fx = &app->fx;
    if(!app->save->set.led) {
        if(fx->led_busy) led_set(0, 0, 0);
        fx->led_busy = false;
        return;
    }
    if(n > FX_MAX_LED) n = FX_MAX_LED;
    memcpy(fx->led, steps, sizeof(LedStep) * n);
    fx->led_n = n;
    fx->led_i = 0;
    fx->led_start = furi_get_tick();
    fx->led_busy = true;
    fx->led_loop = loop;
    led_set(steps[0].r, steps[0].g, steps[0].b);
}

static void led_update(App* app, uint32_t now) {
    Fx* fx = &app->fx;
    if(!fx->led_busy) return;
    while(fx->led_i < fx->led_n) {
        const LedStep* s = &fx->led[fx->led_i];
        uint32_t el = now - fx->led_start;
        if(el < s->ms) {
            float k = s->fade ? 1.0f - (float)el / (float)s->ms : 1.0f;
            led_set((uint8_t)(s->r * k), (uint8_t)(s->g * k), (uint8_t)(s->b * k));
            return;
        }
        fx->led_start += s->ms;
        fx->led_i++;
        if(fx->led_i >= fx->led_n && fx->led_loop) fx->led_i = 0;
    }
    led_set(0, 0, 0);
    fx->led_busy = false;
}

/* ---------------------------------------------------------- engine */

void fx_init(App* app) {
    memset(&app->fx, 0, sizeof(Fx));
}

void fx_stop_all(App* app) {
    Fx* fx = &app->fx;
    if(fx->speaker) furi_hal_speaker_stop();
    fx->tone_busy = false;
    furi_hal_vibro_on(false);
    fx->vib_busy = false;
    if(fx->led_busy) led_set(0, 0, 0);
    fx->led_busy = false;
}

void fx_deinit(App* app) {
    fx_stop_all(app);
    if(app->fx.speaker) {
        furi_hal_speaker_release();
        app->fx.speaker = false;
    }
    led_set(0, 0, 0);
}

void fx_update(App* app) {
    Fx* fx = &app->fx;
    uint32_t now = furi_get_tick();
    if(fx->tone_busy && (int32_t)(now - fx->tone_end) >= 0) tone_next(app, now);
    if(fx->vib_busy && (int32_t)(now - fx->vib_end) >= 0) vib_next(app, now);
    led_update(app, now);
}

/* ---------------------------------------------------------- sounds */

#define N(a) (uint8_t)(sizeof(a) / sizeof(a[0]))

void fx_click(App* app) {
    static const Tone t[] = {{1800, 8, 35}};
    play(app, t, 1);
}

void fx_back(App* app) {
    static const Tone t[] = {{1100, 10, 35}, {800, 12, 30}};
    play(app, t, 2);
}

void fx_boot(App* app) {
    static const Tone t[] = {
        {523, 70, 55}, {0, 15, 0}, {659, 70, 55}, {0, 15, 0}, {784, 70, 55}, {0, 15, 0}, {1047, 180, 60}};
    static const LedStep l[] = {
        {0, 60, 255, 150, false}, {0, 255, 120, 150, false}, {255, 0, 200, 150, false}, {255, 120, 0, 400, true}};
    play(app, t, N(t));
    led(app, l, N(l), false);
}

static const uint8_t src_rgb[SrcCount][3] = {
    {0, 90, 255}, /* Sub-GHz: blue */
    {0, 255, 110}, /* NFC: green */
    {255, 0, 200}, /* RFID: magenta */
    {255, 25, 0}, /* IR: red */
    {255, 170, 0}, /* iButton: amber */
};

void fx_scan_led(App* app, uint8_t src, bool on) {
    if(!on) {
        if(app->fx.led_busy) led_set(0, 0, 0);
        app->fx.led_busy = false;
        return;
    }
    const uint8_t* c = src_rgb[src % SrcCount];
    LedStep l[2] = {{c[0] / 3, c[1] / 3, c[2] / 3, 900, true}, {0, 0, 0, 500, false}};
    led(app, l, 2, true);
}

void fx_sense(App* app) {
    static const Tone t[] = {{1400, 15, 35}, {0, 30, 0}, {1400, 15, 35}};
    play(app, t, N(t));
}

void fx_catch(App* app, uint8_t src) {
    static const Tone t[] = {{1319, 40, 70}, {0, 20, 0}, {1760, 70, 75}};
    static const uint16_t v[] = {70};
    const uint8_t* c = src_rgb[src % SrcCount];
    LedStep l[3] = {{c[0], c[1], c[2], 120, false}, {255, 255, 255, 80, false}, {c[0], c[1], c[2], 500, true}};
    play(app, t, N(t));
    vibrate(app, v, N(v));
    led(app, l, 3, false);
}

void fx_new_species(App* app) {
    static const Tone t[] = {
        {784, 80, 70}, {0, 10, 0}, {988, 80, 70}, {0, 10, 0}, {1175, 80, 70}, {0, 10, 0}, {1568, 260, 75}};
    static const uint16_t v[] = {60, 60, 60};
    static const LedStep l[] = {
        {255, 0, 0, 90, false},
        {255, 160, 0, 90, false},
        {0, 255, 0, 90, false},
        {0, 160, 255, 90, false},
        {160, 0, 255, 90, false},
        {255, 255, 255, 400, true}};
    play(app, t, N(t));
    vibrate(app, v, N(v));
    led(app, l, N(l), false);
}

void fx_chomp(App* app) {
    static const Tone t[] = {{320, 30, 70}, {0, 50, 0}, {260, 30, 70}, {0, 50, 0}, {380, 40, 70}};
    play(app, t, N(t));
}

void fx_stale(App* app) {
    static const Tone t[] = {{220, 110, 55}, {165, 180, 55}};
    play(app, t, N(t));
}

void fx_levelup(App* app) {
    static const Tone t[] = {
        {523, 60, 65},
        {659, 60, 65},
        {784, 60, 65},
        {1047, 60, 65},
        {1319, 60, 65},
        {0, 30, 0},
        {1568, 240, 75}};
    static const uint16_t v[] = {40, 50, 40, 50, 40};
    static const LedStep l[] = {
        {255, 200, 0, 100, false},
        {0, 0, 0, 60, false},
        {255, 200, 0, 100, false},
        {0, 0, 0, 60, false},
        {255, 200, 0, 600, true}};
    play(app, t, N(t));
    vibrate(app, v, N(v));
    led(app, l, N(l), false);
}

void fx_evolve(App* app) {
    static const Tone t[] = {
        {392, 140, 65},
        {523, 140, 65},
        {659, 140, 65},
        {784, 280, 70},
        {0, 60, 0},
        {659, 100, 65},
        {784, 100, 65},
        {1047, 450, 80}};
    static const uint16_t v[] = {250};
    static const LedStep l[] = {
        {255, 255, 255, 120, false},
        {0, 90, 255, 120, false},
        {0, 255, 110, 120, false},
        {255, 0, 200, 120, false},
        {255, 25, 0, 120, false},
        {255, 170, 0, 120, false},
        {255, 255, 255, 900, true}};
    play(app, t, N(t));
    vibrate(app, v, N(v));
    led(app, l, N(l), false);
}

void fx_hatch(App* app) {
    static const Tone t[] = {
        {1047, 50, 65}, {0, 30, 0}, {1319, 50, 65}, {0, 30, 0}, {1568, 60, 70}, {0, 30, 0}, {2093, 200, 70}};
    static const uint16_t v[] = {140};
    static const LedStep l[] = {{255, 255, 255, 200, false}, {255, 200, 120, 700, true}};
    play(app, t, N(t));
    vibrate(app, v, N(v));
    led(app, l, N(l), false);
}

void fx_crack(App* app) {
    static const Tone t[] = {{1600, 12, 55}, {700, 25, 50}};
    static const uint16_t v[] = {30};
    play(app, t, N(t));
    vibrate(app, v, N(v));
}

void fx_purr(App* app) {
    static const Tone t[] = {{196, 70, 45}, {0, 25, 0}, {220, 90, 45}, {0, 25, 0}, {247, 60, 40}};
    static const LedStep l[] = {{255, 40, 120, 500, true}};
    play(app, t, N(t));
    led(app, l, N(l), false);
}

void fx_badge(App* app) {
    static const Tone t[] = {{1319, 60, 65}, {0, 20, 0}, {1568, 60, 65}, {0, 20, 0}, {2093, 160, 70}};
    static const LedStep l[] = {{255, 170, 0, 600, true}};
    play(app, t, N(t));
    led(app, l, N(l), false);
}

void fx_clean(App* app) {
    static const Tone t[] = {
        {2200, 25, 30},
        {1900, 25, 30},
        {2400, 25, 30},
        {2000, 25, 30},
        {2600, 25, 30},
        {2100, 25, 30},
        {2800, 25, 30},
        {0, 60, 0},
        {1568, 50, 50},
        {2093, 90, 55}};
    play(app, t, N(t));
}

void fx_sleep(App* app, bool to_sleep) {
    static const Tone down[] = {{784, 90, 45}, {659, 90, 45}, {523, 180, 40}};
    static const Tone up[] = {{523, 70, 50}, {659, 70, 50}, {784, 70, 50}, {1047, 120, 55}};
    if(to_sleep)
        play(app, down, N(down));
    else
        play(app, up, N(up));
}

void fx_pickup(App* app, bool golden) {
    static const Tone t1[] = {{1568, 30, 55}};
    static const Tone t2[] = {{1568, 30, 60}, {0, 10, 0}, {2093, 70, 65}};
    if(golden)
        play(app, t2, N(t2));
    else
        play(app, t1, N(t1));
}

void fx_hurt(App* app) {
    static const Tone t[] = {{180, 60, 80}, {120, 120, 80}};
    static const uint16_t v[] = {150};
    static const LedStep l[] = {{255, 0, 0, 400, true}};
    play(app, t, N(t));
    vibrate(app, v, N(v));
    led(app, l, N(l), false);
}

void fx_gameover(App* app) {
    static const Tone t[] = {{523, 120, 60}, {392, 120, 60}, {330, 120, 60}, {262, 320, 60}};
    play(app, t, N(t));
}

void fx_lock(App* app) {
    static const Tone t[] = {{1047, 40, 60}, {0, 15, 0}, {1568, 100, 65}};
    static const uint16_t v[] = {50};
    static const LedStep l[] = {{0, 255, 80, 400, true}};
    play(app, t, N(t));
    vibrate(app, v, N(v));
    led(app, l, N(l), false);
}

void fx_refuse(App* app) {
    static const Tone t[] = {{330, 60, 50}, {0, 40, 0}, {262, 110, 50}};
    play(app, t, N(t));
}

void fx_bye(App* app) {
    static const Tone t[] = {{784, 80, 50}, {0, 20, 0}, {523, 160, 45}};
    play(app, t, N(t));
}

/* ---------------------------------------------------------- particles */

void parts_clear(App* app) {
    memset(app->parts, 0, sizeof(app->parts));
}

void parts_spawn(App* app, PartKind k, float x, float y, float vx, float vy, uint16_t life, bool gravity) {
    for(uint8_t i = 0; i < MAX_PARTS; i++) {
        Particle* p = &app->parts[i];
        if(p->alive) continue;
        p->x = x;
        p->y = y;
        p->vx = vx;
        p->vy = vy;
        p->life = life;
        p->age = 0;
        p->kind = k;
        p->gravity = gravity;
        p->alive = true;
        return;
    }
}

void parts_burst(App* app, PartKind k, float x, float y, uint8_t n, float speed, bool gravity) {
    for(uint8_t i = 0; i < n; i++) {
        int32_t a = (int32_t)(furi_hal_random_get() % 64);
        float s = speed * (0.5f + (float)(furi_hal_random_get() % 100) / 200.0f);
        float vx = (float)gfx_cos(a) / 64.0f * s;
        float vy = (float)gfx_sin(a) / 64.0f * s - (gravity ? speed * 0.6f : 0);
        parts_spawn(app, k, x, y, vx, vy, 500 + furi_hal_random_get() % 500, gravity);
    }
}

void parts_update(App* app, uint32_t dt) {
    float s = (float)dt / 1000.0f;
    for(uint8_t i = 0; i < MAX_PARTS; i++) {
        Particle* p = &app->parts[i];
        if(!p->alive) continue;
        p->age += dt;
        if(p->age >= p->life) {
            p->alive = false;
            continue;
        }
        if(p->gravity) p->vy += 140.0f * s;
        p->x += p->vx * s;
        p->y += p->vy * s;
        /* floating kinds wobble sideways */
        if(p->kind == PartHeart || p->kind == PartNote || p->kind == PartZ)
            p->x += (float)gfx_sin((p->age / 40) + i * 7) / 64.0f * 0.25f;
    }
}

static void pdot(Canvas* c, int32_t x, int32_t y) {
    if(x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H) canvas_draw_dot(c, x, y);
}

void parts_draw(App* app, Canvas* c) {
    canvas_set_color(c, ColorBlack);
    for(uint8_t i = 0; i < MAX_PARTS; i++) {
        Particle* p = &app->parts[i];
        if(!p->alive) continue;
        int32_t x = (int32_t)p->x, y = (int32_t)p->y;
        bool late = p->age > p->life * 3 / 4;
        switch(p->kind) {
        case PartHeart:
            if(late && ((p->age / 60) & 1)) break;
            if(x >= 0 && y >= 0) gfx_bmp(c, x - 2, y - 2, &bmp_heart5);
            break;
        case PartNote:
            if(late && ((p->age / 60) & 1)) break;
            if(x >= 0 && y >= 0) gfx_bmp(c, x - 2, y - 2, &bmp_note);
            break;
        case PartStar:
            if(late && ((p->age / 50) & 1)) break;
            if(p->age < p->life / 3) {
                pdot(c, x, y);
                pdot(c, x - 1, y);
                pdot(c, x + 1, y);
                pdot(c, x, y - 1);
                pdot(c, x, y + 1);
            } else if(x >= 2 && y >= 2) {
                gfx_bmp(c, x - 2, y - 2, &bmp_star5);
            }
            break;
        case PartZ:
            if(x >= 0 && y >= 0) gfx_bmp(c, x - 2, y - 2, &bmp_zzz);
            break;
        case PartCrumb:
            pdot(c, x, y);
            pdot(c, x + 1, y);
            break;
        case PartSpark: {
            int32_t r = 1 + (int32_t)(p->age * 3 / (p->life ? p->life : 1));
            pdot(c, x - r, y);
            pdot(c, x + r, y);
            pdot(c, x, y - r);
            pdot(c, x, y + r);
            break;
        }
        case PartShell:
            pdot(c, x, y);
            pdot(c, x + 1, y);
            pdot(c, x + 2, y + 1);
            pdot(c, x, y + 1);
            break;
        case PartDust:
            if((p->age / 40) & 1) pdot(c, x, y);
            break;
        case PartBit:
            pdot(c, x, y);
            if((p->age / 50 + i) & 1) pdot(c, x + 1, y + 1);
            break;
        default:
            break;
        }
    }
}

/* ---------------------------------------------------------- toasts */

void toast_show(App* app, const char* text, uint8_t icon, uint16_t ms) {
    strncpy(app->toast.text, text, TOAST_LEN - 1);
    app->toast.text[TOAST_LEN - 1] = '\0';
    app->toast.icon = icon;
    app->toast.t0 = app->now;
    app->toast.dur = ms;
    app->toast.active = true;
}

extern const Bmp* const badge_icons[BADGE_COUNT];

void toast_draw(App* app, Canvas* c) {
    Toast* t = &app->toast;
    if(!t->active) return;
    uint32_t el = app->now - t->t0;
    if(el >= t->dur) {
        t->active = false;
        return;
    }
    /* slide in for 200 ms, out for the last 200 ms */
    float k = 1.0f;
    if(el < 200) k = ease_out((float)el / 200.0f);
    if(el > (uint32_t)t->dur - 200) k = ease_out((float)(t->dur - el) / 200.0f);
    int32_t h = 13;
    int32_t y = -h + (int32_t)((float)(h + 1) * k);
    canvas_set_font(c, FontSecondary);
    int32_t tw = canvas_string_width(c, t->text);
    int32_t w = tw + (t->icon ? 13 : 0) + 10;
    if(w > 126) w = 126;
    int32_t x = 64 - w / 2;
    if(y + h <= 0) return;
    canvas_set_color(c, ColorWhite);
    canvas_draw_box(c, 0, 0, SCREEN_W, y + h + 1);
    canvas_set_color(c, ColorBlack);
    if(y >= 0) {
        canvas_draw_rframe(c, x, y, w, h, 3);
        canvas_draw_rframe(c, x + 1, y + 1, w - 2, h - 2, 2);
    } else {
        gfx_hline(c, x, y + h - 1, w);
        gfx_vline(c, x, 0, h + y);
        gfx_vline(c, x + w - 1, 0, h + y);
    }
    int32_t tx = x + 5;
    if(t->icon && y + 3 >= 0) {
        gfx_bmp(c, tx, y + 3, badge_icons[(t->icon - 1) % BADGE_COUNT]);
        tx += 11;
    } else if(t->icon) {
        tx += 11;
    }
    if(y + 3 >= 0) canvas_draw_str(c, tx, y + 10, t->text);
}
