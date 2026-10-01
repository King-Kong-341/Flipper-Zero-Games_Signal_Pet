/*
 * Menu screens: naming, Signal Dex, stats & badges, settings, help and
 * the reset confirmation.
 */
#include "pet.h"

extern const Bmp* const badge_icons[BADGE_COUNT];

static uint32_t rnd(uint32_t n) {
    return furi_hal_random_get() % n;
}

/* Title with page arrows: "<  PROFILE  >" */
static void paged_title(App* app, Canvas* c, const char* title) {
    gfx_title(c, title);
    if((app->now / 500) & 1) {
        gfx_bmp(c, 1, 3, &bmp_arrow_l);
        gfx_bmp(c, 124, 3, &bmp_arrow_r);
    }
}

/* ================================================================ name */

/* On-screen keyboard: 3 rows x 10 keys. The last row ends with four
 * special keys: space, delete, dice (random name) and done. */
#define KB_COLS 10
#define KB_ROWS 3
#define KB_X 4
#define KB_Y 19
#define KB_W 12
#define KB_H 11

enum {
    KeySpace = 6,
    KeyDel,
    KeyDice,
    KeyDone,
};

static const char kb_letters[KB_ROWS][KB_COLS + 1] = {"ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ    "};

static void name_set_text(NameState* n, const char* s) {
    strncpy(n->text, s, NAME_MAX);
    n->text[NAME_MAX] = '\0';
    n->len = (uint8_t)strlen(n->text);
}

void name_enter(App* app, bool first) {
    NameState* n = &app->name_st;
    n->first = first;
    n->t0 = app->now;
    n->key_t0 = 0;
    n->shake_t0 = 0;
    n->row = 2;
    n->col = KeyDone;
    n->idx = (uint8_t)rnd(pet_names_n);
    if(first)
        name_set_text(n, pet_names[n->idx]);
    else
        name_set_text(n, app->save->name);
}

static void name_type(NameState* n, char ch) {
    if(n->len >= NAME_MAX) return;
    if(ch == ' ' && (n->len == 0 || n->text[n->len - 1] == ' ')) return;
    /* first letter of every word upper case, the rest lower case */
    bool upper = n->len == 0 || n->text[n->len - 1] == ' ';
    if(ch >= 'A' && ch <= 'Z' && !upper) ch = (char)(ch - 'A' + 'a');
    n->text[n->len++] = ch;
    n->text[n->len] = '\0';
}

static void name_done(App* app) {
    NameState* n = &app->name_st;
    while(n->len && n->text[n->len - 1] == ' ')
        n->text[--n->len] = '\0';
    if(n->len == 0) {
        n->shake_t0 = app->now;
        fx_refuse(app);
        return;
    }
    strncpy(app->save->name, n->text, NAME_LEN - 1);
    app->save->name[NAME_LEN - 1] = '\0';
    app->save->named = 1;
    state_save(app);
    fx_hatch(app);
    if(n->first) {
        char buf[TOAST_LEN];
        snprintf(buf, sizeof(buf), "Welcome, %s!", app->save->name);
        toast_show(app, buf, 0, 2000);
        app_goto(app, SceneHome, TransIris);
    } else {
        app_goto(app, SceneSettings, TransBlinds);
    }
}

void name_input(App* app, InputEvent* ev) {
    NameState* n = &app->name_st;
    if(ev->key == InputKeyOk && ev->type == InputTypeLong) {
        name_done(app);
        return;
    }
    if(ev->type != InputTypeShort && ev->type != InputTypeRepeat) return;
    switch(ev->key) {
    case InputKeyLeft:
        n->col = (n->col + KB_COLS - 1) % KB_COLS;
        fx_click(app);
        break;
    case InputKeyRight:
        n->col = (n->col + 1) % KB_COLS;
        fx_click(app);
        break;
    case InputKeyUp:
        n->row = (n->row + KB_ROWS - 1) % KB_ROWS;
        fx_click(app);
        break;
    case InputKeyDown:
        n->row = (n->row + 1) % KB_ROWS;
        fx_click(app);
        break;
    case InputKeyOk:
        n->key_t0 = app->now;
        if(n->row == 2 && n->col >= KeySpace) {
            switch(n->col) {
            case KeySpace:
                name_type(n, ' ');
                fx_click(app);
                break;
            case KeyDel:
                if(n->len) n->text[--n->len] = '\0';
                fx_back(app);
                break;
            case KeyDice:
                n->idx = (uint8_t)((n->idx + 1 + rnd(pet_names_n - 1)) % pet_names_n);
                name_set_text(n, pet_names[n->idx]);
                n->t0 = app->now;
                fx_click(app);
                break;
            default:
                if(ev->type == InputTypeShort) name_done(app);
                break;
            }
        } else {
            name_type(n, kb_letters[n->row][n->col]);
            fx_click(app);
        }
        break;
    case InputKeyBack:
        if(ev->type != InputTypeShort) break;
        if(n->len) {
            n->text[--n->len] = '\0';
            fx_back(app);
        } else if(!n->first) {
            fx_back(app);
            app_goto(app, SceneSettings, TransBlinds);
        }
        break;
    default:
        break;
    }
}

static const Bmp* special_icon(uint8_t col) {
    switch(col) {
    case KeySpace:
        return &bmp_key_space;
    case KeyDel:
        return &bmp_key_del;
    case KeyDice:
        return &bmp_key_dice;
    default:
        return &bmp_key_done;
    }
}

void name_draw(App* app, Canvas* c) {
    NameState* n = &app->name_st;
    uint32_t now = app->now;

    /* name field, shakes when an empty name is refused */
    int32_t sx = 0;
    if(n->shake_t0 && now - n->shake_t0 < 400) sx = ((now - n->shake_t0) / 50) & 1 ? 2 : -2;
    canvas_draw_rframe(c, 2 + sx, 1, 124, 15, 3);
    if(n->len == 0) {
        canvas_set_font(c, FontSecondary);
        gfx_str_center(c, 64 + sx, 11, n->first ? "Name your pet" : "Type a name");
    } else {
        canvas_set_font(c, FontPrimary);
        int32_t w = canvas_string_width(c, n->text);
        int32_t x = 64 - w / 2 + sx;
        /* a fresh dice name drops in */
        int32_t dy = now - n->t0 < 160 ? (int32_t)((160 - (now - n->t0)) / 40) : 0;
        canvas_draw_str(c, x, 12 - dy, n->text);
        if((now / 400) & 1 && n->len < NAME_MAX) gfx_hline(c, x + w + 2, 12, 5);
    }
    canvas_set_font(c, FontSecondary);

    /* keys */
    for(uint8_t r = 0; r < KB_ROWS; r++) {
        for(uint8_t k = 0; k < KB_COLS; k++) {
            int32_t x = KB_X + k * KB_W, y = KB_Y + r * KB_H;
            bool sel = r == n->row && k == n->col;
            bool pressed = sel && n->key_t0 && now - n->key_t0 < 120;
            bool special = r == 2 && k >= KeySpace;
            if(sel) {
                if(pressed)
                    canvas_draw_rframe(c, x, y, KB_W - 1, KB_H - 1, 2);
                else
                    canvas_draw_rbox(c, x, y, KB_W - 1, KB_H - 1, 2);
                if(!pressed) canvas_set_color(c, ColorWhite);
            } else if(special) {
                canvas_draw_rframe(c, x, y, KB_W - 1, KB_H - 1, 2);
            }
            if(special) {
                const Bmp* b = special_icon(k);
                gfx_bmp(c, x + (KB_W - 1 - b->w) / 2, y + (KB_H - 1 - b->h) / 2, b);
            } else {
                char ch[2] = {kb_letters[r][k], '\0'};
                gfx_str_center(c, x + 6, y + 8, ch);
            }
            canvas_set_color(c, ColorBlack);
        }
    }

    /* what does the selected key do? */
    const char* hint = "OK: type   Back: erase";
    if(n->row == 2 && n->col >= KeySpace) {
        static const char* const sh[4] = {"Add a space", "Erase last letter", "Random name", "Done! Save this name"};
        hint = sh[n->col - KeySpace];
    }
    gfx_hline(c, 0, 53, 128);
    gfx_str_center(c, 64, 62, hint);
}

/* ================================================================ dex */

#define DEX_ROW_Y 22
#define DEX_ROW_H 10
#define DEX_ROWS 4

void dex_enter(App* app) {
    DexState* d = &app->dex;
    d->sel = 0;
    d->top = 0;
}

void dex_input(App* app, InputEvent* ev) {
    DexState* d = &app->dex;
    if(ev->type != InputTypeShort && ev->type != InputTypeRepeat) return;
    int16_t n = app->cat->count[d->tab];
    switch(ev->key) {
    case InputKeyLeft:
        d->tab = (d->tab + SrcCount - 1) % SrcCount;
        d->sel = d->top = 0;
        fx_click(app);
        break;
    case InputKeyRight:
        d->tab = (d->tab + 1) % SrcCount;
        d->sel = d->top = 0;
        fx_click(app);
        break;
    case InputKeyUp:
        if(n) d->sel = (d->sel + n - 1) % n;
        fx_click(app);
        break;
    case InputKeyDown:
        if(n) d->sel = (d->sel + 1) % n;
        fx_click(app);
        break;
    case InputKeyOk:
        if(ev->type == InputTypeShort && n) {
            fx_click(app);
            app_goto(app, SceneDexDetail, TransBlinds);
        }
        break;
    case InputKeyBack:
        if(ev->type == InputTypeShort) {
            fx_back(app);
            app_goto(app, SceneHome, TransBlinds);
        }
        break;
    default:
        break;
    }
    if(d->sel < d->top) d->top = d->sel;
    if(d->sel >= d->top + DEX_ROWS) d->top = d->sel - DEX_ROWS + 1;
}

void dex_update(App* app, uint32_t dt) {
    DexState* d = &app->dex;
    float k = (float)dt * 0.02f;
    if(k > 1) k = 1;
    d->tab_f += ((float)d->tab - d->tab_f) * k;
}

void dex_draw(App* app, Canvas* c) {
    DexState* d = &app->dex;
    Catalog* cat = app->cat;
    canvas_set_color(c, ColorBlack);
    /* tabs */
    int32_t hx = 1 + (int32_t)(d->tab_f * 25.0f + 0.5f);
    canvas_draw_rbox(c, hx, 0, 25, 11, 2);
    for(uint8_t i = 0; i < SrcCount; i++) {
        int32_t x = 1 + i * 25;
        bool on = (hx + 12 > x && hx + 12 < x + 25);
        gfx_bmp_color(c, x + 9, 2, source_icon(i), on ? ColorWhite : ColorBlack);
    }
    gfx_hline(c, 0, 11, 128);

    canvas_set_font(c, FontSecondary);
    canvas_draw_str(c, 2, 20, source_names[d->tab]);
    char buf[48];
    snprintf(buf, sizeof(buf), "%u/%u found", species_found(app, d->tab), cat->count[d->tab]);
    gfx_str_right(c, 126, 20, buf);

    int16_t n = cat->count[d->tab];
    if(n == 0) {
        gfx_str_center(c, 64, 42, "Nothing here");
        return;
    }
    for(int16_t r = 0; r < DEX_ROWS; r++) {
        int16_t i = d->top + r;
        if(i >= n) break;
        const CatEntry* e = &cat->e[cat->first[d->tab] + i];
        SpeciesRec* sp = species_get(app->save, e->hash);
        int32_t y = DEX_ROW_Y + r * DEX_ROW_H;
        bool sel = i == d->sel;
        if(sel) {
            canvas_draw_rbox(c, 0, y, 123, DEX_ROW_H, 2);
            canvas_set_color(c, ColorWhite);
        }
        snprintf(buf, sizeof(buf), "%02d", i + 1);
        canvas_draw_str(c, 3, y + 8, buf);
        if(sp) {
            char nm[28];
            snprintf(nm, sizeof(nm), "%s", e->name);
            gfx_fit_str(c, nm, sizeof(nm), 116 - 6 * e->rarity - 17 - 2);
            canvas_draw_str(c, 17, y + 8, nm);
            for(uint8_t s = 0; s < e->rarity; s++)
                gfx_bmp(c, 116 - s * 6, y + 2, &bmp_star5);
        } else {
            canvas_draw_str(c, 17, y + 8, "? ? ? ? ?");
        }
        canvas_set_color(c, ColorBlack);
    }
    gfx_scrollbar(c, 125, DEX_ROW_Y, DEX_ROWS * DEX_ROW_H, d->top, n, DEX_ROWS);
}

void dexd_input(App* app, InputEvent* ev) {
    DexState* d = &app->dex;
    if(ev->type != InputTypeShort && ev->type != InputTypeRepeat) return;
    int16_t n = app->cat->count[d->tab];
    if(ev->key == InputKeyUp && n) {
        d->sel = (d->sel + n - 1) % n;
        fx_click(app);
    } else if(ev->key == InputKeyDown && n) {
        d->sel = (d->sel + 1) % n;
        fx_click(app);
    } else if((ev->key == InputKeyBack || ev->key == InputKeyOk) && ev->type == InputTypeShort) {
        fx_back(app);
        app_goto(app, SceneDex, TransBlinds);
    }
    if(d->sel < d->top) d->top = d->sel;
    if(d->sel >= d->top + DEX_ROWS) d->top = d->sel - DEX_ROWS + 1;
}

void dexd_draw(App* app, Canvas* c) {
    DexState* d = &app->dex;
    Catalog* cat = app->cat;
    const CatEntry* e = &cat->e[cat->first[d->tab] + d->sel];
    SpeciesRec* sp = species_get(app->save, e->hash);
    char buf[48];

    canvas_set_font(c, FontPrimary);
    if(sp) {
        snprintf(buf, sizeof(buf), "%s", e->name);
        gfx_fit_str(c, buf, sizeof(buf), 110);
    } else {
        snprintf(buf, sizeof(buf), "???");
    }
    gfx_title(c, buf);

    canvas_draw_rframe(c, 2, 17, 30, 30, 4);
    gfx_bmp(c, 5, 20, source_big(e->src));
    if(!sp) {
        canvas_set_color(c, ColorWhite);
        gfx_dither(c, 5, 20, 24, 24, 0);
        canvas_set_color(c, ColorBlack);
    }
    snprintf(buf, sizeof(buf), "No. %02d", d->sel + 1);
    canvas_set_font(c, FontSecondary);
    gfx_str_center(c, 17, 57, buf);

    int32_t x = 38;
    canvas_draw_str(c, x, 23, source_names[e->src]);
    gfx_stars(c, 97, 18, e->rarity);
    canvas_draw_str(c, x, 33, rank_name(e->rarity));
    if(!sp) {
        canvas_draw_str(c, x, 43, "Not found yet!");
        canvas_draw_str(c, x, 53, "Keep hunting...");
        return;
    }
    uint16_t specimens = 0;
    for(uint16_t i = 0; i < app->save->specimens_n; i++)
        if(app->save->specimens[i].species == e->hash) specimens++;
    snprintf(buf, sizeof(buf), "Eaten %u times", sp->count);
    canvas_draw_str(c, x, 43, buf);
    snprintf(buf, sizeof(buf), "%u different signal%s", specimens, specimens == 1 ? "" : "s");
    canvas_draw_str(c, x, 53, buf);
    DateTime dt;
    datetime_timestamp_to_datetime(sp->first_ts, &dt);
    snprintf(buf, sizeof(buf), "First %02u.%02u.%04u", dt.day, dt.month, dt.year);
    canvas_draw_str(c, x, 63, buf);
}

/* ================================================================ stats */

#define STATS_PAGES 4

void stats_enter(App* app) {
    app->stats.page = 0;
    app->stats.badge_sel = 0;
}

void stats_input(App* app, InputEvent* ev) {
    StatsState* s = &app->stats;
    if(ev->type != InputTypeShort && ev->type != InputTypeRepeat) return;
    bool badges = s->page == STATS_PAGES - 1;
    switch(ev->key) {
    case InputKeyLeft:
        if(badges && s->badge_sel % 8) {
            s->badge_sel--;
        } else {
            s->page = (s->page + STATS_PAGES - 1) % STATS_PAGES;
        }
        fx_click(app);
        break;
    case InputKeyRight:
        if(badges && s->badge_sel % 8 != 7) {
            s->badge_sel++;
        } else {
            s->page = (s->page + 1) % STATS_PAGES;
            if(s->page == STATS_PAGES - 1) s->badge_sel = 0;
        }
        fx_click(app);
        break;
    case InputKeyUp:
    case InputKeyDown:
        if(badges) {
            s->badge_sel ^= 8;
            fx_click(app);
        }
        break;
    case InputKeyBack:
    case InputKeyOk:
        if(ev->type == InputTypeShort) {
            fx_back(app);
            app_goto(app, SceneHome, TransBlinds);
        }
        break;
    default:
        break;
    }
}

void stats_update(App* app, uint32_t dt) {
    UNUSED(app);
    UNUSED(dt);
}


static void stats_profile(App* app, Canvas* c) {
    SaveData* s = app->save;
    char buf[48];
    Pose p;
    pet_make_pose(app, &p);
    p.lift = 0;
    draw_pet(c, 24, 61, &p);

    int32_t x = 52;
    canvas_set_font(c, FontPrimary);
    char nm[NAME_LEN + 2];
    snprintf(nm, sizeof(nm), "%s", s->name);
    gfx_fit_str(c, nm, sizeof(nm), 126 - x);
    canvas_draw_str(c, x, 22, nm);
    canvas_set_font(c, FontSecondary);
    canvas_draw_str(c, x, 31, state_form_name(s->stage, s->form));
    snprintf(buf, sizeof(buf), "Lv %u", s->level);
    gfx_str_right(c, 126, 31, buf);
    uint32_t need = state_xp_need(s->level);
    gfx_bar(c, x, 35, 75, 5, (uint8_t)(s->level >= 99 ? 100 : s->xp * 100 / (need ? need : 1)));
    if(s->level >= 99)
        snprintf(buf, sizeof(buf), "Max level!");
    else
        snprintf(buf, sizeof(buf), "XP %lu/%lu", (unsigned long)s->xp, (unsigned long)need);
    canvas_draw_str(c, x, 49, buf);
    snprintf(buf, sizeof(buf), "Moves %u/%u", pet_tier(s->level), TIER_MAX);
    canvas_draw_str(c, x, 59, buf);
}

static void stats_diet(App* app, Canvas* c) {
    SaveData* s = app->save;
    uint32_t mx = 1;
    for(uint8_t i = 0; i < SrcCount; i++)
        if(s->diet[i] > mx) mx = s->diet[i];
    char buf[12];
    canvas_set_font(c, FontSecondary);
    for(uint8_t i = 0; i < SrcCount; i++) {
        int32_t y = 15 + i * 10;
        gfx_bmp(c, 2, y, source_icon(i));
        canvas_draw_str(c, 12, y + 7, source_short[i]);
        gfx_bar(c, 54, y + 1, 48, 5, (uint8_t)(s->diet[i] * 100 / mx));
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)s->diet[i]);
        gfx_str_right(c, 127, y + 7, buf);
    }
}

static void stats_records(App* app, Canvas* c) {
    SaveData* s = app->save;
    char buf[48];
    canvas_set_font(c, FontSecondary);
    static const char* const lbl[5] = {"Signals eaten", "Species found", "Moves unlocked", "Best games", "Age"};
    for(uint8_t i = 0; i < 5; i++) {
        int32_t y = 23 + i * 10;
        canvas_draw_str(c, 2, y, lbl[i]);
        switch(i) {
        case 0:
            snprintf(buf, sizeof(buf), "%lu", (unsigned long)s->catches);
            break;
        case 1:
            snprintf(buf, sizeof(buf), "%u/%u", s->species_n, app->cat->n);
            break;
        case 2:
            snprintf(buf, sizeof(buf), "%u/%u", pet_tier(s->level), TIER_MAX);
            break;
        case 3:
            snprintf(
                buf,
                sizeof(buf),
                "%lu/%lu/%u",
                (unsigned long)s->best_catch,
                (unsigned long)s->best_tune,
                s->best_hop);
            break;
        default: {
            uint32_t d = state_age_days(app);
            snprintf(buf, sizeof(buf), "%lu day%s", (unsigned long)d, d == 1 ? "" : "s");
            break;
        }
        }
        gfx_str_right(c, 126, y, buf);
    }
}

static void stats_badges(App* app, Canvas* c) {
    StatsState* st = &app->stats;
    uint32_t got = app->save->badges;
    for(uint8_t i = 0; i < BADGE_COUNT; i++) {
        int32_t x = 4 + (i % 8) * 15, y = 15 + (i / 8) * 15;
        bool on = got & (1u << i);
        bool sel = i == st->badge_sel;
        if(sel) {
            canvas_draw_rbox(c, x, y, 13, 13, 3);
            canvas_set_color(c, ColorWhite);
        } else {
            canvas_draw_rframe(c, x, y, 13, 13, 3);
        }
        if(on)
            gfx_bmp(c, x + 3, y + 3, badge_icons[i]);
        else
            gfx_bmp(c, x + 4, y + 3, &bmp_lock);
        canvas_set_color(c, ColorBlack);
    }
    uint8_t i = st->badge_sel;
    bool on = got & (1u << i);
    canvas_set_font(c, FontPrimary);
    gfx_str_center(c, 64, 53, on ? badge_names[i] : "Locked");
    canvas_set_font(c, FontSecondary);
    gfx_str_center(c, 64, 63, badge_descs[i]);
}

void stats_draw(App* app, Canvas* c) {
    static const char* const titles[STATS_PAGES] = {"PROFILE", "DIET", "RECORDS", "BADGES"};
    StatsState* s = &app->stats;
    if(s->page == 3) {
        char t[20];
        uint8_t n = 0;
        for(uint8_t i = 0; i < BADGE_COUNT; i++)
            if(app->save->badges & (1u << i)) n++;
        snprintf(t, sizeof(t), "BADGES %u/%u", n, BADGE_COUNT);
        paged_title(app, c, t);
    } else {
        paged_title(app, c, titles[s->page]);
    }
    switch(s->page) {
    case 0:
        stats_profile(app, c);
        break;
    case 1:
        stats_diet(app, c);
        break;
    case 2:
        stats_records(app, c);
        break;
    default:
        stats_badges(app, c);
        break;
    }
}

/* ================================================================ settings */

enum {
    SetSound,
    SetVibro,
    SetLed,
    SetLight,
    SetBand,
    SetName,
    SetHelp,
    SetReset,
    SetCount,
};

#define SET_ROW_Y 14
#define SET_ROW_H 10
#define SET_ROWS 4

static const char* const set_labels[SetCount] = {
    "Sound",
    "Vibration",
    "LED light",
    "Screen light",
    "Radio band",
    "Pet name",
    "How to play",
    "Reset pet",
};

/* one plain-English line per setting, shown under the list */
static const char* const set_help[SetCount] = {
    "Beeps and little songs",
    "Buzz on big moments",
    "Colored light on hunts",
    "Keep the screen bright",
    "All = every Sub-GHz band",
    "Type a new name",
    "Quick guide, 8 pages",
    "Start over with a new egg",
};

void settings_enter(App* app) {
    SettingsState* s = &app->settings;
    s->sel = 0;
    s->top = 0;
    Settings* st = &app->save->set;
    s->knob[0] = st->sound;
    s->knob[1] = st->vibro;
    s->knob[2] = st->led;
}

static void settings_change(App* app, int8_t dir) {
    SettingsState* ss = &app->settings;
    Settings* st = &app->save->set;
    switch(ss->sel) {
    case SetSound:
        st->sound ^= 1;
        if(!st->sound) fx_stop_all(app);
        break;
    case SetVibro:
        st->vibro ^= 1;
        break;
    case SetLed:
        st->led ^= 1;
        if(!st->led) fx_scan_led(app, 0, false);
        break;
    case SetLight:
        st->backlight ^= 1;
        notification_message(
            app->notif,
            st->backlight ? &sequence_display_backlight_enforce_on : &sequence_display_backlight_enforce_auto);
        break;
    case SetBand:
        st->band = (st->band + BandCount + dir) % BandCount;
        break;
    case SetName:
        name_enter(app, false);
        app_goto(app, SceneName, TransBlinds);
        return;
    case SetHelp:
        app_goto(app, SceneHelp, TransBlinds);
        return;
    default:
        app->confirm_sel = 0;
        app_goto(app, SceneConfirm, TransBlinds);
        return;
    }
    state_save(app);
}

void settings_input(App* app, InputEvent* ev) {
    SettingsState* s = &app->settings;
    if(ev->type != InputTypeShort && ev->type != InputTypeRepeat) return;
    switch(ev->key) {
    case InputKeyUp:
        s->sel = (s->sel + SetCount - 1) % SetCount;
        fx_click(app);
        break;
    case InputKeyDown:
        s->sel = (s->sel + 1) % SetCount;
        fx_click(app);
        break;
    case InputKeyLeft:
    case InputKeyRight:
        if(s->sel < SetName) {
            settings_change(app, ev->key == InputKeyLeft ? -1 : 1);
            fx_click(app);
        }
        break;
    case InputKeyOk:
        if(ev->type == InputTypeShort) {
            settings_change(app, 1);
            fx_click(app);
        }
        break;
    case InputKeyBack:
        if(ev->type == InputTypeShort) {
            fx_back(app);
            app_goto(app, SceneHome, TransBlinds);
        }
        break;
    default:
        break;
    }
    if(s->sel < s->top) s->top = s->sel;
    if(s->sel >= s->top + SET_ROWS) s->top = s->sel - SET_ROWS + 1;
}

void settings_update(App* app, uint32_t dt) {
    SettingsState* s = &app->settings;
    Settings* st = &app->save->set;
    uint8_t v[3] = {st->sound, st->vibro, st->led};
    float k = (float)dt * 0.02f;
    if(k > 1) k = 1;
    for(uint8_t i = 0; i < 3; i++)
        s->knob[i] += ((float)v[i] - s->knob[i]) * k;
}

static void toggle(Canvas* c, int32_t x, int32_t y, float knob, bool inv) {
    /* pill switch 15x8, knob slides */
    Color fg = inv ? ColorWhite : ColorBlack;
    Color bg = inv ? ColorBlack : ColorWhite;
    canvas_set_color(c, fg);
    if(knob > 0.5f)
        canvas_draw_rbox(c, x, y, 15, 8, 3);
    else
        canvas_draw_rframe(c, x, y, 15, 8, 3);
    int32_t kx = x + 2 + (int32_t)(knob * 7.0f + 0.5f);
    canvas_set_color(c, knob > 0.5f ? bg : fg);
    canvas_draw_box(c, kx, y + 2, 4, 4);
    canvas_set_color(c, fg);
}

void settings_draw(App* app, Canvas* c) {
    SettingsState* s = &app->settings;
    Settings* st = &app->save->set;
    static const char* const band[BandCount] = {"433 MHz", "868 MHz", "315 MHz", "All"};
    gfx_title(c, "SETTINGS");
    canvas_set_font(c, FontSecondary);
    for(int8_t r = 0; r < SET_ROWS; r++) {
        int8_t i = s->top + r;
        if(i >= SetCount) break;
        int32_t y = SET_ROW_Y + r * SET_ROW_H;
        bool sel = i == s->sel;
        if(sel) {
            canvas_draw_rbox(c, 0, y, 123, SET_ROW_H, 2);
            canvas_set_color(c, ColorWhite);
        }
        canvas_draw_str(c, 3, y + 8, set_labels[i]);
        const char* val = NULL;
        switch(i) {
        case SetSound:
        case SetVibro:
        case SetLed:
            toggle(c, 105, y + 1, s->knob[i], sel);
            break;
        case SetLight:
            val = st->backlight ? "Always" : "Normal";
            break;
        case SetBand:
            val = band[st->band % BandCount];
            break;
        case SetName:
            val = app->save->name;
            break;
        default:
            gfx_bmp_color(c, 117, y + 2, &bmp_arrow_r, sel ? ColorWhite : ColorBlack);
            break;
        }
        if(sel) canvas_set_color(c, ColorWhite);
        if(val) {
            if(i < SetName && sel) {
                int32_t w = canvas_string_width(c, val);
                gfx_bmp_color(c, 114 - w - 5, y + 2, &bmp_arrow_l, ColorWhite);
                gfx_bmp_color(c, 117, y + 2, &bmp_arrow_r, ColorWhite);
                canvas_set_color(c, ColorWhite);
                gfx_str_right(c, 114, y + 8, val);
            } else {
                gfx_str_right(c, 120, y + 8, val);
            }
        }
        canvas_set_color(c, ColorBlack);
    }
    gfx_scrollbar(c, 125, SET_ROW_Y, SET_ROWS * SET_ROW_H, s->top, SetCount, SET_ROWS);
    canvas_set_font(c, FontSecondary);
    gfx_bmp(c, 2, 56, &bmp_info);
    canvas_draw_str(c, 10, 62, set_help[s->sel]);
}

/* ================================================================ help */

typedef struct {
    const char* title;
    const char* lines[4];
} HelpPage;

static const HelpPage help_pages[] = {
    {"WELCOME", {"Your pet lives on radio", "signals. Hunt them with", "Flipper's antennas to", "feed it and help it grow."}},
    {"HUNTING", {"Sub-GHz: keys, gates", "NFC: bank & transit cards", "RFID: door fobs, chips", "IR: remotes  iButton: keys"}},
    {"CARE", {"No hunger, it can't die.", "Rare signals = big XP!", "Static halves XP: Clean!", "Games: max 30 XP a day."}},
    {"RANKS", {"New species gives XP:", "Common 15  Uncommon 30", "Rare 60     Epic 100", "Legendary 160 XP!"}},
    {"GROWING", {"Lv 5: teen, Lv 10: adult.", "Its diet decides which", "of 6 forms it becomes.", "New moves up to Lv 99!"}},
    {"EXTRAS", {"Band All sweeps every", "Sub-GHz band for you.", "CC1101 board on GPIO:", "used for Sub-GHz hunts"}},
    {"CONTROLS", {"Left/Right: pick in dock", "OK: open    Up: pet it", "Down: chat  Back: exit", "Logbook: every catch"}},
    {"ABOUT", {"Signal Pet v1.1", "Made by King-Kong-341", "for the Flipper Zero.", "Happy hunting!"}},
};
#define HELP_N (sizeof(help_pages) / sizeof(help_pages[0]))

void help_enter(App* app) {
    app->help.page = 0;
}

void help_input(App* app, InputEvent* ev) {
    if(ev->type != InputTypeShort) return;
    if(ev->key == InputKeyLeft) {
        app->help.page = (app->help.page + HELP_N - 1) % HELP_N;
        fx_click(app);
    } else if(ev->key == InputKeyRight || ev->key == InputKeyOk) {
        app->help.page = (app->help.page + 1) % HELP_N;
        fx_click(app);
    } else if(ev->key == InputKeyBack) {
        fx_back(app);
        app_goto(app, SceneSettings, TransBlinds);
    }
}

void help_draw(App* app, Canvas* c) {
    const HelpPage* p = &help_pages[app->help.page % HELP_N];
    paged_title(app, c, p->title);
    canvas_set_font(c, FontSecondary);
    for(uint8_t i = 0; i < 4; i++)
        canvas_draw_str(c, 3, 24 + i * 10, p->lines[i]);
    gfx_dots(c, 64, 61, HELP_N, app->help.page);
}

/* ================================================================ confirm */

void confirm_input(App* app, InputEvent* ev) {
    if(ev->type != InputTypeShort) return;
    if(ev->key == InputKeyLeft || ev->key == InputKeyRight) {
        app->confirm_sel ^= 1;
        fx_click(app);
    } else if(ev->key == InputKeyOk) {
        if(app->confirm_sel) {
            state_reset(app);
            fx_sleep(app, true);
            app_goto(app, SceneHatch, TransIris);
        } else {
            fx_back(app);
            app_goto(app, SceneSettings, TransBlinds);
        }
    } else if(ev->key == InputKeyBack) {
        fx_back(app);
        app_goto(app, SceneSettings, TransBlinds);
    }
}

void confirm_draw(App* app, Canvas* c) {
    gfx_title(c, "RESET PET?");
    canvas_set_font(c, FontSecondary);
    char buf[48];
    snprintf(buf, sizeof(buf), "%s, its Dex and all", app->save->name);
    gfx_str_center(c, 64, 26, buf);
    gfx_str_center(c, 64, 36, "badges will be gone.");
    static const char* const b[2] = {"Keep", "Reset"};
    for(uint8_t i = 0; i < 2; i++) {
        int32_t x = 14 + i * 56, y = 46, w = 44, h = 14;
        if(app->confirm_sel == i) {
            canvas_draw_rbox(c, x, y, w, h, 3);
            canvas_set_color(c, ColorWhite);
        } else {
            canvas_draw_rframe(c, x, y, w, h, 3);
        }
        gfx_str_center(c, x + w / 2, y + 10, b[i]);
        canvas_set_color(c, ColorBlack);
    }
}

/* ================================================================ logbook */

#define LOG_ROW_Y 14
#define LOG_ROW_H 10
#define LOG_ROWS 5

static const char* const kind_names[4] = {"New species!", "New signal", "Seen again", "Too soon"};

void log_enter(App* app) {
    app->log_sel = 0;
    app->log_top = 0;
}

static void log_move(App* app, int16_t d) {
    int16_t n = app->save->log_n;
    if(!n) return;
    app->log_sel = (app->log_sel + n + d) % n;
    if(app->log_sel < app->log_top) app->log_top = app->log_sel;
    if(app->log_sel >= app->log_top + LOG_ROWS) app->log_top = app->log_sel - LOG_ROWS + 1;
}

void log_input(App* app, InputEvent* ev) {
    if(ev->type != InputTypeShort && ev->type != InputTypeRepeat) return;
    switch(ev->key) {
    case InputKeyUp:
        log_move(app, -1);
        fx_click(app);
        break;
    case InputKeyDown:
        log_move(app, 1);
        fx_click(app);
        break;
    case InputKeyOk:
        if(ev->type == InputTypeShort && app->save->log_n) {
            fx_click(app);
            app_goto(app, SceneLogDetail, TransBlinds);
        }
        break;
    case InputKeyBack:
        if(ev->type == InputTypeShort) {
            fx_back(app);
            app_goto(app, SceneHome, TransBlinds);
        }
        break;
    default:
        break;
    }
}

void log_draw(App* app, Canvas* c) {
    gfx_title(c, "LOGBOOK");
    canvas_set_font(c, FontSecondary);
    int16_t n = app->save->log_n;
    if(n == 0) {
        gfx_str_center(c, 64, 32, "No catches yet.");
        gfx_str_center(c, 64, 44, "Go hunting with your pet!");
        return;
    }
    char buf[28];
    for(int16_t r = 0; r < LOG_ROWS; r++) {
        int16_t i = app->log_top + r;
        const LogEntry* e = log_get(app, i);
        if(!e) break;
        int32_t y = LOG_ROW_Y + r * LOG_ROW_H;
        bool sel = i == app->log_sel;
        if(sel) {
            canvas_draw_rbox(c, 0, y, 123, LOG_ROW_H, 2);
            canvas_set_color(c, ColorWhite);
        }
        gfx_bmp(c, 2, y + 2, source_icon(e->src));
        time_ago(e->ts, buf, sizeof(buf));
        int32_t tw = canvas_string_width(c, buf);
        gfx_str_right(c, 120, y + 8, buf);
        int32_t right = 120 - tw - 3;
        if(e->kind == CatchNewSpecies) {
            gfx_bmp(c, right - 5, y + 2, &bmp_star5);
            right -= 8;
        }
        char nm[24];
        snprintf(nm, sizeof(nm), "%s", e->proto);
        gfx_fit_str(c, nm, sizeof(nm), right - 12);
        canvas_draw_str(c, 12, y + 8, nm);
        canvas_set_color(c, ColorBlack);
    }
    gfx_scrollbar(c, 125, LOG_ROW_Y, LOG_ROWS * LOG_ROW_H, app->log_top, n, LOG_ROWS);
}

void logd_input(App* app, InputEvent* ev) {
    if(ev->type != InputTypeShort && ev->type != InputTypeRepeat) return;
    if(ev->key == InputKeyUp) {
        log_move(app, -1);
        fx_click(app);
    } else if(ev->key == InputKeyDown) {
        log_move(app, 1);
        fx_click(app);
    } else if((ev->key == InputKeyBack || ev->key == InputKeyOk) && ev->type == InputTypeShort) {
        fx_back(app);
        app_goto(app, SceneLog, TransBlinds);
    }
}

void logd_draw(App* app, Canvas* c) {
    const LogEntry* e = log_get(app, app->log_sel);
    if(!e) return;
    char buf[32];
    canvas_set_font(c, FontPrimary);
    snprintf(buf, sizeof(buf), "%s", e->proto);
    gfx_fit_str(c, buf, sizeof(buf), 110);
    gfx_title(c, buf);

    canvas_draw_rframe(c, 2, 17, 30, 30, 4);
    gfx_bmp(c, 5, 20, source_big(e->src));
    canvas_set_font(c, FontSecondary);
    snprintf(buf, sizeof(buf), "%d/%u", app->log_sel + 1, app->save->log_n);
    gfx_str_center(c, 17, 57, buf);

    int32_t x = 38;
    canvas_draw_str(c, x, 23, source_names[e->src % SrcCount]);
    gfx_stars(c, 97, 18, e->rarity);
    canvas_draw_str(c, x, 33, kind_names[e->kind % 4]);
    snprintf(buf, sizeof(buf), "%s", e->detail[0] ? e->detail : "-");
    gfx_fit_str(c, buf, sizeof(buf), 127 - x);
    canvas_draw_str(c, x, 43, buf);
    DateTime dt;
    datetime_timestamp_to_datetime(e->ts, &dt);
    snprintf(buf, sizeof(buf), "%02u.%02u.%04u %02u:%02u", dt.day, dt.month, dt.year, dt.hour, dt.minute);
    canvas_draw_str(c, x, 53, buf);
    char ago[12];
    time_ago(e->ts, ago, sizeof(ago));
    snprintf(buf, sizeof(buf), "%s%s", ago, strcmp(ago, "now") ? " ago" : "");
    canvas_draw_str(c, x, 63, buf);
}
