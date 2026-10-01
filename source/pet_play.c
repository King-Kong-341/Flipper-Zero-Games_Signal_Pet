/*
 * Mini games.
 *
 *   Byte Catch  move left/right, catch falling data packets, golden
 *                 packets are worth 5, static costs a life. Speeds up.
 *   Tune In       an oscilloscope: match your wave to the dotted target
 *                 wave (Left/Right = frequency, Up/Down = amplitude).
 *                 5 rounds against the clock.
 *
 * Games can be replayed forever, so they pay only a little XP (max 5 per
 * round, GAME_XP_DAY per day); rare signals are where the big XP is.
 */
#include "pet.h"

#define TUNE_ROUNDS 5
#define TUNE_MS 12000
#define SCOPE_Y 12
#define SCOPE_H 40
#define SCOPE_MID (SCOPE_Y + SCOPE_H / 2 - 1)

static uint32_t rnd(uint32_t n) {
    return furi_hal_random_get() % n;
}

/* ================================================================ picker */

#define GAMES 2

static const char* const game_names[GAMES] = {"Byte Catch", "Tune In"};
static const char* const game_titles[GAMES] = {"BYTE CATCH", "TUNE IN"};

static Scene game_scene(uint8_t game) {
    return game ? SceneGameTune : SceneGameCatch;
}

static uint32_t game_best(App* app, uint8_t game) {
    return game ? app->save->best_tune : app->save->best_catch;
}

static uint32_t game_xp_today(App* app) {
    SaveData* s = app->save;
    return s->games_day == state_now_ts() / 86400 ? s->games_xp : 0;
}

void playpick_enter(App* app) {
    parts_clear(app);
    if(app->play.sel >= GAMES) app->play.sel = 0;
}

void playpick_input(App* app, InputEvent* ev) {
    PlayState* g = &app->play;
    if(ev->type != InputTypeShort) return;
    switch(ev->key) {
    case InputKeyLeft:
    case InputKeyRight:
        g->sel ^= 1;
        fx_click(app);
        break;
    case InputKeyOk:
        fx_click(app);
        app_goto(app, game_scene(g->sel), TransBlinds);
        break;
    case InputKeyBack:
        fx_back(app);
        app_goto(app, SceneHome, TransBlinds);
        break;
    default:
        break;
    }
}

static void preview_catch(App* app, Canvas* c, int32_t x, int32_t y) {
    uint32_t t = app->now;
    for(int32_t k = 0; k < 3; k++) {
        int32_t py = y + (int32_t)((t / 40 + k * 6) % 16) - 4;
        int32_t px = x + 8 + k * 17;
        if(py >= y && py < y + 9) gfx_bmp(c, px, py, k == 1 ? &bmp_pkt_gold : &bmp_pkt);
    }
    /* a tiny pet catching them */
    int32_t bx = x + 30 + (gfx_sin((int32_t)(t / 45)) * 16) / 64;
    canvas_draw_circle(c, bx, y + 12, 4);
    canvas_draw_dot(c, bx - 2, y + 11);
    canvas_draw_dot(c, bx + 2, y + 11);
    canvas_draw_line(c, bx, y + 8, bx, y + 6);
}

static void preview_tune(App* app, Canvas* c, int32_t x, int32_t y) {
    uint32_t t = app->now;
    int32_t mid = y + 8;
    int32_t ph = (int32_t)(t / 25);
    int32_t lx = 0, ly = 0;
    for(int32_t i = 0; i <= 50; i++) {
        int32_t a = i * 64 * 2 / 50 + ph;
        int32_t yy = mid - gfx_sin(a) * 6 / 64;
        if(i & 1) canvas_draw_dot(c, x + 5 + i, mid - gfx_sin(a + 6) * 6 / 64);
        if(i) canvas_draw_line(c, x + 5 + lx, ly, x + 5 + i, yy);
        lx = i;
        ly = yy;
    }
}

void playpick_draw(App* app, Canvas* c) {
    PlayState* g = &app->play;
    gfx_title(c, "PLAY");
    canvas_set_font(c, FontSecondary);
    char buf[48];
    for(uint8_t i = 0; i < GAMES; i++) {
        int32_t x = 2 + i * 64, y = 14, w = 60, h = 41;
        bool sel = g->sel == i;
        canvas_draw_rframe(c, x, y, w, h, 3);
        if(sel) canvas_draw_rframe(c, x + 1, y + 1, w - 2, h - 2, 2);
        if(i == 0)
            preview_catch(app, c, x, y + 2);
        else
            preview_tune(app, c, x, y + 2);
        if(sel) {
            canvas_draw_rbox(c, x + 3, y + 20, w - 6, 10, 2);
            canvas_set_color(c, ColorWhite);
        }
        gfx_str_center(c, x + w / 2, y + 28, game_names[i]);
        canvas_set_color(c, ColorBlack);
        snprintf(buf, sizeof(buf), "Best %lu", (unsigned long)game_best(app, i));
        gfx_str_center(c, x + w / 2, y + 38, buf);
    }
    /* game XP left today */
    uint32_t today = game_xp_today(app);
    gfx_bmp(c, 2, 58, &bmp_star5);
    if(today >= GAME_XP_DAY)
        snprintf(buf, sizeof(buf), "XP done today");
    else
        snprintf(buf, sizeof(buf), "XP today %lu/%u", (unsigned long)today, GAME_XP_DAY);
    canvas_draw_str(c, 9, 63, buf);
    gfx_button_hint(c, 99, 63, &bmp_btn_ok, "Play");
}

/* ================================================================ common */

static void start_game(App* app, uint8_t game) {
    PlayState* g = &app->play;
    uint8_t sel = g->sel;
    memset(g, 0, sizeof(PlayState));
    g->sel = sel;
    g->game = game;
    g->t0 = app->now;
    app->save->games++;
    parts_clear(app);
}

static void finish_game(App* app, uint16_t result) {
    PlayState* g = &app->play;
    SaveData* s = app->save;
    if(g->finished) return;
    g->finished = true;
    g->result = result;
    g->best = result > 0 && result > game_best(app, g->game);
    uint32_t want = 1;
    if(g->game == 0) {
        if(g->best) s->best_catch = result;
        want = 1 + result / 8;
    } else if(g->game == 1) {
        if(g->best) s->best_tune = result;
        want = 1 + g->locked / 2 + (g->locked >= TUNE_ROUNDS ? 1 : 0);
        if(g->locked >= TUNE_ROUNDS && !(s->badges & (1u << 13))) {
            /* perfect run badge */
            s->badges |= 1u << 13;
            app->new_badges |= 1u << 13;
        }
    }
    if(want > 5) want = 5; /* games are endless: keep the XP small */
    g->g_xp = (int16_t)state_game_xp(app, want);
    g->capped = g->g_xp < (int16_t)want;
    state_badges_check(app);
    state_save(app);
    if(g->best)
        fx_levelup(app);
    else
        fx_gameover(app);
    app_goto(app, SceneGameOver, TransBlinds);
}

/* ================================================================ packet catch */

void gcatch_enter(App* app) {
    start_game(app, 0);
    PlayState* g = &app->play;
    g->px = 64;
    g->lives = 3;
    g->next_spawn = app->now + 700;
}

void gcatch_input(App* app, InputEvent* ev) {
    PlayState* g = &app->play;
    if(g->over_t0) return;
    if(ev->key == InputKeyLeft || ev->key == InputKeyRight) {
        int8_t d = ev->key == InputKeyLeft ? -1 : 1;
        if(ev->type == InputTypePress)
            g->dir = d;
        else if(ev->type == InputTypeRelease && g->dir == d)
            g->dir = 0;
    } else if(ev->key == InputKeyBack && ev->type == InputTypeShort) {
        g->over_t0 = app->now;
        g->lives = 0;
    }
}

void gcatch_update(App* app, uint32_t dt) {
    PlayState* g = &app->play;
    float s = (float)dt / 1000.0f;
    if(g->finished) return;
    if(g->over_t0) {
        if(app->now - g->over_t0 > 700) finish_game(app, g->score);
        return;
    }
    uint16_t level = 1 + g->score / 8;
    /* smooth acceleration */
    float target = g->dir * 82.0f;
    g->pvx += (target - g->pvx) * (s * 14.0f > 1 ? 1 : s * 14.0f);
    g->px += g->pvx * s;
    if(g->px < 10) g->px = 10, g->pvx = 0;
    if(g->px > 117) g->px = 117, g->pvx = 0;

    if((int32_t)(app->now - g->next_spawn) >= 0) {
        int32_t iv = 950 - level * 70;
        if(iv < 380) iv = 380;
        g->next_spawn = app->now + iv + rnd(250);
        for(uint8_t i = 0; i < GAME_ITEMS; i++) {
            FallItem* it = &g->items[i];
            if(it->alive) continue;
            uint32_t r = rnd(100);
            it->kind = r < 8 ? 1 : (r < 30u + (level > 6 ? 6u : level) * 3u ? 2 : 0);
            it->x = 6 + (float)rnd(110);
            it->y = 4;
            it->vy = 20.0f + level * 5.0f + (float)rnd(10);
            it->alive = true;
            break;
        }
    }

    for(uint8_t i = 0; i < GAME_ITEMS; i++) {
        FallItem* it = &g->items[i];
        if(!it->alive) continue;
        it->y += it->vy * s;
        float dx = it->x + 3 - g->px;
        if(it->y + 5 >= 46 && it->y < 60 && dx > -11 && dx < 11) {
            it->alive = false;
            if(it->kind == 2) {
                g->lives = g->lives ? g->lives - 1 : 0;
                g->hurt_t0 = app->now;
                fx_hurt(app);
                parts_burst(app, PartBit, it->x + 3, it->y + 3, 6, 40, true);
                if(!g->lives) g->over_t0 = app->now;
            } else {
                g->score += it->kind == 1 ? 5 : 1;
                fx_pickup(app, it->kind == 1);
                parts_burst(app, it->kind == 1 ? PartStar : PartCrumb, it->x + 3, it->y + 3, 4, 30, it->kind != 1);
            }
        } else if(it->y > 64) {
            it->alive = false;
        }
    }
}

void gcatch_draw(App* app, Canvas* c) {
    PlayState* g = &app->play;
    canvas_set_font(c, FontSecondary);
    char buf[48];
    snprintf(buf, sizeof(buf), "Score %u", g->score);
    canvas_draw_str(c, 1, 8, buf);
    for(uint8_t i = 0; i < 3; i++)
        gfx_bmp(c, 109 + i * 6, 2, i < g->lives ? &bmp_heart5 : &bmp_heart5_empty);
    for(int32_t x = 0; x < 128; x += 2)
        canvas_draw_dot(c, x, 10);

    for(uint8_t i = 0; i < GAME_ITEMS; i++) {
        FallItem* it = &g->items[i];
        if(!it->alive) continue;
        int32_t x = (int32_t)it->x, y = (int32_t)it->y;
        if(y < 11) continue;
        const Bmp* b = it->kind == 1 ? &bmp_pkt_gold : (it->kind == 2 ? (((app->now / 120) & 1) ? &bmp_glitch1 : &bmp_glitch2) : &bmp_pkt);
        gfx_bmp(c, x, y, b);
    }

    Pose p;
    pose_default(&p, app);
    bool hurt = g->hurt_t0 && app->now - g->hurt_t0 < 500;
    p.eyes = hurt ? EyeDizzy : EyeOpen;
    p.mouth = hurt ? MouthFrown : MouthOpen;
    p.look = g->dir;
    p.step = g->dir ? (uint8_t)(((app->now / 120) & 1) + 1) : 0;
    p.shear = g->dir;
    if(g->lives == 0) {
        p.eyes = EyeDizzy;
        p.mouth = MouthO;
    }
    draw_pet_sized(c, (int32_t)g->px, 64, &p, StBaby);
    parts_draw(app, c);
    if(hurt && app->now - g->hurt_t0 < 90) gfx_invert(c, 0, 11, 128, 53);
}

/* ================================================================ tune in */

static void tune_round(App* app) {
    PlayState* g = &app->play;
    g->t_freq = 1 + rnd(6);
    g->t_amp = 4 + 2 * rnd(6);
    do {
        g->p_freq = 1 + rnd(6);
        g->p_amp = 4 + 2 * rnd(6);
    } while(g->p_freq == g->t_freq || g->p_amp == g->t_amp);
    g->round_t0 = app->now;
    g->lock_t0 = 0;
}

void gtune_enter(App* app) {
    start_game(app, 1);
    tune_round(app);
}

void gtune_input(App* app, InputEvent* ev) {
    PlayState* g = &app->play;
    if(g->lock_t0 || g->finished) return;
    if(ev->type != InputTypeShort && ev->type != InputTypeRepeat) return;
    switch(ev->key) {
    case InputKeyLeft:
        if(g->p_freq > 1) g->p_freq--;
        fx_click(app);
        break;
    case InputKeyRight:
        if(g->p_freq < 6) g->p_freq++;
        fx_click(app);
        break;
    case InputKeyUp:
        if(g->p_amp < 14) g->p_amp += 2;
        fx_click(app);
        break;
    case InputKeyDown:
        if(g->p_amp > 4) g->p_amp -= 2;
        fx_click(app);
        break;
    case InputKeyBack:
        if(ev->type == InputTypeShort) finish_game(app, g->score);
        break;
    default:
        break;
    }
}

void gtune_update(App* app, uint32_t dt) {
    PlayState* g = &app->play;
    if(g->finished) return;
    g->phase += (float)dt * 0.02f;
    if(g->phase > 64000.0f) g->phase = 0;
    uint32_t el = app->now - g->round_t0;
    if(!g->lock_t0) {
        if(g->p_freq == g->t_freq && g->p_amp == g->t_amp) {
            g->lock_t0 = app->now;
            uint32_t left = el < TUNE_MS ? (TUNE_MS - el) / 1000 : 0;
            g->score += 50 + left * 10;
            g->locked++;
            fx_lock(app);
            parts_burst(app, PartStar, 64, SCOPE_MID, 8, 40, false);
        } else if(el > TUNE_MS) {
            g->lock_t0 = app->now;
            fx_refuse(app);
        }
    } else if(app->now - g->lock_t0 > 1000) {
        g->round++;
        if(g->round >= TUNE_ROUNDS)
            finish_game(app, g->score);
        else
            tune_round(app);
    }
}

static int32_t wave_y(uint8_t freq, uint8_t amp, int32_t x, int32_t ph) {
    int32_t a = freq * 32 * x / 123 + ph;
    return SCOPE_MID - gfx_sin(a) * amp / 64;
}

static void slider(Canvas* c, int32_t x, uint8_t val, uint8_t n) {
    for(uint8_t i = 0; i < n; i++) {
        if(i + 1 == val)
            canvas_draw_box(c, x + i * 5, 57, 3, 5);
        else
            canvas_draw_box(c, x + i * 5 + 1, 59, 1, 1);
    }
}

void gtune_draw(App* app, Canvas* c) {
    PlayState* g = &app->play;
    uint32_t el = app->now - g->round_t0;
    canvas_set_font(c, FontSecondary);
    char buf[48];
    snprintf(buf, sizeof(buf), "Round %u/%u", g->round + 1 > TUNE_ROUNDS ? TUNE_ROUNDS : g->round + 1, TUNE_ROUNDS);
    canvas_draw_str(c, 1, 8, buf);
    snprintf(buf, sizeof(buf), "%u", g->score);
    gfx_str_right(c, 127, 8, buf);
    /* time left */
    if(!g->lock_t0) {
        int32_t w = el < TUNE_MS ? (int32_t)((TUNE_MS - el) * 46 / TUNE_MS) : 0;
        canvas_draw_rframe(c, 52, 3, 48, 5, 1);
        if(w > 0) canvas_draw_box(c, 53, 4, w, 3);
    }

    canvas_draw_rframe(c, 0, SCOPE_Y, 128, SCOPE_H, 3);
    for(int32_t x = 4; x < 124; x += 4)
        canvas_draw_dot(c, x, SCOPE_MID);
    for(int32_t x = 16; x < 124; x += 16)
        for(int32_t y = SCOPE_Y + 3; y < SCOPE_Y + SCOPE_H - 2; y += 4)
            canvas_draw_dot(c, x, y);

    int32_t ph = (int32_t)g->phase / 10;
    bool locked = g->lock_t0 && g->p_freq == g->t_freq && g->p_amp == g->t_amp;
    /* target: dotted, a little noisy */
    for(int32_t x = 2; x < 126; x += 2) {
        int32_t y = wave_y(g->t_freq, g->t_amp, x - 2, ph);
        if(!locked && rnd(8) == 0) y += (int32_t)rnd(3) - 1;
        canvas_draw_dot(c, x, y);
    }
    /* player: solid */
    int32_t ly = wave_y(g->p_freq, g->p_amp, 0, ph);
    for(int32_t x = 3; x < 126; x++) {
        int32_t y = wave_y(g->p_freq, g->p_amp, x - 2, ph);
        canvas_draw_line(c, x - 1, ly, x, y);
        ly = y;
    }

    if(g->lock_t0) {
        const char* msg = locked ? "LOCKED!" : "TOO SLOW";
        canvas_set_font(c, FontPrimary);
        int32_t w = canvas_string_width(c, msg) + 10;
        canvas_set_color(c, ColorBlack);
        canvas_draw_rbox(c, 64 - w / 2, SCOPE_MID - 7, w, 14, 3);
        canvas_set_color(c, ColorWhite);
        gfx_str_center(c, 64, SCOPE_MID + 4, msg);
        canvas_set_color(c, ColorBlack);
    }
    parts_draw(app, c);

    canvas_set_font(c, FontSecondary);
    gfx_bmp(c, 1, 57, &bmp_arrow_l);
    gfx_bmp(c, 5, 57, &bmp_arrow_r);
    canvas_draw_str(c, 10, 63, "Freq");
    slider(c, 31, g->p_freq, 6);
    gfx_bmp(c, 66, 56, &bmp_arrow_u);
    gfx_bmp(c, 66, 60, &bmp_arrow_d);
    canvas_draw_str(c, 73, 63, "Amp");
    slider(c, 93, (uint8_t)((g->p_amp - 2) / 2), 6);
}

/* ================================================================ game over */

void gover_input(App* app, InputEvent* ev) {
    PlayState* g = &app->play;
    if(ev->type != InputTypeShort) return;
    if(ev->key == InputKeyOk) {
        fx_click(app);
        app_goto(app, game_scene(g->game), TransBlinds);
    } else if(ev->key == InputKeyBack) {
        fx_back(app);
        app_goto(app, SceneHome, TransBlinds);
    }
}

void gover_draw(App* app, Canvas* c) {
    PlayState* g = &app->play;
    gfx_title(c, game_titles[g->game % GAMES]);
    char buf[48];
    snprintf(buf, sizeof(buf), "%u", g->result);
    canvas_set_font(c, FontBigNumbers);
    gfx_str_center(c, 64, 35, buf);
    canvas_set_font(c, FontSecondary);
    if(g->best) {
        if((app->now / 300) & 1) {
            int32_t bw = canvas_string_width(c, "NEW BEST!") + 6;
            canvas_draw_rbox(c, 64 - bw / 2, 37, bw, 10, 2);
            canvas_set_color(c, ColorWhite);
            gfx_str_center(c, 64, 45, "NEW BEST!");
            canvas_set_color(c, ColorBlack);
        } else {
            gfx_str_center(c, 64, 45, "NEW BEST!");
        }
    } else {
        snprintf(buf, sizeof(buf), "Best %lu", (unsigned long)game_best(app, g->game));
        gfx_str_center(c, 64, 45, buf);
    }
    gfx_bmp(c, 2, 58, &bmp_star5);
    if(g->capped && g->g_xp == 0)
        snprintf(buf, sizeof(buf), "Daily XP max");
    else
        snprintf(buf, sizeof(buf), "+%d XP", g->g_xp);
    canvas_draw_str(c, 9, 63, buf);
    gfx_button_hint(c, 89, 63, &bmp_btn_ok, "Again");
}
