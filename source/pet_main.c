/*
 * App entry point: setup, main loop (about 30 fps), scene dispatch and
 * the animated transitions between scenes.
 */
#include "pet.h"

#define TRANS_MS 320

static void render_callback(Canvas* canvas, void* ctx);

static void input_callback(InputEvent* ev, void* ctx) {
    App* app = ctx;
    AppEvent e = {.type = EvInput, .input = *ev};
    furi_message_queue_put(app->queue, &e, 0);
}

static bool scene_is_hunting(App* app, Scene s) {
    return s == SceneScan || s == SceneCatch || (s == SceneCelebrate && app->celeb.ret == SceneScan);
}

static void scene_enter(App* app, Scene s) {
    Scene old = app->scene;
    if(scene_is_hunting(app, old) && !scene_is_hunting(app, s)) scan_exit(app);
    app->scene = s;
    switch(s) {
    case SceneBoot:
        boot_enter(app);
        break;
    case SceneHatch:
        hatch_enter(app);
        break;
    case SceneHome:
        home_enter(app);
        break;
    case SceneHuntPick:
        huntpick_enter(app);
        break;
    case SceneScan:
        if(old != SceneCatch && old != SceneCelebrate) scan_enter(app);
        break;
    case ScenePlayPick:
        playpick_enter(app);
        break;
    case SceneGameCatch:
        gcatch_enter(app);
        break;
    case SceneGameTune:
        gtune_enter(app);
        break;
    case SceneDex:
        if(old != SceneDexDetail) dex_enter(app);
        break;
    case SceneStats:
        stats_enter(app);
        break;
    case SceneSettings:
        if(old != SceneHelp && old != SceneConfirm && old != SceneName) settings_enter(app);
        break;
    case SceneHelp:
        help_enter(app);
        break;
    case SceneLog:
        if(old != SceneLogDetail) log_enter(app);
        break;
    default:
        break;
    }
}

void app_goto(App* app, Scene s, TransKind t) {
    if(t == TransNone) {
        scene_enter(app, s);
        app->trans = TransNone;
        return;
    }
    app->next_scene = s;
    app->trans = t;
    app->trans_t0 = app->now;
    app->trans_switched = false;
}

uint32_t ease_ms(App* app, uint32_t t0) {
    return app->now - t0;
}

/* ---------------------------------------------------------- transitions */

static void draw_transition(App* app, Canvas* c) {
    if(app->trans == TransNone) return;
    uint32_t el = app->now - app->trans_t0;
    float p = (float)el / TRANS_MS;
    if(p > 1) p = 1;
    float k = p < 0.5f ? p * 2 : (1 - p) * 2; /* coverage 0..1..0 */
    k = ease_in_out(k);
    canvas_set_color(c, ColorBlack);
    switch(app->trans) {
    case TransBlinds: {
        int32_t h = (int32_t)(8.0f * k + 0.5f);
        for(int32_t y = 0; y < SCREEN_H; y += 8)
            if(h > 0) canvas_draw_box(c, 0, y, SCREEN_W, h);
        break;
    }
    case TransIris: {
        int32_t r = (int32_t)((1.0f - k) * 74.0f);
        for(int32_t y = 0; y < SCREEN_H; y++) {
            int32_t dy = y - 32;
            int32_t w = r * r - dy * dy;
            w = w > 0 ? gfx_isqrt(w) : -1;
            if(w < 0) {
                canvas_draw_box(c, 0, y, SCREEN_W, 1);
            } else {
                gfx_hline(c, 0, y, 64 - w);
                gfx_hline(c, 64 + w, y, SCREEN_W);
            }
        }
        break;
    }
    case TransStatic: {
        if(k > 0.85f) {
            canvas_draw_box(c, 0, 0, SCREEN_W, SCREEN_H);
        } else {
            int32_t n = (int32_t)(k * 2600.0f);
            for(int32_t i = 0; i < n; i++) {
                uint32_t rnd = furi_hal_random_get();
                canvas_draw_dot(c, rnd % SCREEN_W, (rnd >> 8) % SCREEN_H);
            }
            /* a few torn scan lines */
            for(int32_t i = 0; i < (int32_t)(k * 6); i++) {
                uint32_t rnd = furi_hal_random_get();
                gfx_hline(c, (int32_t)(rnd % 40) - 20, (rnd >> 8) % SCREEN_H, 40 + (rnd >> 16) % 90);
            }
        }
        break;
    }
    default:
        break;
    }
}

static void update_transition(App* app) {
    if(app->trans == TransNone) return;
    uint32_t el = app->now - app->trans_t0;
    if(!app->trans_switched && el >= TRANS_MS / 2) {
        app->trans_switched = true;
        scene_enter(app, app->next_scene);
    }
    if(el >= TRANS_MS) app->trans = TransNone;
}

/* ---------------------------------------------------------- dispatch */

static void dispatch_input(App* app, InputEvent* ev) {
    if(ev->type == InputTypeLong && ev->key == InputKeyBack) {
        app->running = false;
        return;
    }
    if(app->trans != TransNone) return;
    switch(app->scene) {
    case SceneBoot:
        boot_input(app, ev);
        break;
    case SceneHatch:
        hatch_input(app, ev);
        break;
    case SceneName:
        name_input(app, ev);
        break;
    case SceneHome:
        home_input(app, ev);
        break;
    case SceneHuntPick:
        huntpick_input(app, ev);
        break;
    case SceneScan:
        scan_input(app, ev);
        break;
    case SceneCatch:
        catch_input(app, ev);
        break;
    case SceneCelebrate:
        celeb_input(app, ev);
        break;
    case ScenePlayPick:
        playpick_input(app, ev);
        break;
    case SceneGameCatch:
        gcatch_input(app, ev);
        break;
    case SceneGameTune:
        gtune_input(app, ev);
        break;
    case SceneGameOver:
        gover_input(app, ev);
        break;
    case SceneDex:
        dex_input(app, ev);
        break;
    case SceneDexDetail:
        dexd_input(app, ev);
        break;
    case SceneStats:
        stats_input(app, ev);
        break;
    case SceneSettings:
        settings_input(app, ev);
        break;
    case SceneHelp:
        help_input(app, ev);
        break;
    case SceneConfirm:
        confirm_input(app, ev);
        break;
    case SceneLog:
        log_input(app, ev);
        break;
    case SceneLogDetail:
        logd_input(app, ev);
        break;
    default:
        break;
    }
}

static void dispatch_update(App* app, uint32_t dt) {
    switch(app->scene) {
    case SceneBoot:
        boot_update(app, dt);
        break;
    case SceneHatch:
        hatch_update(app, dt);
        break;
    case SceneHome:
        home_update(app, dt);
        break;
    case SceneHuntPick:
        huntpick_update(app, dt);
        break;
    case SceneScan:
        scan_update(app, dt);
        break;
    case SceneCatch:
        catch_update(app, dt);
        break;
    case SceneCelebrate:
        celeb_update(app, dt);
        break;
    case SceneGameCatch:
        gcatch_update(app, dt);
        break;
    case SceneGameTune:
        gtune_update(app, dt);
        break;
    case SceneDex:
        dex_update(app, dt);
        break;
    case SceneStats:
        stats_update(app, dt);
        break;
    case SceneSettings:
        settings_update(app, dt);
        break;
    default:
        break;
    }
}

static void render_callback(Canvas* c, void* ctx) {
    App* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    canvas_clear(c);
    canvas_set_bitmap_mode(c, true);
    canvas_set_color(c, ColorBlack);
    canvas_set_font(c, FontSecondary);
    switch(app->scene) {
    case SceneBoot:
        boot_draw(app, c);
        break;
    case SceneHatch:
        hatch_draw(app, c);
        break;
    case SceneName:
        name_draw(app, c);
        break;
    case SceneHome:
        home_draw(app, c);
        break;
    case SceneHuntPick:
        huntpick_draw(app, c);
        break;
    case SceneScan:
        scan_draw(app, c);
        break;
    case SceneCatch:
        catch_draw(app, c);
        break;
    case SceneCelebrate:
        celeb_draw(app, c);
        break;
    case ScenePlayPick:
        playpick_draw(app, c);
        break;
    case SceneGameCatch:
        gcatch_draw(app, c);
        break;
    case SceneGameTune:
        gtune_draw(app, c);
        break;
    case SceneGameOver:
        gover_draw(app, c);
        break;
    case SceneDex:
        dex_draw(app, c);
        break;
    case SceneDexDetail:
        dexd_draw(app, c);
        break;
    case SceneStats:
        stats_draw(app, c);
        break;
    case SceneSettings:
        settings_draw(app, c);
        break;
    case SceneHelp:
        help_draw(app, c);
        break;
    case SceneConfirm:
        confirm_draw(app, c);
        break;
    case SceneLog:
        log_draw(app, c);
        break;
    case SceneLogDetail:
        logd_draw(app, c);
        break;
    default:
        break;
    }
    canvas_set_color(c, ColorBlack);
    draw_transition(app, c);
    toast_draw(app, c);
    furi_mutex_release(app->mutex);
}

/* ---------------------------------------------------------- clock */

static void update_clock(App* app) {
    uint32_t ts = state_now_ts();
    if(ts == app->rtc_last) return;
    uint32_t d = ts > app->rtc_last ? ts - app->rtc_last : 0;
    if(d > 120) d = 120; /* clock was changed */
    app->rtc_last = ts;
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    app->hour = dt.hour;
    app->minute = dt.minute;
    app->charging = furi_hal_power_is_charging();
    app->sim_sec += d;
    while(app->sim_sec >= 60) {
        app->sim_sec -= 60;
        bool was_asleep = app->save->asleep;
        state_tick_minute(app);
        if(was_asleep && !app->save->asleep && app->scene == SceneHome) {
            app->pet.act = ActWake;
            app->pet.act_t0 = app->now;
            app->pet.act_until = app->now + 1800;
            pet_say(app, "Good morning!");
            fx_sleep(app, false);
        }
    }
    app->save->last_ts = ts;
    if(ts >= app->save_due) {
        app->save_due = ts + 120;
        state_save(app);
    }
}

static void update_badge_toasts(App* app) {
    if(!app->new_badges || app->toast.active) return;
    if(app->scene == SceneBoot || app->scene == SceneHatch || app->scene == SceneCelebrate) return;
    for(uint8_t i = 0; i < BADGE_COUNT; i++) {
        uint32_t bit = 1u << i;
        if(app->new_badges & bit) {
            app->new_badges &= ~bit;
            char buf[TOAST_LEN];
            snprintf(buf, sizeof(buf), "Badge: %s", badge_names[i]);
            toast_show(app, buf, i + 1, 2400);
            fx_badge(app);
            return;
        }
    }
}

/* ---------------------------------------------------------- lifecycle */

static App* app_alloc(void) {
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->queue = furi_message_queue_alloc(16, sizeof(AppEvent));
    app->storage = furi_record_open(RECORD_STORAGE);
    app->notif = furi_record_open(RECORD_NOTIFICATION);
    app->gui = furi_record_open(RECORD_GUI);

    app->save = malloc(sizeof(SaveData));
    app->cat = malloc(sizeof(Catalog));
    catalog_build(app->cat);
    state_load(app);
    app->now = furi_get_tick();

    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    app->hour = dt.hour;
    app->minute = dt.minute;
    app->rtc_last = state_now_ts();
    app->save_due = app->rtc_last + 120;
    state_catch_up(app);

    fx_init(app);
    app->radio = radio_alloc(app->queue);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, render_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    if(app->save->set.backlight)
        notification_message(app->notif, &sequence_display_backlight_enforce_on);
    app->running = true;
    return app;
}

static void app_free(App* app) {
    radio_free(app->radio);
    fx_deinit(app);
    app->save->last_ts = state_now_ts();
    state_save(app);
    notification_message(app->notif, &sequence_reset_rgb);
    notification_message(app->notif, &sequence_display_backlight_enforce_auto);

    view_port_enabled_set(app->view_port, false);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);
    furi_message_queue_free(app->queue);
    furi_mutex_free(app->mutex);
    free(app->cat);
    free(app->save);
    free(app);
}

int32_t signal_pet_app(void* p) {
    UNUSED(p);
    App* app = app_alloc();

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app_goto(app, SceneBoot, TransNone);
    furi_mutex_release(app->mutex);

    uint32_t last = furi_get_tick();
    uint32_t last_frame = last;

    while(app->running) {
        uint32_t now = furi_get_tick();
        uint32_t since = now - last_frame;
        uint32_t wait = since >= FRAME_MS ? 0 : FRAME_MS - since;

        AppEvent ev;
        bool got = furi_message_queue_get(app->queue, &ev, wait) == FuriStatusOk;

        furi_mutex_acquire(app->mutex, FuriWaitForever);
        now = furi_get_tick();
        app->now = now;
        if(got) {
            if(ev.type == EvInput)
                dispatch_input(app, &ev.input);
            else if(ev.type == EvRadio && app->scene == SceneScan)
                scan_radio_event(app);
        }
        bool frame = now - last_frame >= FRAME_MS;
        if(frame) {
            uint32_t dt = now - last;
            if(dt > 100) dt = 100;
            last = now;
            last_frame = now;
            app->frame++;
            update_clock(app);
            if(scene_is_hunting(app, app->scene)) radio_tick(app->radio);
            if(app->scene == SceneScan) scan_radio_event(app);
            update_transition(app);
            dispatch_update(app, dt);
            parts_update(app, dt);
            update_badge_toasts(app);
        }
        fx_update(app);
        furi_mutex_release(app->mutex);

        if(frame) view_port_update(app->view_port);
    }

    app_free(app);
    return 0;
}
