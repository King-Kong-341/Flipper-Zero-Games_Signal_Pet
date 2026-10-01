/*
 * Signal Pet - a virtual pet that lives inside your Flipper Zero and feeds
 * on the radio signals around you: Sub-GHz remotes, NFC cards, 125 kHz
 * tags, infrared remotes and iButton keys.
 *
 * Shared types and the interface between the modules:
 *   pet_main.c      app lifecycle, main loop, scene switching, transitions
 *   pet_state.c     simulation (needs, levels, evolution), catalog, save file
 *   pet_radio.c     the five receivers (threads -> pending catch)
 *   pet_gfx.c       drawing helpers + generated bitmaps (pet_gfx_data.c)
 *   pet_draw.c      the procedural pet renderer
 *   pet_fx.c        sound / vibration / LED sequencer, particles, toasts
 *   pet_home.c      the room: pet behaviour, dock, sleep, cleaning
 *   pet_hunt.c      source picker, scan screens, catch cinematic
 *   pet_play.c      the two mini games
 *   pet_menus.c     dex, stats, badges, settings, help, naming
 *   pet_intro.c     boot animation, hatching, level-up and evolution
 */
#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define SCREEN_W 128
#define SCREEN_H 64
#define FRAME_MS 33

/* ------------------------------------------------------------ sources */

typedef enum {
    SrcSubGhz,
    SrcNfc,
    SrcRfid,
    SrcIr,
    SrcIbutton,
    SrcCount,
} Source;

/* ------------------------------------------------------------ the pet */

typedef enum {
    StEgg,
    StBaby,
    StTeen,
    StAdult,
} Stage;

typedef enum {
    FormWavern, /* mostly Sub-GHz */
    FormTapkin, /* mostly NFC */
    FormCoilbit, /* mostly 125 kHz RFID */
    FormIrix, /* mostly infrared */
    FormKeybo, /* mostly iButton */
    FormOmnix, /* balanced diet */
    FormCount,
} Form;

typedef enum {
    EyeOpen,
    EyeBlink,
    EyeHappy,
    EyeSad,
    EyeSleepy,
    EyeSleep,
    EyeWide,
    EyeDizzy,
} EyeKind;

typedef enum {
    MouthSmile,
    MouthOpen,
    MouthBig,
    MouthFrown,
    MouthFlat,
    MouthO,
    MouthChomp,
    MouthTongue,
} MouthKind;

typedef struct {
    uint8_t stage;
    uint8_t form;
    uint8_t eyes;
    uint8_t mouth;
    bool blush;
    bool tear;
    int8_t look; /* -1 / 0 / 1 */
    int8_t squash; /* + wider, - taller */
    int8_t lift; /* pixels above the ground */
    uint8_t step; /* walking: 0, 1, 2 */
    int8_t shear; /* lean, pixels at the top */
    uint8_t glow; /* antenna waves 0..2 */
    uint8_t cracks; /* egg only */
    uint8_t tier; /* 0..10, grows with the level: extra effects */
    uint32_t t; /* ms clock for small details */
} Pose;

/* Level tiers: every tier unlocks a new move and/or a new look. */
#define TIER_MAX 10
uint8_t pet_tier(uint8_t level);
uint8_t tier_level(uint8_t tier); /* first level of a tier */
extern const char* const move_names[TIER_MAX];

/* ------------------------------------------------------------ save file */

#define SAVE_MAGIC 0x54455053u /* "SPET" */
#define SAVE_VERSION 4
#define SAVE_V2_SIZE 5088
#define SAVE_V1_SIZE 5076 /* v1 files load too; new fields start zeroed */
#define MAX_SPECIES 200
#define MAX_SPECIMENS 160
#define NAME_LEN 12
#define STAT_MAX 100000 /* needs are kept in milli-points */

typedef struct {
    uint32_t hash; /* species = source + protocol name */
    uint32_t first_ts;
    uint16_t count;
    uint8_t src;
    uint8_t pad;
} SpeciesRec;

typedef struct {
    uint32_t hash; /* individual signal = protocol + id */
    uint32_t species;
    uint32_t last_ts;
    uint16_t count;
    uint8_t src;
    uint8_t pad;
} SpecimenRec;

#define LOG_N 40

/* Logbook: the last LOG_N catches, newest written at log_next */
typedef struct {
    uint32_t ts;
    uint8_t src;
    uint8_t kind; /* CatchKind */
    uint8_t rarity;
    uint8_t pad;
    char proto[20];
    char detail[24];
} LogEntry;

typedef enum {
    PaceChill,
    PaceNormal,
    PaceIntense,
} Pace;

typedef enum {
    BandEU433,
    BandEU868,
    BandUS315,
    BandHop,
    BandCount,
} Band;

typedef struct {
    uint8_t sound;
    uint8_t vibro;
    uint8_t led;
    uint8_t backlight; /* 1 = always on while the app is open */
    uint8_t pace;
    uint8_t away; /* 1 = needs keep changing while the app is closed */
    uint8_t band;
    uint8_t pad;
} Settings;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    Settings set;
    char name[NAME_LEN];
    uint8_t stage;
    uint8_t form;
    uint8_t level;
    uint8_t noise; /* static left behind by digested signals (0..4) */
    uint8_t asleep;
    uint8_t named;
    uint8_t pad[2];
    uint32_t xp;
    int32_t food;
    int32_t joy;
    int32_t energy;
    uint32_t born_ts;
    uint32_t last_ts;
    uint32_t diet[SrcCount];
    uint32_t catches;
    uint32_t games;
    uint32_t pets;
    uint32_t cleans;
    uint32_t best_catch;
    uint32_t best_tune;
    uint32_t badges;
    uint16_t species_n;
    uint16_t specimens_n;
    uint32_t specimen_next;
    SpeciesRec species[MAX_SPECIES];
    SpecimenRec specimens[MAX_SPECIMENS];
    /* ---- v2 ---- */
    uint32_t unused_a; /* kept so older saves still load */
    uint32_t unused_b;
    uint8_t gadgets; /* bit 0: external CC1101 module seen */
    uint8_t pad2[3];
    /* ---- v3 ---- */
    uint32_t sleep_ts; /* when it fell asleep (dream bonus) */
    uint32_t last_catch_ts;
    uint16_t log_n;
    uint16_t log_next;
    LogEntry log[LOG_N];
} SaveData;

/* ------------------------------------------------------------ catalog */

#define MAX_CATALOG 200

typedef struct {
    const char* name;
    uint32_t hash;
    uint8_t src;
    uint8_t rarity; /* 1..3 */
} CatEntry;

typedef struct {
    CatEntry e[MAX_CATALOG];
    uint16_t n;
    uint16_t first[SrcCount]; /* index of the first entry per source */
    uint16_t count[SrcCount];
} Catalog;

/* ------------------------------------------------------------ radio */

typedef struct Radio Radio;

typedef enum {
    RadioIdle,
    RadioListening,
    RadioSensing, /* something is there, decoding... */
    RadioError,
} RadioStatus;

typedef struct {
    uint8_t src;
    char proto[24]; /* protocol name as in the catalog */
    char detail[40]; /* id / uid / key, for the result card */
    uint32_t id_hash; /* hash of the raw identity */
} Catch;

Radio* radio_alloc(FuriMessageQueue* wake_queue);
void radio_free(Radio* r);
bool radio_start(Radio* r, Source src, uint8_t band);
void radio_stop(Radio* r);
void radio_set_band(Radio* r, uint8_t band);
void radio_tick(Radio* r);
bool radio_take(Radio* r, Catch* out);
RadioStatus radio_status(Radio* r);
float radio_rssi(Radio* r);
uint32_t radio_freq(Radio* r);
uint8_t radio_sweep_pos(Radio* r);
void radio_resume(Radio* r);
bool radio_probe_external(void); /* is a CC1101 board on the GPIO? */
bool radio_external(Radio* r); /* Sub-GHz is using a CC1101 module on the GPIO */


/* ------------------------------------------------------------ fx */

#define FX_MAX_TONES 24
#define FX_MAX_VIB 8
#define FX_MAX_LED 12

typedef struct {
    uint16_t f; /* Hz, 0 = pause */
    uint16_t ms;
    uint8_t vol; /* 0..100 */
} Tone;

typedef struct {
    uint8_t r, g, b;
    uint16_t ms;
    bool fade;
} LedStep;

typedef struct {
    bool speaker;
    uint32_t speaker_retry;
    Tone tones[FX_MAX_TONES];
    uint8_t tone_n, tone_i;
    uint32_t tone_end;
    bool tone_busy;
    uint16_t vib[FX_MAX_VIB];
    uint8_t vib_n, vib_i;
    uint32_t vib_end;
    bool vib_busy;
    LedStep led[FX_MAX_LED];
    uint8_t led_n, led_i;
    uint32_t led_start;
    bool led_busy;
    bool led_loop;
} Fx;

typedef enum {
    PartHeart,
    PartNote,
    PartStar,
    PartZ,
    PartCrumb,
    PartSpark,
    PartShell,
    PartDust,
    PartBit,
} PartKind;

#define MAX_PARTS 32

typedef struct {
    float x, y, vx, vy;
    uint16_t life, age; /* ms */
    uint8_t kind;
    bool gravity;
    bool alive;
} Particle;

#define TOAST_LEN 32

typedef struct {
    char text[TOAST_LEN];
    uint8_t icon; /* 0 none, else badge index + 1 */
    uint32_t t0;
    uint16_t dur;
    bool active;
} Toast;

/* ------------------------------------------------------------ scenes */

typedef enum {
    SceneBoot,
    SceneHatch,
    SceneName,
    SceneHome,
    SceneHuntPick,
    SceneScan,
    SceneCatch,
    SceneCelebrate,
    ScenePlayPick,
    SceneGameCatch,
    SceneGameTune,
    SceneGameOver,
    SceneDex,
    SceneDexDetail,
    SceneStats,
    SceneSettings,
    SceneHelp,
    SceneConfirm,
    SceneLog,
    SceneLogDetail,
    SceneCount,
} Scene;

typedef enum {
    TransNone,
    TransBlinds,
    TransIris,
    TransStatic,
    TransSlide,
} TransKind;

typedef enum {
    ActIdle,
    ActWalk,
    ActHop,
    ActDance,
    ActPetted,
    ActEat,
    ActYawn,
    ActSad,
    ActRefuse,
    ActWake,
    ActCheer,
    ActBye,
    /* moves unlocked by level tiers */
    ActScanRings,
    ActSpin,
    ActJuggle,
    ActMoonwalk,
    ActBackflip,
    ActPowerUp,
    ActLevitate,
    ActTeleport,
} PetAct;

typedef struct {
    float x, target_x;
    float lift, lift_v;
    uint8_t act;
    uint32_t act_t0;
    uint32_t act_until;
    uint32_t next_blink;
    uint32_t blink_until;
    int8_t look;
    uint32_t look_until;
    uint32_t next_think;
    uint32_t bubble_t0;
    uint8_t bubble; /* thought bubble icon: 0 none, 1 food, 2 joy, 3 sleep, 4 noise */
    uint32_t say_t0;
    char say[28];
    uint32_t last_pet_gain;
} PetAnim;

typedef struct {
    uint8_t sel;
    float sel_x;
    uint32_t label_t0;
    uint32_t clean_t0; /* cleaning sweep running if != 0 */
    uint8_t clean_n;
    uint32_t sleep_fx_t0; /* lights going off / on */
    bool sleep_fx_to_dark;
    bool quitting;
    uint32_t quit_t0;
    uint32_t tip_at;
    uint32_t shock_t0; /* landing shockwave (tier 7+) */
    float shock_x;
} HomeState;

typedef struct {
    uint8_t sel; /* source in the picker */
    float sel_f;
    uint32_t scan_t0;
    bool ext_announced;
    bool ext_ready; /* picker: a Sub-GHz board is plugged in */
    float rssi_hist[40];
    uint8_t rssi_i;
    uint32_t rssi_t;
    float sweep;
    uint8_t blips[6][3]; /* x, y, life */
    bool err;
} HuntState;

typedef enum {
    CatchNewSpecies,
    CatchNewSignal,
    CatchSnack,
    CatchStale,
} CatchKind;

typedef struct {
    Catch c;
    uint8_t kind;
    uint8_t rarity;
    int16_t dex_no;
    int16_t g_food, g_joy, g_xp;
    uint32_t t0;
    bool card;
    bool leveled;
    bool messy; /* XP halved by static */
} CatchState;

typedef enum {
    CelebLevel,
    CelebEvolve,
    CelebHatch,
} CelebKind;

typedef struct {
    uint8_t kind;
    uint32_t t0;
    uint8_t old_stage, old_form;
    uint8_t level;
    uint8_t ret; /* scene to return to */
    bool done;
} CelebState;

typedef struct {
    uint32_t t0;
    uint8_t taps;
    uint32_t wobble_t0;
    uint32_t hatch_t0;
    bool hatched;
} IntroState;

#define NAME_MAX 10

typedef struct {
    uint8_t idx; /* suggestion index (dice key) */
    bool first; /* naming right after hatching */
    uint32_t t0;
    char text[NAME_MAX + 1];
    uint8_t len;
    uint8_t row, col; /* keyboard cursor */
    uint32_t key_t0; /* last key press (pop animation) */
    uint32_t shake_t0; /* empty name refused */
} NameState;

/* mini games */
#define GAME_ITEMS 8

typedef struct {
    float x, y, vy;
    uint8_t kind; /* 0 packet, 1 golden, 2 noise */
    bool alive;
} FallItem;

typedef struct {
    uint8_t game; /* 0 catch, 1 tune */
    uint8_t sel;
    uint32_t t0;
    /* packet catch */
    float px, pvx;
    int8_t dir;
    FallItem items[GAME_ITEMS];
    uint32_t next_spawn;
    uint16_t score;
    uint8_t lives;
    uint32_t hurt_t0;
    uint32_t over_t0;
    /* tune in */
    uint8_t round;
    uint8_t t_freq, t_amp; /* target: freq in half cycles, amp px */
    uint8_t p_freq, p_amp;
    uint32_t round_t0;
    uint32_t lock_t0;
    uint8_t locked;
    float phase;
    /* result */
    bool finished;
    uint16_t result;
    bool best;
    int16_t g_joy, g_xp;
} PlayState;

typedef struct {
    uint8_t tab;
    int16_t sel;
    int16_t top;
    float tab_f;
} DexState;

typedef struct {
    uint8_t page;
    uint8_t badge_sel;
    float page_f;
} StatsState;

typedef struct {
    int8_t sel;
    int8_t top;
    float knob[8];
} SettingsState;

typedef struct {
    uint8_t page;
} HelpState;

/* ------------------------------------------------------------ app */

typedef enum {
    EvInput,
    EvRadio,
} EvType;

typedef struct {
    EvType type;
    InputEvent input;
} AppEvent;

typedef struct App {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* queue;
    FuriMutex* mutex;
    NotificationApp* notif;
    Storage* storage;

    SaveData* save;
    Catalog* cat;
    Radio* radio;
    Fx fx;
    Particle parts[MAX_PARTS];
    Toast toast;

    uint32_t now;
    uint32_t frame;
    bool running;

    Scene scene;
    Scene next_scene;
    uint8_t trans;
    uint32_t trans_t0;
    bool trans_switched;

    uint32_t rtc_last;
    uint32_t sim_sec;
    uint32_t save_due;
    uint8_t hour;
    uint8_t minute;
    bool charging;

    uint8_t pending_levels;
    bool pending_evolve;
    uint32_t new_badges;

    PetAnim pet;
    HomeState home;
    HuntState hunt;
    CatchState cat_st;
    CelebState celeb;
    IntroState intro;
    NameState name_st;
    PlayState play;
    DexState dex;
    StatsState stats;
    SettingsState settings;
    HelpState help;
    uint8_t confirm_sel;
    int16_t log_sel;
    int16_t log_top;
    uint8_t new_tier; /* tier reached by the last level up, shown once */
} App;

/* ------------------------------------------------------------ pet_main.c */
void app_goto(App* app, Scene s, TransKind t);
uint32_t ease_ms(App* app, uint32_t t0);

/* ------------------------------------------------------------ pet_state.c */
void state_defaults(SaveData* s);
void state_load(App* app);
void state_save(App* app);
void state_catch_up(App* app);
void state_tick_minute(App* app);
void state_add_xp(App* app, uint32_t xp);
void state_check_evolve(App* app);
void state_add(int32_t* stat, int32_t milli);
uint32_t state_xp_need(uint8_t level);
uint8_t state_pct(int32_t v);
uint8_t state_mood_need(App* app);
void state_badges_check(App* app);
const char* state_form_name(uint8_t stage, uint8_t form);
uint8_t state_pick_form(App* app);
void state_reset(App* app);
uint32_t state_age_days(App* app);
uint32_t state_now_ts(void);

void catalog_build(Catalog* cat);
int16_t catalog_find(Catalog* cat, uint32_t hash);
uint32_t hash_str(uint32_t h, const char* s);
uint32_t hash_bytes(uint32_t h, const uint8_t* d, size_t n);
uint32_t species_hash(uint8_t src, const char* proto);
SpeciesRec* species_get(SaveData* s, uint32_t hash);
uint16_t species_found(App* app, int src); /* src = -1 for all */
uint16_t species_total(App* app, int src);
void catch_digest(App* app, const Catch* c, CatchState* out);

#define BADGE_COUNT 16
extern const char* const badge_names[BADGE_COUNT];
extern const char* const badge_descs[BADGE_COUNT];
extern const char* const source_names[SrcCount];
extern const char* const source_short[SrcCount];
extern const char* const pet_names[];
extern const uint8_t pet_names_n;

/* ------------------------------------------------------------ pet_gfx.c */
typedef struct {
    uint8_t w, h;
    const uint8_t* data;
} Bmp;

#include "pet_gfx_data.h"

int32_t gfx_isqrt(int32_t n);
int16_t gfx_sin(int32_t a); /* a in 1/64 turns, result * 64 */
int16_t gfx_cos(int32_t a);
int16_t gfx_ellipse_half(int16_t rx, int16_t ry, int16_t dy);
void gfx_bmp(Canvas* c, int32_t x, int32_t y, const Bmp* b);
void gfx_bmp_color(Canvas* c, int32_t x, int32_t y, const Bmp* b, Color col);
void gfx_hline(Canvas* c, int32_t x, int32_t y, int32_t w);
void gfx_vline(Canvas* c, int32_t x, int32_t y, int32_t h);
void gfx_dither(Canvas* c, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t phase);
void gfx_sparse(Canvas* c, int32_t x, int32_t y, int32_t w, int32_t h);
void gfx_invert(Canvas* c, int32_t x, int32_t y, int32_t w, int32_t h);
void gfx_str_center(Canvas* c, int32_t cx, int32_t baseline, const char* s);
void gfx_str_right(Canvas* c, int32_t rx, int32_t baseline, const char* s);
void gfx_bar(Canvas* c, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t pct);
void gfx_stars(Canvas* c, int32_t x, int32_t y, uint8_t rarity);
void gfx_button_hint(Canvas* c, int32_t x, int32_t baseline, const Bmp* icon, const char* label);
void gfx_scrollbar(Canvas* c, int32_t x, int32_t y, int32_t h, int32_t pos, int32_t total, int32_t vis);
void gfx_title(Canvas* c, const char* title);
void gfx_dots(Canvas* c, int32_t cx, int32_t y, uint8_t n, uint8_t sel);
void gfx_burst(Canvas* c, int32_t cx, int32_t cy, int32_t r0, int32_t r1, int32_t rot, uint8_t rays);
void gfx_fit_str(Canvas* c, char* buf, size_t len, int32_t max_w);
float ease_out(float t);
float ease_in_out(float t);
float ease_back(float t);
const Bmp* source_icon(uint8_t src);
const Bmp* source_big(uint8_t src);

/* ------------------------------------------------------------ pet_draw.c */
void pose_default(Pose* p, App* app);
void draw_pet(Canvas* c, int32_t cx, int32_t gy, const Pose* p);
void draw_pet_sized(Canvas* c, int32_t cx, int32_t gy, const Pose* p, uint8_t stage);
int32_t pet_height(uint8_t stage);
int32_t pet_mouth_y(const Pose* p, int32_t gy);

/* ------------------------------------------------------------ pet_fx.c */
void fx_init(App* app);
void fx_deinit(App* app);
void fx_update(App* app);
void fx_stop_all(App* app);
void fx_click(App* app);
void fx_back(App* app);
void fx_boot(App* app);
void fx_catch(App* app, uint8_t src);
void fx_new_species(App* app);
void fx_chomp(App* app);
void fx_stale(App* app);
void fx_levelup(App* app);
void fx_evolve(App* app);
void fx_hatch(App* app);
void fx_crack(App* app);
void fx_purr(App* app);
void fx_badge(App* app);
void fx_clean(App* app);
void fx_sleep(App* app, bool to_sleep);
void fx_pickup(App* app, bool golden);
void fx_hurt(App* app);
void fx_gameover(App* app);
void fx_lock(App* app);
void fx_refuse(App* app);
void fx_scan_led(App* app, uint8_t src, bool on);
void fx_sense(App* app);
void fx_bye(App* app);

void parts_clear(App* app);
void parts_spawn(App* app, PartKind k, float x, float y, float vx, float vy, uint16_t life, bool gravity);
void parts_burst(App* app, PartKind k, float x, float y, uint8_t n, float speed, bool gravity);
void parts_update(App* app, uint32_t dt);
void parts_draw(App* app, Canvas* c);

void toast_show(App* app, const char* text, uint8_t icon, uint16_t ms);
void toast_draw(App* app, Canvas* c);

/* ------------------------------------------------------------ scenes */
void home_enter(App* app);
void home_input(App* app, InputEvent* ev);
void home_update(App* app, uint32_t dt);
void home_draw(App* app, Canvas* c);
void home_scene_backdrop(App* app, Canvas* c, bool with_ground);
void pet_anim_reset(App* app);
void pet_say(App* app, const char* text);
void pet_make_pose(App* app, Pose* p);

void huntpick_enter(App* app);
void huntpick_input(App* app, InputEvent* ev);
void huntpick_update(App* app, uint32_t dt);
void huntpick_draw(App* app, Canvas* c);
void scan_enter(App* app);
void scan_exit(App* app);
void scan_input(App* app, InputEvent* ev);
void scan_update(App* app, uint32_t dt);
void scan_draw(App* app, Canvas* c);
void scan_radio_event(App* app);
void catch_input(App* app, InputEvent* ev);
void catch_update(App* app, uint32_t dt);
void catch_draw(App* app, Canvas* c);

void boot_enter(App* app);
void boot_input(App* app, InputEvent* ev);
void boot_update(App* app, uint32_t dt);
void boot_draw(App* app, Canvas* c);
void hatch_enter(App* app);
void hatch_input(App* app, InputEvent* ev);
void hatch_update(App* app, uint32_t dt);
void hatch_draw(App* app, Canvas* c);
void celeb_start(App* app, Scene ret);
bool celeb_pending(App* app);
void celeb_input(App* app, InputEvent* ev);
void celeb_update(App* app, uint32_t dt);
void celeb_draw(App* app, Canvas* c);

void playpick_enter(App* app);
void playpick_input(App* app, InputEvent* ev);
void playpick_draw(App* app, Canvas* c);
void gcatch_enter(App* app);
void gcatch_input(App* app, InputEvent* ev);
void gcatch_update(App* app, uint32_t dt);
void gcatch_draw(App* app, Canvas* c);
void gtune_enter(App* app);
void gtune_input(App* app, InputEvent* ev);
void gtune_update(App* app, uint32_t dt);
void gtune_draw(App* app, Canvas* c);
void gover_input(App* app, InputEvent* ev);
void gover_draw(App* app, Canvas* c);

void name_enter(App* app, bool first);
void name_input(App* app, InputEvent* ev);
void name_draw(App* app, Canvas* c);
void dex_enter(App* app);
void dex_input(App* app, InputEvent* ev);
void dex_update(App* app, uint32_t dt);
void dex_draw(App* app, Canvas* c);
void dexd_input(App* app, InputEvent* ev);
void dexd_draw(App* app, Canvas* c);
void stats_enter(App* app);
void stats_input(App* app, InputEvent* ev);
void stats_update(App* app, uint32_t dt);
void stats_draw(App* app, Canvas* c);
void settings_enter(App* app);
void settings_input(App* app, InputEvent* ev);
void settings_update(App* app, uint32_t dt);
void settings_draw(App* app, Canvas* c);
void help_enter(App* app);
void help_input(App* app, InputEvent* ev);
void help_draw(App* app, Canvas* c);
void confirm_input(App* app, InputEvent* ev);
void confirm_draw(App* app, Canvas* c);
void log_enter(App* app);
void log_input(App* app, InputEvent* ev);
void log_draw(App* app, Canvas* c);
void logd_input(App* app, InputEvent* ev);
void logd_draw(App* app, Canvas* c);
const LogEntry* log_get(App* app, int16_t i); /* 0 = newest */
void time_ago(uint32_t ts, char* out, size_t len);
