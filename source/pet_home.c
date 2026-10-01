/*
 * The room: the pet lives here. Sky follows the real clock (sun and
 * drifting clouds by day, moon and twinkling stars at night). No hunger,
 * no health: the pet just grows with every signal you catch, and every
 * level tier unlocks a new move it shows off on its own.
 *
 *   Left/Right  move in the dock     OK    open
 *   Up          pet your pet         Down  chat
 *   Back        say goodbye and exit
 */
#include "pet.h"

#define GROUND_Y 50
#define DOCK_Y 53
#define DOCK_N 8
#define SLOT_W 16

static const char* const dock_labels[DOCK_N] = {
    "Hunt", "Play", "Clean", "Sleep", "Signal Dex", "Logbook", "Stats", "Settings"};

static const Bmp* dock_icon(uint8_t i) {
    static const Bmp* const icons[DOCK_N] = {
        &bmp_dock_hunt,
        &bmp_dock_play,
        &bmp_dock_clean,
        &bmp_dock_sleep,
        &bmp_dock_dex,
        &bmp_dock_log,
        &bmp_dock_stats,
        &bmp_dock_settings,
    };
    return icons[i % DOCK_N];
}

static const int8_t noise_x[4] = {12, 34, 94, 116};
static const uint8_t star_xy[9][2] = {
    {30, 13}, {52, 20}, {76, 12}, {98, 17}, {118, 24}, {40, 28}, {88, 30}, {64, 25}, {110, 11}};
static const uint8_t tufts[8][2] = {{6, 0}, {19, 1}, {33, 0}, {47, 1}, {74, 0}, {101, 1}, {115, 0}, {124, 1}};

/* tier move k (unlocked at tier k + 1) */
static const uint8_t tier_moves[TIER_MAX] = {
    ActScanRings,
    ActDance,
    ActSpin,
    ActJuggle,
    ActMoonwalk,
    ActBackflip,
    ActPowerUp,
    ActLevitate,
    ActTeleport,
    ActCheer, /* Legend Glow: a shower of stars */
};
static const uint16_t move_ms[TIER_MAX] = {1600, 1800, 900, 2200, 2400, 1000, 1500, 2700, 1300, 1600};

static uint32_t rnd(uint32_t n) {
    return furi_hal_random_get() % n;
}

static int32_t pet_top(App* app) {
    return GROUND_Y - pet_height(app->save->stage) - (int32_t)app->pet.lift;
}

/* ---------------------------------------------------------- pet brain */

void pet_anim_reset(App* app) {
    PetAnim* a = &app->pet;
    float x = a->x;
    memset(a, 0, sizeof(PetAnim));
    a->x = (x > 20 && x < 108) ? x : 64;
    a->target_x = a->x;
    a->next_blink = app->now + 1200;
    a->next_think = app->now + 1500;
    a->bubble_t0 = app->now;
}

void pet_say(App* app, const char* text) {
    strncpy(app->pet.say, text, sizeof(app->pet.say) - 1);
    app->pet.say[sizeof(app->pet.say) - 1] = '\0';
    app->pet.say_t0 = app->now;
}

static void set_act(App* app, uint8_t act, uint32_t ms) {
    PetAnim* a = &app->pet;
    a->act = act;
    a->act_t0 = app->now;
    a->act_until = app->now + ms;
    if(act == ActHop || act == ActCheer || act == ActBye) a->lift_v = 62.0f;
    if(act == ActBackflip) a->lift_v = 80.0f;
}

static float pick_x_away(float from) {
    float x;
    do {
        x = 24 + (float)rnd(81);
    } while(x > from - 28 && x < from + 28);
    return x;
}

static void start_move(App* app, uint8_t k) {
    PetAnim* a = &app->pet;
    uint8_t act = tier_moves[k % TIER_MAX];
    set_act(app, act, move_ms[k % TIER_MAX]);
    if(act == ActMoonwalk) {
        a->target_x = a->x > 64 ? a->x - 34 : a->x + 34;
    } else if(act == ActTeleport) {
        a->target_x = pick_x_away(a->x);
    } else if(k == 9) {
        for(uint8_t i = 0; i < 8; i++)
            parts_spawn(
                app, PartStar, a->x + (float)((int32_t)rnd(41) - 20), 12 + (float)rnd(10), 0, 18 + (float)rnd(12), 1500, false);
    }
}

void pet_make_pose(App* app, Pose* p) {
    SaveData* s = app->save;
    PetAnim* a = &app->pet;
    uint32_t now = app->now;
    pose_default(p, app);
    uint8_t need = state_mood_need(app);
    bool night = app->hour >= 22 || app->hour < 6;

    if(s->asleep) {
        p->eyes = EyeSleep;
        p->mouth = ((now / 1500) & 1) ? MouthO : MouthFlat;
        p->squash = ((now / 1500) & 1) ? 1 : 0;
        return;
    }

    if(need == 4)
        p->mouth = MouthFlat;
    else if(need == 1)
        p->mouth = (now / 2500) % 2 ? MouthO : MouthSmile;
    else
        p->mouth = (now / 4000) % 3 == 0 ? MouthTongue : MouthSmile;
    if(night && (now / 3000) % 4 == 0) p->eyes = EyeSleepy;

    p->look = a->look;
    p->squash = ((now / 1000) % 3 == 0) ? 1 : 0; /* breathing */
    p->glow = (now / 600) % (p->tier >= 1 ? 3 : 6) == 0 ? 1 : 0;
    p->lift = (int8_t)a->lift;

    uint32_t el = now - a->act_t0;
    switch(a->act) {
    case ActWalk:
    case ActMoonwalk:
        p->step = ((now / 140) & 1) + 1;
        p->shear = a->target_x > a->x ? 1 : -1;
        p->look = a->target_x > a->x ? 1 : -1;
        if(a->act == ActMoonwalk) {
            p->look = -p->look; /* walks one way, looks the other */
            p->shear = -p->shear;
            p->eyes = EyeHappy;
            p->mouth = MouthTongue;
        }
        p->squash = 0;
        break;
    case ActHop:
    case ActCheer:
        p->eyes = EyeHappy;
        p->mouth = MouthOpen;
        p->squash = a->lift > 0.5f ? -1 : 1;
        break;
    case ActDance:
        p->eyes = EyeHappy;
        p->mouth = MouthOpen;
        p->shear = ((el / 220) & 1) ? 2 : -2;
        p->step = ((el / 220) & 1) + 1;
        break;
    case ActScanRings:
        p->glow = 2;
        p->eyes = (el / 400) & 1 ? EyeOpen : EyeWide;
        p->mouth = MouthO;
        p->look = ((el / 500) & 1) ? 1 : -1;
        break;
    case ActSpin: {
        static const int8_t seq[6] = {-3, -1, 1, 3, 1, -1};
        p->shear = seq[(el / 60) % 6];
        p->look = p->shear > 0 ? 1 : -1;
        p->eyes = EyeHappy;
        p->mouth = MouthOpen;
        break;
    }
    case ActJuggle:
        p->eyes = EyeHappy;
        p->mouth = MouthSmile;
        p->shear = ((el / 300) & 1) ? 1 : -1;
        break;
    case ActBackflip: {
        static const int8_t rot[5] = {-1, -3, 0, 3, 1};
        if(a->lift > 0.5f) {
            p->shear = rot[(el / 70) % 5];
            p->squash = -2;
            p->eyes = EyeSleep;
            p->mouth = MouthO;
        } else {
            p->eyes = EyeHappy;
            p->mouth = MouthOpen;
            p->squash = 2;
        }
        break;
    }
    case ActPowerUp:
        if(el < 500) {
            p->squash = 2;
            p->shear = ((el / 50) & 1) ? 1 : -1;
            p->eyes = EyeSleep;
            p->mouth = MouthFlat;
        } else {
            p->squash = -1;
            p->eyes = EyeWide;
            p->mouth = MouthBig;
        }
        p->glow = 2;
        break;
    case ActLevitate:
        p->eyes = EyeSleep;
        p->mouth = MouthO;
        p->glow = 2;
        break;
    case ActTeleport:
        p->eyes = EyeWide;
        p->mouth = MouthO;
        break;
    case ActPetted:
        p->eyes = EyeHappy;
        p->blush = true;
        p->mouth = (el / 300) & 1 ? MouthOpen : MouthSmile;
        p->shear = ((el / 120) & 1) ? 1 : -1;
        p->squash = 1;
        break;
    case ActYawn:
        if(el < 1000) {
            p->eyes = EyeSleep;
            p->mouth = MouthBig;
            p->squash = -1;
        }
        break;
    case ActRefuse:
        p->eyes = EyeSad;
        p->mouth = MouthFrown;
        p->shear = ((el / 70) & 1) ? 1 : -1;
        p->look = ((el / 140) & 1) ? 1 : -1;
        break;
    case ActWake:
        if(el < 700) {
            p->eyes = EyeSleep;
            p->mouth = MouthBig;
        } else if(el < 1000) {
            p->eyes = EyeBlink;
            p->mouth = MouthO;
        } else {
            p->eyes = EyeHappy;
            p->mouth = MouthOpen;
        }
        break;
    case ActBye:
        p->eyes = EyeHappy;
        p->mouth = MouthOpen;
        p->shear = ((el / 150) & 1) ? 2 : -2;
        break;
    default:
        break;
    }

    if(now < a->blink_until && (p->eyes == EyeOpen || p->eyes == EyeSad)) p->eyes = EyeBlink;
}

static bool ticked(uint32_t now, uint32_t dt, uint32_t period) {
    return (now / period) != ((now - dt) / period);
}

static void pet_brain(App* app, uint32_t dt) {
    SaveData* s = app->save;
    PetAnim* a = &app->pet;
    uint32_t now = app->now;
    float sec = (float)dt / 1000.0f;
    uint32_t el = now - a->act_t0;

    if(now >= a->next_blink) {
        a->blink_until = now + 120;
        a->next_blink = now + 1600 + rnd(3600);
        if(rnd(5) == 0) a->next_blink = now + 260; /* double blink */
    }
    if(a->look && now >= a->look_until) a->look = 0;

    /* jumping physics */
    if(a->act == ActHop || a->act == ActCheer || a->act == ActBye || a->act == ActBackflip) {
        a->lift_v -= (a->act == ActBackflip ? 300.0f : 330.0f) * sec;
        a->lift += a->lift_v * sec;
        if(a->lift <= 0) {
            a->lift = 0;
            if(a->lift_v < -1 && pet_tier(s->level) >= 7) {
                app->home.shock_t0 = now;
                app->home.shock_x = a->x;
            }
            if(a->act != ActBackflip && now < a->act_until) {
                a->lift_v = 62.0f;
            } else if(a->act == ActBackflip && a->lift_v < -1) {
                a->lift_v = 0;
                parts_burst(app, PartDust, a->x, GROUND_Y - 1, 5, 25, false);
            }
            if(a->act != ActBackflip) {
                parts_spawn(app, PartDust, a->x - 6, GROUND_Y - 1, -8, -6, 300, false);
                parts_spawn(app, PartDust, a->x + 6, GROUND_Y - 1, 8, -6, 300, false);
            }
        }
    } else if(a->act == ActLevitate) {
        float target = el < 500 ? (float)el / 500.0f * 9.0f :
                       el < 2100 ? 9.0f + (float)gfx_sin((int32_t)(el / 40)) * 1.5f / 64.0f :
                                   9.0f * (1.0f - (float)(el - 2100) / 600.0f);
        a->lift = target < 0 ? 0 : target;
        if(ticked(now, dt, 120)) parts_spawn(app, PartDust, a->x + (float)((int32_t)rnd(13) - 6), GROUND_Y - 1, 0, -10, 500, false);
    } else if(a->act == ActPowerUp) {
        a->lift = el > 500 && el < 1300 ? 3.0f : 0;
        if(el >= 500 && el - dt < 500) {
            parts_burst(app, PartStar, a->x, (float)pet_top(app) + 10, 8, 45, false);
            parts_burst(app, PartSpark, a->x, (float)pet_top(app) + 12, 5, 30, false);
        }
    } else {
        a->lift = 0;
    }

    if(s->asleep) {
        a->act = ActIdle;
        if(ticked(now, dt, 1100)) parts_spawn(app, PartZ, a->x + 8, (float)pet_top(app) + 4, 6, -9, 1800, false);
        return;
    }

    if(app->charging && ticked(now, dt, 1700))
        parts_spawn(app, PartSpark, a->x + (float)((int32_t)rnd(11) - 5), (float)pet_top(app) + 1, 0, 0, 260, false);

    if(a->act != ActIdle && a->act != ActWalk && now >= a->act_until && a->lift <= 0) {
        if(a->act == ActTeleport) a->x = a->target_x;
        a->act = ActIdle;
    }

    if((a->act == ActWalk || a->act == ActMoonwalk) && pet_tier(s->level) >= 5 && ticked(now, dt, 110)) {
        float back = a->target_x > a->x ? -9.0f : 9.0f;
        if(a->act == ActMoonwalk) back = -back;
        parts_spawn(app, PartBit, a->x + back, GROUND_Y - 2 - (float)rnd(4), 0, -5, 550, false);
    }
    if(a->act == ActWalk || a->act == ActMoonwalk) {
        float dx = a->target_x - a->x;
        float step = (a->act == ActMoonwalk ? 13.0f : 16.0f) * sec;
        if(dx > -step && dx < step) {
            a->x = a->target_x;
            a->act = ActIdle;
        } else {
            a->x += dx > 0 ? step : -step;
        }
    }
    if(a->act == ActTeleport && el >= 550 && el - dt < 550) {
        parts_burst(app, PartSpark, a->x, GROUND_Y - 12, 5, 30, false);
        a->x = a->target_x;
        parts_burst(app, PartSpark, a->x, GROUND_Y - 12, 5, 30, false);
    }
    if(a->act == ActSpin && ticked(now, dt, 120))
        parts_spawn(app, PartDust, a->x + (rnd(2) ? 9 : -9), GROUND_Y - 1, 0, -6, 300, false);
    if(a->act == ActDance && ticked(now, dt, 400))
        parts_spawn(app, PartNote, a->x + (rnd(2) ? 12 : -12), (float)pet_top(app) + 6, 0, -14, 1200, false);
    if(a->act == ActPetted && ticked(now, dt, 250))
        parts_spawn(app, PartHeart, a->x + (float)((int32_t)rnd(17) - 8), (float)pet_top(app) + 4, 0, -16, 1100, false);

    if(a->act == ActIdle && now >= a->next_think) {
        uint8_t tier = pet_tier(s->level);
        bool night = app->hour >= 22 || app->hour < 6;
        uint32_t r = rnd(100);
        if(tier && r < 38) {
            start_move(app, (uint8_t)rnd(tier));
        } else if(r < 70) {
            a->target_x = 22 + (float)rnd(85);
            a->act = ActWalk;
            a->act_t0 = now;
        } else if(r < 80) {
            set_act(app, ActHop, 700);
        } else if(r < 86 && night) {
            set_act(app, ActYawn, 1300);
        } else {
            a->look = rnd(2) ? 1 : -1;
            a->look_until = now + 900 + rnd(900);
        }
        a->next_think = now + 1600 + rnd(2400);
    }

    uint8_t need = state_mood_need(app);
    if(need && now - a->bubble_t0 > 7500) {
        a->bubble_t0 = now;
        a->bubble = need;
    }
}

/* ---------------------------------------------------------- scene */

void home_enter(App* app) {
    HomeState* h = &app->home;
    h->sel_x = h->sel * SLOT_W;
    h->clean_t0 = 0;
    h->quitting = false;
    pet_anim_reset(app);
    parts_clear(app);
    h->tip_at = 0;
    if(app->save->stage == StBaby && app->save->catches == 0) h->tip_at = app->now + 2600;
}

static void go_to_sleep(App* app) {
    app->save->asleep = 1;
    app->save->sleep_ts = state_now_ts();
    app->home.sleep_fx_t0 = app->now;
    app->home.sleep_fx_to_dark = true;
    app->pet.act = ActIdle;
    pet_say(app, "Good night!");
    fx_sleep(app, true);
}

static void wake_up(App* app) {
    SaveData* s = app->save;
    s->asleep = 0;
    app->home.sleep_fx_t0 = app->now;
    app->home.sleep_fx_to_dark = false;
    fx_sleep(app, false);
    set_act(app, ActWake, 1600);
    /* dream bonus: 1 XP per 10 minutes of sleep, up to 40 */
    uint32_t now = state_now_ts();
    uint32_t mins = s->sleep_ts && now > s->sleep_ts ? (now - s->sleep_ts) / 60 : 0;
    uint32_t xp = mins / 10;
    if(xp > 40) xp = 40;
    if(xp) {
        char buf[TOAST_LEN];
        snprintf(buf, sizeof(buf), "Sweet dreams! +%lu XP", (unsigned long)xp);
        toast_show(app, buf, 0, 2400);
        state_add_xp(app, xp);
    }
    s->sleep_ts = 0;
    pet_say(app, "Good morning!");
    state_save(app);
}

static void chat_line(App* app, char* out, size_t len) {
    SaveData* s = app->save;
    switch(state_mood_need(app)) {
    case 1:
        snprintf(out, len, "Let's hunt new signals!");
        return;
    case 4:
        snprintf(out, len, "Ew, static everywhere!");
        return;
    default:
        break;
    }
    if(app->charging) {
        snprintf(out, len, "Mmm, USB power snacks!");
        return;
    }
    uint8_t tier = pet_tier(s->level);
    switch((app->now / 1000) % 5) {
    case 0:
        snprintf(out, len, "%lu XP to level %u!", (unsigned long)(state_xp_need(s->level) - s->xp), s->level + 1);
        break;
    case 1:
        if(tier < TIER_MAX)
            snprintf(out, len, "New move at level %u!", tier_level(tier + 1));
        else
            snprintf(out, len, "I am a legend now!");
        break;
    case 2:
        snprintf(out, len, app->hour >= 22 || app->hour < 6 ? "Look at all the stars!" : "I hear radio waves...");
        break;
    case 3:
        snprintf(out, len, "%u signal types found!", s->species_n);
        break;
    default:
        snprintf(out, len, "Beep boop!");
        break;
    }
}

static void activate(App* app, uint8_t i) {
    SaveData* s = app->save;
    switch(i) {
    case 0:
        if(s->asleep) wake_up(app);
        fx_click(app);
        app_goto(app, SceneHuntPick, TransIris);
        break;
    case 1:
        if(s->asleep) wake_up(app);
        fx_click(app);
        app_goto(app, ScenePlayPick, TransBlinds);
        break;
    case 2:
        if(s->noise == 0) {
            pet_say(app, "Already spotless!");
            fx_refuse(app);
        } else if(!app->home.clean_t0) {
            app->home.clean_t0 = app->now;
            app->home.clean_n = s->noise;
            fx_clean(app);
        }
        break;
    case 3:
        if(s->asleep)
            wake_up(app);
        else
            go_to_sleep(app);
        break;
    case 4:
        fx_click(app);
        app_goto(app, SceneDex, TransBlinds);
        break;
    case 5:
        fx_click(app);
        app_goto(app, SceneLog, TransBlinds);
        break;
    case 6:
        fx_click(app);
        app_goto(app, SceneStats, TransBlinds);
        break;
    default:
        fx_click(app);
        app_goto(app, SceneSettings, TransBlinds);
        break;
    }
}

void home_input(App* app, InputEvent* ev) {
    HomeState* h = &app->home;
    SaveData* s = app->save;
    if(h->quitting) return;
    if(ev->type != InputTypeShort && ev->type != InputTypeRepeat) return;
    switch(ev->key) {
    case InputKeyLeft:
        h->sel = (h->sel + DOCK_N - 1) % DOCK_N;
        h->label_t0 = app->now;
        fx_click(app);
        break;
    case InputKeyRight:
        h->sel = (h->sel + 1) % DOCK_N;
        h->label_t0 = app->now;
        fx_click(app);
        break;
    case InputKeyOk:
        if(ev->type == InputTypeShort) activate(app, h->sel);
        break;
    case InputKeyUp:
        if(ev->type != InputTypeShort) break;
        if(s->asleep) {
            pet_say(app, "Shh... Zzz...");
            break;
        }
        set_act(app, ActPetted, 1300);
        fx_purr(app);
        s->pets++;
        state_badges_check(app);
        break;
    case InputKeyDown: {
        if(ev->type != InputTypeShort) break;
        char buf[28];
        if(s->asleep)
            snprintf(buf, sizeof(buf), "Zzz... zzz...");
        else
            chat_line(app, buf, sizeof(buf));
        pet_say(app, buf);
        fx_click(app);
        break;
    }
    case InputKeyBack:
        if(ev->type != InputTypeShort) break;
        h->quitting = true;
        h->quit_t0 = app->now;
        if(!s->asleep) set_act(app, ActBye, 900);
        pet_say(app, s->asleep ? "Zzz... bye..." : "Bye bye! Come back soon!");
        fx_bye(app);
        break;
    default:
        break;
    }
}

void home_update(App* app, uint32_t dt) {
    HomeState* h = &app->home;
    SaveData* s = app->save;
    float target = h->sel * SLOT_W;
    float k = (float)dt * 0.018f;
    if(k > 1) k = 1;
    h->sel_x += (target - h->sel_x) * k;
    if(h->sel_x > target - 0.3f && h->sel_x < target + 0.3f) h->sel_x = target;

    pet_brain(app, dt);

    if(h->clean_t0) {
        uint32_t el = app->now - h->clean_t0;
        float bx = -12.0f + (float)el * 0.105f;
        for(uint8_t i = 0; i < 4; i++) {
            if(i < s->noise && bx > noise_x[i] && bx - (float)dt * 0.105f <= noise_x[i]) {
                parts_burst(app, PartSpark, noise_x[i] + 3, GROUND_Y - 3, 3, 20, false);
                parts_burst(app, PartBit, noise_x[i] + 3, GROUND_Y - 3, 4, 30, true);
            }
        }
        if(ticked(app->now, dt, 60)) parts_spawn(app, PartDust, bx - 2, GROUND_Y - 1 - rnd(3), -12, -8, 400, false);
        if(el > 1450) {
            h->clean_t0 = 0;
            s->cleans++;
            char buf[TOAST_LEN];
            snprintf(buf, sizeof(buf), "All clean! +%u XP", 5 * h->clean_n);
            toast_show(app, buf, 0, 2000);
            state_add_xp(app, 5 * h->clean_n);
            s->noise = 0;
            if(!s->asleep) set_act(app, ActCheer, 900);
            pet_say(app, "So fresh and clean!");
            state_badges_check(app);
            state_save(app);
        }
    }

    if(h->tip_at && (int32_t)(app->now - h->tip_at) >= 0 && !app->toast.active) {
        h->tip_at = 0;
        if(!s->asleep) pet_say(app, "Feed me! Pick Hunt below");
    }
    if(h->quitting && app->now - h->quit_t0 > 950) app->running = false;
    if(app->trans != TransNone || h->quitting) return;
    if(app->pending_levels || app->pending_evolve) {
        celeb_start(app, SceneHome);
    } else if(app->new_tier && !app->toast.active) {
        /* a freshly unlocked move: announce it and show it off */
        uint8_t t = app->new_tier;
        app->new_tier = 0;
        char buf[TOAST_LEN];
        snprintf(buf, sizeof(buf), "New move: %s!", move_names[(t - 1) % TIER_MAX]);
        toast_show(app, buf, 0, 2600);
        fx_badge(app);
        if(!s->asleep) start_move(app, t - 1);
    }
}

/* ---------------------------------------------------------- drawing */

static void draw_sky(App* app, Canvas* c) {
    uint32_t t = app->now;
    bool night = app->hour >= 20 || app->hour < 6;
    canvas_set_color(c, ColorBlack);
    if(!night) {
        int32_t sx = 14, sy = 20;
        if(app->hour >= 17 || app->hour < 8) sy = 26; /* low sun */
        canvas_draw_circle(c, sx, sy, 4);
        for(int32_t a = 0; a < 64; a += 8) {
            int32_t aa = a + (int32_t)(t / 160);
            canvas_draw_dot(c, sx + gfx_sin(aa) * 6 / 64, sy + gfx_cos(aa) * 6 / 64);
        }
        int32_t cx = (int32_t)((t / 90) % 170) - 22;
        if(cx >= -14 && cx < 128) {
            for(uint8_t j = 0; j < bmp_cloud.h; j++)
                for(uint8_t i = 0; i < bmp_cloud.w; i++)
                    if(cx + i >= 0 && cx + i < 128 && (bmp_cloud.data[j * 2 + i / 8] & (1 << (i % 8))))
                        canvas_draw_dot(c, cx + i, 14 + j);
        }
        int32_t cx2 = (int32_t)((t / 140 + 90) % 170) - 22;
        if(cx2 >= 0 && cx2 < 114) gfx_bmp(c, cx2, 28, &bmp_cloud);
    } else {
        gfx_bmp(c, 10, 13, &bmp_moon);
        for(uint8_t i = 0; i < 9; i++) {
            int32_t x = star_xy[i][0], y = star_xy[i][1];
            canvas_draw_dot(c, x, y);
            if((t / 300 + i * 5) % 12 == 0) {
                canvas_draw_dot(c, x - 1, y);
                canvas_draw_dot(c, x + 1, y);
                canvas_draw_dot(c, x, y - 1);
                canvas_draw_dot(c, x, y + 1);
            }
        }
    }
}

static void draw_ground(Canvas* c) {
    canvas_set_color(c, ColorBlack);
    canvas_draw_line(c, 0, GROUND_Y, 127, GROUND_Y);
    for(uint8_t i = 0; i < 8; i++) {
        int32_t x = tufts[i][0];
        if(tufts[i][1] == 0) {
            canvas_draw_dot(c, x, GROUND_Y - 1);
            canvas_draw_dot(c, x + 2, GROUND_Y - 2);
            canvas_draw_dot(c, x + 4, GROUND_Y - 1);
        } else {
            canvas_draw_dot(c, x, GROUND_Y - 1);
            canvas_draw_dot(c, x + 1, GROUND_Y - 2);
        }
    }
    for(int32_t x = 1; x < 128; x += 4)
        canvas_draw_dot(c, x, GROUND_Y + 1);
}

static void draw_noise(App* app, Canvas* c) {
    canvas_set_color(c, ColorBlack);
    for(uint8_t i = 0; i < app->save->noise && i < 4; i++) {
        if(app->home.clean_t0) {
            float bx = -12.0f + (float)(app->now - app->home.clean_t0) * 0.105f;
            if(bx > noise_x[i]) continue;
        }
        int32_t jx = ((app->now / 90 + i) % 7 == 0) ? 1 : 0;
        const Bmp* b = ((app->now / 200 + i) & 1) ? &bmp_glitch1 : &bmp_glitch2;
        gfx_bmp(c, noise_x[i] + jx, GROUND_Y - 1 - b->h, b);
    }
}

void home_scene_backdrop(App* app, Canvas* c, bool with_ground) {
    draw_sky(app, c);
    if(with_ground) draw_ground(c);
}

static void draw_topbar(App* app, Canvas* c) {
    SaveData* s = app->save;
    PetAnim* a = &app->pet;
    HomeState* h = &app->home;
    /* solid bar: a jumping pet slides behind it instead of through the text */
    canvas_set_color(c, ColorWhite);
    canvas_draw_box(c, 0, 0, 128, 10);
    canvas_set_color(c, ColorBlack);
    canvas_set_font(c, FontSecondary);
    uint32_t say_el = app->now - a->say_t0;
    if(a->say[0] && say_el < 3000) {
        /* the pet talks: a speech line over the whole top bar */
        canvas_set_color(c, ColorWhite);
        canvas_draw_box(c, 0, 0, 128, 10);
        canvas_set_color(c, ColorBlack);
        gfx_bmp(c, 1, 1, &bmp_speech);
        char buf[28];
        size_t n = say_el / 28;
        size_t len = strlen(a->say);
        if(n > len) n = len;
        memcpy(buf, a->say, n);
        buf[n] = '\0';
        canvas_draw_str(c, 12, 8, buf);
        return;
    }
    if(app->now - h->label_t0 < 1300 && h->label_t0) {
        const char* label = (h->sel == 3 && s->asleep) ? "Wake up" : dock_labels[h->sel];
        char buf[16];
        size_t n = (app->now - h->label_t0) / 35 + 1;
        size_t len = strlen(label);
        if(n > len) n = len;
        memcpy(buf, label, n);
        buf[n] = '\0';
        gfx_bmp(c, 1, 2, &bmp_arrow_r);
        canvas_draw_str(c, 6, 8, buf);
    } else {
        char lv[8];
        snprintf(lv, sizeof(lv), "Lv%u", s->level);
        int32_t w = canvas_string_width(c, lv) + 4;
        /* long names are shortened so they never touch the XP bar */
        char nm[NAME_LEN + 2];
        snprintf(nm, sizeof(nm), "%s", s->name);
        gfx_fit_str(c, nm, sizeof(nm), 72 - 5 - w - 3);
        canvas_draw_str(c, 1, 8, nm);
        int32_t x = 3 + canvas_string_width(c, nm);
        canvas_draw_rbox(c, x, 0, w, 10, 2);
        canvas_set_color(c, ColorWhite);
        canvas_draw_str(c, x + 2, 8, lv);
        canvas_set_color(c, ColorBlack);
    }
    /* XP towards the next level */
    uint32_t need = state_xp_need(s->level);
    gfx_bmp(c, 74, 2, &bmp_star5);
    gfx_bar(c, 81, 2, 46, 6, (uint8_t)(s->level >= 99 ? 100 : s->xp * 100 / (need ? need : 1)));
}

static void draw_bubble(App* app, Canvas* c, int32_t px, int32_t top) {
    PetAnim* a = &app->pet;
    uint32_t el = app->now - a->bubble_t0;
    if(!a->bubble || el > 2600 || app->save->asleep) return;
    bool left = px > 64;
    int32_t bw = 15, bh = 13;
    int32_t bx = left ? px - 14 - bw : px + 14;
    int32_t by = top - 4;
    if(by < 11) by = 11;
    if(el < 120) {
        canvas_draw_disc(c, left ? px - 10 : px + 10, top + 4, 1);
        return;
    }
    canvas_set_color(c, ColorWhite);
    canvas_draw_rbox(c, bx, by, bw, bh, 3);
    canvas_set_color(c, ColorBlack);
    canvas_draw_rframe(c, bx, by, bw, bh, 3);
    int32_t tx = left ? bx + bw + 1 : bx - 3;
    canvas_draw_disc(c, tx + 1, by + bh, 1);
    canvas_draw_dot(c, left ? tx + 3 : tx - 1, by + bh + 3);
    const Bmp* ic = a->bubble == 4 ? &bmp_glitch1 : &bmp_src_subghz;
    gfx_bmp(c, bx + (bw - ic->w) / 2, by + (bh - ic->h) / 2, ic);
}

/* move effects drawn around the pet */
static void draw_move_fx(App* app, Canvas* c, int32_t px, int32_t top) {
    PetAnim* a = &app->pet;
    uint32_t el = app->now - a->act_t0;
    canvas_set_color(c, ColorBlack);
    if(a->act == ActScanRings) {
        int32_t ay = top - 6;
        for(int32_t k = 0; k < 3; k++) {
            int32_t r = (int32_t)((el / 18 + k * 9) % 27) + 3;
            for(int32_t ang = 0; ang < 64; ang += 2) {
                int32_t x = px + gfx_cos(ang) * r / 64, y = ay + gfx_sin(ang) * r / 2 / 64;
                if(y > 10 && y < GROUND_Y && x >= 0 && x < 128 && gfx_sin(ang) < 0) canvas_draw_dot(c, x, y);
            }
        }
    } else if(a->act == ActJuggle) {
        for(int32_t k = 0; k < 3; k++) {
            int32_t ph = (int32_t)(el / 10 + k * 21) % 64; /* each ball: an arc over the head */
            int32_t x = px - 12 + ph * 24 / 64;
            int32_t y = top - 2 - gfx_sin(ph / 2) * 12 / 64;
            if(((el / 10 + k * 21) / 64) & 1) x = px + 12 - ph * 24 / 64;
            if(y > 11 && x > 1 && x < 126) canvas_draw_box(c, x - 1, y - 1, 3, 3);
        }
    } else if(a->act == ActPowerUp && el >= 500) {
        int32_t r = (int32_t)((el - 500) / 12);
        if(r < 40) canvas_draw_circle(c, px, top + 12, r);
    }
}

/* dissolve the pet into pixels (teleport) */
static void dissolve(Canvas* c, int32_t px, int32_t top, float k) {
    canvas_set_color(c, ColorWhite);
    uint32_t thr = (uint32_t)(k * 1000.0f);
    for(int32_t y = top - 8; y < GROUND_Y; y++) {
        if(y < 11) continue;
        for(int32_t x = px - 22; x <= px + 22; x++) {
            if(x < 0 || x > 127) continue;
            uint32_t h = (uint32_t)(x * 73856093) ^ (uint32_t)(y * 19349663);
            h ^= h >> 11;
            if(h % 1000 < thr) canvas_draw_dot(c, x, y);
        }
    }
    canvas_set_color(c, ColorBlack);
}

static void draw_dock(App* app, Canvas* c) {
    HomeState* h = &app->home;
    canvas_set_color(c, ColorBlack);
    canvas_draw_box(c, 0, DOCK_Y, 128, 11);
    for(uint8_t i = 0; i < DOCK_N; i++)
        gfx_bmp_color(c, i * SLOT_W + 3, DOCK_Y + 1, dock_icon(i), ColorWhite);
    int32_t x = (int32_t)(h->sel_x + 0.5f);
    canvas_set_color(c, ColorWhite);
    canvas_draw_rbox(c, x + 1, DOCK_Y, 14, 11, 2);
    canvas_set_color(c, ColorBlack);
    /* icons under the moving highlight turn black where it covers them */
    for(uint8_t i = 0; i < DOCK_N; i++) {
        int32_t ix = i * SLOT_W + 3;
        if(ix + 9 <= x + 1 || ix >= x + 15) continue;
        const Bmp* b = dock_icon(i);
        for(uint8_t j = 0; j < b->h; j++)
            for(uint8_t k = 0; k < b->w; k++)
                if((b->data[j * ((b->w + 7) / 8) + k / 8] & (1 << (k % 8))) && ix + k > x + 1 && ix + k < x + 14)
                    canvas_draw_dot(c, ix + k, DOCK_Y + 1 + j);
    }
}

void home_draw(App* app, Canvas* c) {
    SaveData* s = app->save;
    HomeState* h = &app->home;
    PetAnim* a = &app->pet;

    draw_sky(app, c);
    draw_ground(c);
    draw_noise(app, c);

    Pose p;
    pet_make_pose(app, &p);
    int32_t px = (int32_t)(a->x + 0.5f);
    int32_t top = GROUND_Y - pet_height(s->stage) - p.lift;
    draw_pet(c, px, GROUND_Y, &p);
    if(a->act == ActTeleport) {
        uint32_t el = app->now - a->act_t0;
        float k = el < 550 ? (float)el / 550.0f : 1.0f - (float)(el - 550) / 600.0f;
        if(k > 0) dissolve(c, px, top, k > 1 ? 1 : k);
    }
    draw_move_fx(app, c, px, top);
    if(h->shock_t0 && app->now - h->shock_t0 < 450) {
        /* flat ring racing outwards on the ground */
        int32_t r = 6 + (int32_t)((app->now - h->shock_t0) / 14);
        int32_t sx = (int32_t)h->shock_x;
        for(int32_t ang = 0; ang < 64; ang += 2) {
            int32_t x = sx + gfx_cos(ang) * r / 64, y = GROUND_Y + gfx_sin(ang) * r / 6 / 64;
            if(x >= 0 && x < 128 && y > 11 && y < DOCK_Y) canvas_draw_dot(c, x, y);
        }
    }
    if(pet_tier(s->level) >= 8 && !s->asleep && (app->now % 3700) < 150) {
        /* a little lightning bolt from the antenna */
        static const int8_t zz[5][2] = {{0, -1}, {2, -3}, {-1, -5}, {1, -7}, {-1, -9}};
        int32_t ox = ((app->now / 3700) & 1) ? 2 : -2;
        for(uint8_t i = 0; i < 4; i++) {
            int32_t x0 = px + ox + zz[i][0], y0 = top + zz[i][1] - 2;
            int32_t x1 = px + ox + zz[i + 1][0], y1 = top + zz[i + 1][1] - 2;
            for(int32_t k = 0; k <= 2; k++) {
                int32_t x = x0 + (x1 - x0) * k / 2, y = y0 + (y1 - y0) * k / 2;
                if(x >= 0 && x < 128 && y > 10 && y < 64) canvas_draw_dot(c, x, y);
            }
        }
    }
    draw_bubble(app, c, px, top);

    if(h->clean_t0) {
        int32_t bx = -12 + (int32_t)((float)(app->now - h->clean_t0) * 0.105f);
        int32_t bob = ((app->now / 80) & 1);
        if(bx >= 0 && bx < 118) gfx_bmp(c, bx, GROUND_Y - 13 - bob, &bmp_broom);
    }
    parts_draw(app, c);
    draw_topbar(app, c);

    /* lights off: invert the room above the dock */
    uint32_t fx_el = app->now - h->sleep_fx_t0;
    int32_t room_h = DOCK_Y;
    if(h->sleep_fx_t0 && fx_el < 500) {
        int32_t edge = (int32_t)(ease_in_out((float)fx_el / 500.0f) * room_h);
        if(h->sleep_fx_to_dark)
            gfx_invert(c, 0, 0, 128, edge);
        else
            gfx_invert(c, 0, edge, 128, room_h - edge);
    } else if(s->asleep) {
        gfx_invert(c, 0, 0, 128, room_h);
    }

    draw_dock(app, c);
}
