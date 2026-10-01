/*
 * Pet simulation, the signal catalog ("Signal Dex"), badges and the save file.
 *
 * No hunger, no health: the pet grows with XP from new signals, games,
 * cleaning and sleeping. The old need fields stay in the save file only
 * so older saves keep loading.
 */
#include "pet.h"
#include <stddef.h>
#include <lib/subghz/subghz_protocol_registry.h>
#include <lib/subghz/types.h>
#include <lib/lfrfid/protocols/lfrfid_protocols.h>
#include <toolbox/protocols/protocol_dict.h>
#include <infrared.h>
#include <ibutton/ibutton_protocols.h>
#include <datetime/datetime.h>

#define SAVE_PATH APP_DATA_PATH("signal_pet.sav")

_Static_assert(offsetof(SaveData, games_day) == SAVE_V1_SIZE, "v1 save layout changed");
_Static_assert(offsetof(SaveData, sleep_ts) == SAVE_V2_SIZE, "v2 save layout changed");
#define STALE_SEC (20 * 60) /* the same signal is boring for 20 minutes */
#define MAX_LEVEL 99
#define TEEN_LEVEL 5
#define ADULT_LEVEL 10

/* ---------------------------------------------------------- tiers */

static const uint8_t tier_levels[TIER_MAX] = {3, 5, 8, 10, 15, 20, 30, 45, 70, 99};

const char* const move_names[TIER_MAX] = {
    "Radar Ping",
    "Dance",
    "Spin",
    "Juggle",
    "Moonwalk",
    "Backflip",
    "Power Up",
    "Levitate",
    "Teleport",
    "Legend Glow",
};

uint8_t pet_tier(uint8_t level) {
    uint8_t t = 0;
    while(t < TIER_MAX && level >= tier_levels[t])
        t++;
    return t;
}

uint8_t tier_level(uint8_t tier) {
    if(tier == 0) return 1;
    return tier_levels[(tier - 1) % TIER_MAX];
}

const char* const source_names[SrcCount] = {"Sub-GHz", "NFC", "RFID 125k", "Infrared", "iButton"};
const char* const source_short[SrcCount] = {"Sub-GHz", "NFC", "RFID", "IR", "iButton"};

/* Friendly names, same order as NfcProtocol */
const char* const nfc_names[12] = {
    "ISO14443-3A",
    "ISO14443-3B",
    "ISO14443-4A",
    "ISO14443-4B",
    "ISO15693",
    "FeliCa",
    "NTAG/Ultralight",
    "MIFARE Classic",
    "MIFARE Plus",
    "MIFARE DESFire",
    "NXP SLIX",
    "ST25TB",
};
static const uint8_t nfc_rarity[12] = {1, 2, 1, 2, 2, 3, 1, 1, 2, 2, 2, 3};

const char* const pet_names[] = {
    "Blip",  "Pixel", "Byte",  "Glitch", "Static", "Ping",  "Echo",  "Nibble",
    "Sparky", "Fizz", "Zap",   "Volt",   "Hertz",  "Chirp", "Buzz",  "Nano",
    "Qubit", "Ohm",   "Bleep", "Dotty",  "Morse",  "Tesla", "Radar", "Wavy",
    "Bitsy", "Fuzz",  "Sonar", "Kilo",   "Beacon", "Juno",  "Mochi", "Tofu",
};
const uint8_t pet_names_n = sizeof(pet_names) / sizeof(pet_names[0]);

const char* const badge_names[BADGE_COUNT] = {
    "Hatched",
    "First Bite",
    "Tuned In",
    "Tap Tap",
    "Close Call",
    "Couch Potato",
    "Key Master",
    "Omnivore",
    "Collector",
    "Archivist",
    "Grown Up",
    "Night Owl",
    "Gamer",
    "Golden Ear",
    "Clean Freak",
    "Best Friends",
};

const char* const badge_descs[BADGE_COUNT] = {
    "Hatch your egg",
    "Eat your first signal",
    "Catch a Sub-GHz signal",
    "Catch an NFC card",
    "Catch a 125 kHz tag",
    "Catch an IR remote",
    "Catch an iButton key",
    "Eat from all 5 sources",
    "Find 10 species",
    "Find 25 species",
    "Reach the adult form",
    "Hunt between 0 and 4 am",
    "Score 30 in Byte Catch",
    "Perfect Tune In run",
    "Clean up 10 times",
    "Pet your pet 50 times",
};

const Bmp* const badge_icons[BADGE_COUNT] = {
    &bmp_bd_egg,
    &bmp_bd_bite,
    &bmp_src_subghz,
    &bmp_src_nfc,
    &bmp_src_rfid,
    &bmp_src_ir,
    &bmp_src_ibutton,
    &bmp_bd_omni,
    &bmp_bd_book,
    &bmp_bd_books,
    &bmp_bd_crown,
    &bmp_bd_owl,
    &bmp_bd_pad,
    &bmp_bd_ear,
    &bmp_bd_broom,
    &bmp_bd_heart2,
};

/* ---------------------------------------------------------- hashing */

uint32_t hash_bytes(uint32_t h, const uint8_t* d, size_t n) {
    for(size_t i = 0; i < n; i++) {
        h ^= d[i];
        h *= 16777619u;
    }
    return h;
}

uint32_t hash_str(uint32_t h, const char* s) {
    return hash_bytes(h, (const uint8_t*)s, strlen(s));
}

uint32_t species_hash(uint8_t src, const char* proto) {
    uint32_t h = 2166136261u;
    h = hash_bytes(h, &src, 1);
    h = hash_str(h, proto);
    return h ? h : 1;
}

/* ---------------------------------------------------------- catalog */

static uint8_t subghz_rarity(const char* n) {
    static const char* const rare[] = {
        "KeeLoq",
        "Star Line",
        "Security+",
        "Somfy",
        "FloR",
        "Faac",
        "Alutech",
        "Atomo",
        "Twee",
        "KingGates",
        "Scher",
        "Kia",
        "Jarolift",
        "Phoenix",
        "Mastercode",
        "Dooya",
        "Nero Radio",
        "BETT",
    };
    static const char* const common[] = {
        "Princeton", "Holtek", "GateTX", "Linear", "Nice FLO", "Ansonic", "SMC5326", "Clemsa"};
    for(size_t i = 0; i < sizeof(rare) / sizeof(rare[0]); i++)
        if(strstr(n, rare[i])) return 3;
    for(size_t i = 0; i < sizeof(common) / sizeof(common[0]); i++)
        if(strstr(n, common[i])) return 1;
    if(strcmp(n, "CAME") == 0) return 1;
    return 2;
}

static uint8_t rfid_rarity(const char* n) {
    if(strcmp(n, "EM4100") == 0 || strcmp(n, "H10301") == 0 || strcmp(n, "Indala26") == 0) return 1;
    if(strstr(n, "FDX")) return 3; /* animal microchips */
    return 2;
}

static uint8_t ir_rarity(const char* n) {
    static const char* const common[] = {"NEC", "NECext", "Samsung32", "RC5", "RC6", "SIRC", "Raw IR"};
    for(size_t i = 0; i < sizeof(common) / sizeof(common[0]); i++)
        if(strcmp(n, common[i]) == 0) return 1;
    return 2;
}

/* Rank = how hard a signal is to find: the source matters (every home has
 * a TV remote, few people carry an iButton) and so does the protocol. */
static const uint8_t rank_table[SrcCount][3] = {
    {2, 3, 4}, /* Sub-GHz: Uncommon .. Epic */
    {1, 2, 3}, /* NFC: Common .. Rare */
    {2, 3, 4}, /* RFID: Uncommon .. Epic (animal chips) */
    {1, 2, 3}, /* IR: Common .. Rare */
    {3, 4, 5}, /* iButton: Rare .. Legendary */
};

static uint8_t rank_of(uint8_t src, uint8_t rarity) {
    if(rarity < 1) rarity = 1;
    if(rarity > 3) rarity = 3;
    return rank_table[src % SrcCount][rarity - 1];
}

const char* rank_name(uint8_t rank) {
    static const char* const names[6] = {"?", "Common", "Uncommon", "Rare", "Epic", "Legendary"};
    return names[rank > 5 ? 0 : rank];
}

/* XP per rank: a new species, a new signal of a known species, a re-catch */
static const uint8_t xp_species[6] = {0, 15, 30, 60, 100, 160};
static const uint8_t xp_signal[6] = {0, 4, 8, 15, 25, 40};
static const uint8_t xp_snack[6] = {0, 1, 1, 2, 2, 3};

static void cat_add(Catalog* cat, uint8_t src, const char* name, uint8_t rarity) {
    if(cat->n >= MAX_CATALOG || !name) return;
    CatEntry* e = &cat->e[cat->n++];
    e->name = name;
    e->src = src;
    e->rarity = rank_of(src, rarity);
    e->hash = species_hash(src, name);
    cat->count[src]++;
}

void catalog_build(Catalog* cat) {
    memset(cat, 0, sizeof(Catalog));

    cat->first[SrcSubGhz] = cat->n;
    size_t cnt = subghz_protocol_registry_count(&subghz_protocol_registry);
    for(size_t i = 0; i < cnt; i++) {
        const SubGhzProtocol* p = subghz_protocol_registry_get_by_index(&subghz_protocol_registry, i);
        if(!p || !p->name) continue;
        if(!(p->flag & SubGhzProtocolFlag_Decodable)) continue;
        if(strcmp(p->name, "RAW") == 0 || strcmp(p->name, "BinRAW") == 0) continue;
        cat_add(cat, SrcSubGhz, p->name, subghz_rarity(p->name));
    }

    cat->first[SrcNfc] = cat->n;
    for(uint8_t i = 0; i < 12; i++)
        cat_add(cat, SrcNfc, nfc_names[i], nfc_rarity[i]);

    cat->first[SrcRfid] = cat->n;
    ProtocolDict* dict = protocol_dict_alloc(lfrfid_protocols, LFRFIDProtocolMax);
    for(size_t i = 0; i < LFRFIDProtocolMax; i++) {
        const char* n = protocol_dict_get_name(dict, i);
        cat_add(cat, SrcRfid, n, rfid_rarity(n));
    }
    protocol_dict_free(dict);

    cat->first[SrcIr] = cat->n;
    for(int i = 0; i < InfraredProtocolMAX; i++) {
        const char* n = infrared_get_protocol_name((InfraredProtocol)i);
        cat_add(cat, SrcIr, n, ir_rarity(n));
    }
    cat_add(cat, SrcIr, "Raw IR", 1);

    cat->first[SrcIbutton] = cat->n;
    iButtonProtocols* ib = ibutton_protocols_alloc();
    uint32_t ibn = ibutton_protocols_get_protocol_count();
    for(uint32_t i = 0; i < ibn; i++) {
        const char* n = ibutton_protocols_get_name(ib, (iButtonProtocolId)i);
        cat_add(cat, SrcIbutton, n, strstr(n, "DS") || strstr(n, "Dallas") ? 2 : 3);
    }
    ibutton_protocols_free(ib);
}

int16_t catalog_find(Catalog* cat, uint32_t hash) {
    for(uint16_t i = 0; i < cat->n; i++)
        if(cat->e[i].hash == hash) return (int16_t)i;
    return -1;
}

SpeciesRec* species_get(SaveData* s, uint32_t hash) {
    for(uint16_t i = 0; i < s->species_n; i++)
        if(s->species[i].hash == hash) return &s->species[i];
    return NULL;
}

uint16_t species_found(App* app, int src) {
    uint16_t n = 0;
    for(uint16_t i = 0; i < app->save->species_n; i++)
        if(src < 0 || app->save->species[i].src == src) n++;
    return n;
}

uint16_t species_total(App* app, int src) {
    if(src < 0) return app->cat->n;
    return app->cat->count[src];
}

static SpecimenRec* specimen_get(SaveData* s, uint32_t hash) {
    for(uint16_t i = 0; i < s->specimens_n; i++)
        if(s->specimens[i].hash == hash) return &s->specimens[i];
    return NULL;
}

/* ---------------------------------------------------------- basics */

uint32_t state_now_ts(void) {
    return furi_hal_rtc_get_timestamp();
}

void state_add(int32_t* stat, int32_t milli) {
    int32_t v = *stat + milli;
    if(v < 0) v = 0;
    if(v > STAT_MAX) v = STAT_MAX;
    *stat = v;
}

uint8_t state_pct(int32_t v) {
    return (uint8_t)((v + 500) / 1000);
}

uint32_t state_xp_need(uint8_t level) {
    return 30 + (uint32_t)level * 20;
}

uint32_t state_age_days(App* app) {
    if(!app->save->born_ts) return 0;
    uint32_t now = state_now_ts();
    return now > app->save->born_ts ? (now - app->save->born_ts) / 86400 : 0;
}

const char* state_form_name(uint8_t stage, uint8_t form) {
    static const char* const adult[FormCount] = {"Wavern", "Tapkin", "Coilbit", "Irix", "Keybo", "Omnix"};
    switch(stage) {
    case StEgg:
        return "Egg";
    case StBaby:
        return "Bitling";
    case StTeen:
        return "Byteling";
    default:
        return adult[form % FormCount];
    }
}

uint8_t state_pick_form(App* app) {
    SaveData* s = app->save;
    uint32_t total = 0, best = 0;
    uint8_t best_src = 0;
    for(uint8_t i = 0; i < SrcCount; i++) {
        total += s->diet[i];
        if(s->diet[i] > best) {
            best = s->diet[i];
            best_src = i;
        }
    }
    if(total == 0 || best * 100 < total * 40) return FormOmnix;
    return best_src; /* Form order matches Source order */
}

/* Which need is the most urgent? 0 none, 1 food, 2 joy, 3 sleep, 4 noise */
/* What does the pet want? 0 nothing, 1 new signals, 4 a clean room */
uint8_t state_mood_need(App* app) {
    SaveData* s = app->save;
    if(s->stage == StEgg || s->asleep) return 0;
    if(s->noise >= 3) return 4;
    uint32_t now = state_now_ts();
    uint32_t since = s->last_catch_ts ? s->last_catch_ts : s->born_ts;
    if(since && now > since && now - since > 12 * 3600) return 1;
    return 0;
}

/* ---------------------------------------------------------- defaults / save */

void state_defaults(SaveData* s) {
    memset(s, 0, sizeof(SaveData));
    s->magic = SAVE_MAGIC;
    s->version = SAVE_VERSION;
    s->size = sizeof(SaveData);
    s->set.sound = 1;
    s->set.vibro = 1;
    s->set.led = 1;
    s->set.backlight = 1;
    s->set.pace = PaceNormal;
    s->set.away = 1;
    s->set.band = BandHop;
    s->stage = StEgg;
    s->food = 70000;
    s->joy = 80000;
    s->energy = 90000;
    strncpy(s->name, "Blip", NAME_LEN - 1);
}

void state_reset(App* app) {
    Settings keep = app->save->set;
    state_defaults(app->save);
    app->save->set = keep;
    app->save->last_ts = state_now_ts();
    app->pending_levels = 0;
    app->pending_evolve = false;
    app->new_badges = 0;
    state_save(app);
}

void state_load(App* app) {
    SaveData* s = app->save;
    bool ok = false;
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, SAVE_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        memset(s, 0, sizeof(SaveData));
        size_t n = storage_file_read(f, s, sizeof(SaveData));
        if(s->magic == SAVE_MAGIC && s->version == SAVE_VERSION)
            ok = n == sizeof(SaveData) && s->size == sizeof(SaveData);
        else if(s->magic == SAVE_MAGIC && s->version == 1)
            ok = n == SAVE_V1_SIZE && s->size == SAVE_V1_SIZE; /* upgrade */
        else if(s->magic == SAVE_MAGIC && s->version == 2)
            ok = n == SAVE_V2_SIZE && s->size == SAVE_V2_SIZE; /* upgrade */
        else if(s->magic == SAVE_MAGIC && s->version == 3)
            ok = n == sizeof(SaveData) && s->size == sizeof(SaveData); /* same layout */
        if(ok && s->version < 4) s->set.band = BandHop; /* v1.2: sweep all bands */
        if(ok) {
            s->version = SAVE_VERSION;
            s->size = sizeof(SaveData);
        }
    }
    storage_file_close(f);
    storage_file_free(f);
    if(!ok) {
        state_defaults(s);
        s->last_ts = state_now_ts();
    }
    s->name[NAME_LEN - 1] = '\0';
    if(s->species_n > MAX_SPECIES) s->species_n = MAX_SPECIES;
    if(s->specimens_n > MAX_SPECIMENS) s->specimens_n = MAX_SPECIMENS;
    if(s->set.band >= BandCount) s->set.band = BandEU433;
    if(s->set.pace > PaceIntense) s->set.pace = PaceNormal;
    if(s->log_n > LOG_N) s->log_n = LOG_N;
    s->log_next %= LOG_N;
}

void state_save(App* app) {
    storage_simply_mkdir(app->storage, APP_DATA_PATH(""));
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, SAVE_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(f, app->save, sizeof(SaveData));
    }
    storage_file_close(f);
    storage_file_free(f);
}

/* ---------------------------------------------------------- time */

/* No hunger, no health: time only lets a little static pile up. */
static void tick_minute_at(App* app, uint8_t hour, bool charging) {
    SaveData* s = app->save;
    UNUSED(hour);
    UNUSED(charging);
    if(s->stage == StEgg) return;
    if(furi_hal_random_get() % 600 == 0 && s->noise < 4) s->noise++;
}

void state_tick_minute(App* app) {
    tick_minute_at(app, app->hour, app->charging);
}

void state_catch_up(App* app) {
    SaveData* s = app->save;
    uint32_t now = state_now_ts();
    if(!s->last_ts || now <= s->last_ts || !s->set.away) {
        s->last_ts = now;
        return;
    }
    uint32_t mins = (now - s->last_ts) / 60;
    if(mins > 7 * 24 * 60) mins = 7 * 24 * 60;
    uint32_t t = now - mins * 60;
    for(uint32_t i = 0; i < mins; i++) {
        DateTime dt;
        datetime_timestamp_to_datetime(t + i * 60, &dt);
        tick_minute_at(app, dt.hour, false);
    }
    s->last_ts = now;
}

/* ---------------------------------------------------------- xp & levels */

static void check_evolve(App* app) {
    SaveData* s = app->save;
    if((s->stage == StBaby && s->level >= TEEN_LEVEL) || (s->stage == StTeen && s->level >= ADULT_LEVEL))
        app->pending_evolve = true;
}

void state_add_xp(App* app, uint32_t xp) {
    SaveData* s = app->save;
    if(s->stage == StEgg || s->level >= MAX_LEVEL) return;
    uint8_t tier = pet_tier(s->level);
    s->xp += xp;
    while(s->level < MAX_LEVEL && s->xp >= state_xp_need(s->level)) {
        s->xp -= state_xp_need(s->level);
        s->level++;
        app->pending_levels++;
    }
    if(s->level >= MAX_LEVEL) s->xp = 0;
    if(pet_tier(s->level) > tier) app->new_tier = pet_tier(s->level);
    check_evolve(app);
}

void state_check_evolve(App* app) {
    check_evolve(app);
}

/* Games can be replayed endlessly, so they only pay a little XP and at
 * most GAME_XP_DAY per day. Returns what was actually given. */
uint32_t state_game_xp(App* app, uint32_t want) {
    SaveData* s = app->save;
    uint32_t day = state_now_ts() / 86400;
    if(s->games_day != day) {
        s->games_day = day;
        s->games_xp = 0;
    }
    uint32_t left = s->games_xp < GAME_XP_DAY ? GAME_XP_DAY - s->games_xp : 0;
    uint32_t give = want < left ? want : left;
    s->games_xp += give;
    state_add_xp(app, give);
    return give;
}

/* ---------------------------------------------------------- badges */

static void badge_give(App* app, uint8_t i) {
    uint32_t bit = 1u << i;
    if(app->save->badges & bit) return;
    app->save->badges |= bit;
    app->new_badges |= bit;
}

void state_badges_check(App* app) {
    SaveData* s = app->save;
    if(s->stage != StEgg) badge_give(app, 0);
    if(s->catches > 0) badge_give(app, 1);
    uint8_t srcs = 0;
    for(uint8_t i = 0; i < SrcCount; i++) {
        if(s->diet[i]) {
            badge_give(app, 2 + i);
            srcs++;
        }
    }
    if(srcs == SrcCount) badge_give(app, 7);
    if(s->species_n >= 10) badge_give(app, 8);
    if(s->species_n >= 25) badge_give(app, 9);
    if(s->stage == StAdult) badge_give(app, 10);
    if(s->best_catch >= 30) badge_give(app, 12);
    if(s->cleans >= 10) badge_give(app, 14);
    if(s->pets >= 50) badge_give(app, 15);
}

/* ---------------------------------------------------------- eating */

/* Logbook */
const LogEntry* log_get(App* app, int16_t i) {
    SaveData* s = app->save;
    if(i < 0 || i >= s->log_n) return NULL;
    int16_t k = (int16_t)s->log_next - 1 - i;
    while(k < 0)
        k += LOG_N;
    return &s->log[k % LOG_N];
}

static void log_add(App* app, const CatchState* cs, uint32_t now) {
    SaveData* s = app->save;
    LogEntry* e = &s->log[s->log_next % LOG_N];
    memset(e, 0, sizeof(LogEntry));
    e->ts = now;
    e->src = cs->c.src;
    e->kind = cs->kind;
    e->rarity = cs->rarity;
    strncpy(e->proto, cs->c.proto, sizeof(e->proto) - 1);
    strncpy(e->detail, cs->c.detail, sizeof(e->detail) - 1);
    s->log_next = (s->log_next + 1) % LOG_N;
    if(s->log_n < LOG_N) s->log_n++;
}

void time_ago(uint32_t ts, char* out, size_t len) {
    uint32_t now = state_now_ts();
    uint32_t d = now > ts ? now - ts : 0;
    if(d < 60)
        snprintf(out, len, "now");
    else if(d < 3600)
        snprintf(out, len, "%lum", (unsigned long)(d / 60));
    else if(d < 86400)
        snprintf(out, len, "%luh", (unsigned long)(d / 3600));
    else
        snprintf(out, len, "%lud", (unsigned long)(d / 86400));
}

void catch_digest(App* app, const Catch* c, CatchState* out) {
    SaveData* s = app->save;
    uint32_t now = state_now_ts();
    uint32_t sh = species_hash(c->src, c->proto);
    uint32_t mh = c->id_hash ^ (sh * 2654435761u);
    if(!mh) mh = 1;

    int16_t ci = catalog_find(app->cat, sh);
    out->c = *c;
    out->rarity = ci >= 0 ? app->cat->e[ci].rarity : 2;
    if(out->rarity < 1 || out->rarity > 5) out->rarity = 2;
    out->dex_no = ci >= 0 ? (int16_t)(ci - app->cat->first[c->src] + 1) : 0;

    SpeciesRec* sp = species_get(s, sh);
    SpecimenRec* mr = specimen_get(s, mh);
    uint8_t r = out->rarity;

    out->g_food = 0;
    out->g_joy = 0;
    if(!sp) {
        out->kind = CatchNewSpecies;
        out->g_xp = xp_species[r];
    } else if(!mr) {
        out->kind = CatchNewSignal;
        out->g_xp = xp_signal[r];
    } else if(now - mr->last_ts < STALE_SEC) {
        out->kind = CatchStale;
        out->g_xp = 0;
    } else {
        out->kind = CatchSnack;
        out->g_xp = xp_snack[r];
    }
    /* a messy room spoils the appetite: half XP */
    out->messy = s->noise >= 3 && out->g_xp > 0;
    if(out->messy) out->g_xp = (out->g_xp + 1) / 2;

    log_add(app, out, now);
    if(out->kind == CatchStale) {
        state_save(app);
        return;
    }

    /* records */
    if(!sp && s->species_n < MAX_SPECIES) {
        sp = &s->species[s->species_n++];
        memset(sp, 0, sizeof(SpeciesRec));
        sp->hash = sh;
        sp->src = c->src;
        sp->first_ts = now;
    }
    if(sp) sp->count++;
    if(!mr) {
        if(s->specimens_n < MAX_SPECIMENS) {
            mr = &s->specimens[s->specimens_n++];
        } else {
            mr = &s->specimens[s->specimen_next % MAX_SPECIMENS];
            s->specimen_next = (s->specimen_next + 1) % MAX_SPECIMENS;
        }
        memset(mr, 0, sizeof(SpecimenRec));
        mr->hash = mh;
        mr->species = sh;
        mr->src = c->src;
    }
    mr->count++;
    mr->last_ts = now;

    s->diet[c->src]++;
    s->catches++;
    s->last_catch_ts = now;
    s->asleep = 0;
    if(furi_hal_random_get() % 100 < 30 && s->noise < 4) s->noise++;
    if(app->hour < 4) badge_give(app, 11);

    uint8_t lv = s->level;
    state_add_xp(app, out->g_xp);
    out->leveled = s->level != lv;
    state_badges_check(app);
    state_save(app);
}
