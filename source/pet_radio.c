/*
 * The five receivers. Only one runs at a time.
 *
 *   Sub-GHz  CC1101 async RX + the firmware's protocol decoders
 *            (same setup as the official Weather Station app)
 *   NFC      NfcScanner finds the card type, then a poller of the base
 *            protocol reads the UID
 *   RFID     LFRFID worker in auto read mode
 *   IR       infrared worker, decoded or raw signals
 *   iButton  iButton worker read mode
 *
 * Worker threads only fill `pending` (under the radio mutex) and wake the
 * main loop with an EvRadio event; everything else happens on the main
 * thread in radio_tick() / radio_take().
 */
#include "pet.h"
#include <lib/subghz/receiver.h>
#include <lib/subghz/subghz_worker.h>
#include <lib/subghz/subghz_protocol_registry.h>
#include <lib/subghz/protocols/base.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <nfc/nfc.h>
#include <nfc/nfc_scanner.h>
#include <nfc/nfc_poller.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a_poller.h>
#include <nfc/protocols/iso14443_3b/iso14443_3b.h>
#include <nfc/protocols/iso14443_3b/iso14443_3b_poller.h>
#include <nfc/protocols/iso15693_3/iso15693_3.h>
#include <nfc/protocols/iso15693_3/iso15693_3_poller.h>
#include <nfc/protocols/felica/felica.h>
#include <nfc/protocols/felica/felica_poller.h>
#include <lib/lfrfid/lfrfid_worker.h>
#include <lib/lfrfid/protocols/lfrfid_protocols.h>
#include <toolbox/protocols/protocol_dict.h>
#include <infrared_worker.h>
#include <ibutton/ibutton_worker.h>
#include <ibutton/ibutton_key.h>
#include <ibutton/ibutton_protocols.h>

extern const char* const nfc_names[12];

static const uint32_t band_freq[3] = {433920000, 868350000, 315000000};

/* Band "All": every frequency of the firmware's default Sub-GHz list, which
 * covers all three CC1101 bands (300-348, 387-464, 779-928 MHz). Receiving
 * is fine everywhere - the region lock only applies to transmitting. */
static const uint32_t sweep_freq[] = {
    300000000, 302757000, 303875000, 304250000, 307000000, 307500000, 307800000,
    309000000, 310000000, 312000000, 312100000, 312200000, 313000000, 313850000,
    314000000, 314350000, 314980000, 315000000, 318000000, 330000000, 345000000,
    348000000, 387000000, 390000000, 418000000, 430000000, 430500000, 431000000,
    431500000, 433075000, 433220000, 433420000, 433657070, 433889000, 433920000,
    434075000, 434177000, 434190000, 434390000, 434420000, 434620000, 434775000,
    438900000, 440175000, 464000000, 779000000, 868350000, 868400000, 868800000,
    868950000, 906400000, 915000000, 925000000, 928000000,
};
#define SWEEP_MAX (sizeof(sweep_freq) / sizeof(sweep_freq[0]))
/* The CC1101 has one receiver, so it can't hear every channel at the very
 * same moment. Instead it sniffs the signal strength of all channels in
 * a fast loop (like the Frequency Analyzer: ~2 ms per channel, the whole
 * list in about 0.2 s) and jumps onto any channel that lights up to
 * decode it there. */
#define SWEEP_SETTLE_US 1800 /* RSSI needs a moment after retuning */
#define SWEEP_HOLD_MS 2500 /* max time decoding one channel */
#define SWEEP_QUIET_MS 450 /* leave a channel this long after it went quiet */
#define SWEEP_RSSI_BUSY -70.0f

/* external CC1101 boards on the GPIO header (e.g. Rabbit-Labs Flux Capacitor);
 * driver plugin ships with the firmware, same name as in the Sub-GHz app */
#define CC1101_EXT_NAME "cc1101_ext"


typedef enum {
    NfcPhaseScan,
    NfcPhaseUid,
    NfcPhaseDone,
} NfcPhase;

struct Radio {
    FuriMessageQueue* wake;
    FuriMutex* mx;
    Source src;
    bool running;
    volatile bool sensing;
    bool error;

    /* pending catch, filled by worker threads */
    bool has_pending;
    Catch pending;
    bool paused; /* waiting for radio_resume() after a catch */

    /* Sub-GHz */
    SubGhzEnvironment* env;
    SubGhzReceiver* receiver;
    SubGhzWorker* worker;
    const SubGhzDevice* dev;
    FuriString* tmp;
    uint32_t freq;
    uint8_t band;
    uint8_t hop_i;
    uint32_t hop_t;
    uint32_t sweep[SWEEP_MAX]; /* the frequencies this radio accepts */
    uint8_t sweep_n;
    volatile bool hold; /* locked on a busy channel, decoder running */
    uint32_t hold_t;
    uint32_t strong_t; /* last time the locked channel was loud */
    FuriThread* sweep_thread;
    volatile bool sweep_run;
    volatile bool decoded; /* a decoder recognised the locked signal */
    float peak;
    uint32_t lock_base;
    float rssi;
    bool ext; /* using the external module */
    bool otg_ours; /* we switched on 5V for it */

    /* NFC */
    Nfc* nfc;
    NfcScanner* scanner;
    NfcPoller* poller;
    volatile NfcPhase phase;
    volatile bool nfc_detected;
    NfcProtocol nfc_leaf;
    NfcProtocol nfc_base;
    uint8_t uid[10];
    volatile uint8_t uid_len;
    volatile bool uid_done;
    uint32_t uid_t0;

    /* RFID */
    ProtocolDict* rfid_dict;
    LFRFIDWorker* rfid;
    volatile bool rfid_done;
    volatile ProtocolId rfid_proto;

    /* IR */
    InfraredWorker* ir;
    uint32_t ir_last_t;

    /* iButton */
    iButtonProtocols* ib_protocols;
    iButtonKey* ib_key;
    iButtonWorker* ib;
    volatile bool ib_done;
};

static void wake(Radio* r) {
    AppEvent ev = {.type = EvRadio};
    furi_message_queue_put(r->wake, &ev, 0);
}

static void hex_into(char* out, size_t len, const uint8_t* d, size_t n) {
    size_t p = 0;
    out[0] = '\0';
    for(size_t i = 0; i < n && p + 3 < len; i++) {
        p += snprintf(out + p, len - p, "%02X", d[i]);
        if(i + 1 < n && p + 2 < len) out[p++] = ' ', out[p] = '\0';
    }
}

static void set_pending(Radio* r, const Catch* c) {
    furi_mutex_acquire(r->mx, FuriWaitForever);
    if(!r->has_pending && !r->paused) {
        r->pending = *c;
        r->has_pending = true;
        r->paused = true;
    }
    furi_mutex_release(r->mx);
    wake(r);
}

/* ================================================================ Sub-GHz */

static void subghz_cb(SubGhzReceiver* receiver, SubGhzProtocolDecoderBase* decoder_base, void* context) {
    Radio* r = context;
    furi_mutex_acquire(r->mx, FuriWaitForever);
    bool busy = r->has_pending || r->paused;
    furi_mutex_release(r->mx);
    if(!busy && decoder_base && decoder_base->protocol) {
        Catch c;
        memset(&c, 0, sizeof(c));
        c.src = SrcSubGhz;
        strncpy(c.proto, decoder_base->protocol->name, sizeof(c.proto) - 1);
        furi_string_reset(r->tmp);
        subghz_protocol_decoder_base_get_string(decoder_base, r->tmp);
        const char* s = furi_string_get_cstr(r->tmp);
        /* rolling codes change every press: identify those by serial */
        const char* sn = strstr(s, "Sn:");
        const char* key = strstr(s, "Key:");
        uint32_t h = 2166136261u;
        if(sn) {
            size_t n = 0;
            while(sn[n] && sn[n] != '\r' && sn[n] != '\n' && sn[n] != ' ') n++;
            h = hash_bytes(h, (const uint8_t*)sn, n);
            snprintf(c.detail, sizeof(c.detail), "%.*s", (int)(n > 30 ? 30 : n), sn);
        } else {
            h = hash_str(h, s);
            const char* d = key ? key + 4 : s;
            size_t n = 0;
            while(d[n] && d[n] != '\r' && d[n] != '\n' && n < sizeof(c.detail) - 1) n++;
            snprintf(c.detail, sizeof(c.detail), "%.*s", (int)n, d);
        }
        c.id_hash = h;
        c.freq = r->freq;
        r->decoded = true;
        set_pending(r, &c);
    }
    subghz_receiver_reset(receiver);
}

static void subghz_rx_start(Radio* r, uint32_t freq) {
    subghz_devices_idle(r->dev);
    r->freq = subghz_devices_set_frequency(r->dev, freq);
    subghz_devices_flush_rx(r->dev);
    subghz_devices_set_rx(r->dev);
    subghz_devices_start_async_rx(r->dev, subghz_worker_rx_callback, r->worker);
    subghz_worker_start(r->worker);
    r->hop_t = furi_get_tick();
}

static void subghz_rx_stop(Radio* r) {
    if(subghz_worker_is_running(r->worker)) {
        subghz_worker_stop(r->worker);
        subghz_devices_stop_async_rx(r->dev);
    }
    subghz_devices_idle(r->dev);
}

/* one fast signal-strength sniff (decoder must be stopped) */
static float sweep_probe(Radio* r, uint32_t freq) {
    subghz_devices_idle(r->dev);
    subghz_devices_set_frequency(r->dev, freq);
    subghz_devices_set_rx(r->dev);
    furi_delay_us(SWEEP_SETTLE_US);
    return subghz_devices_get_rssi(r->dev);
}

static const char* mystery_name(uint32_t freq) {
    if(freq < 360000000) return "Mystery 315";
    if(freq < 600000000) return "Mystery 433";
    return "Mystery 868";
}

/* Something was on air but no decoder knew it: it still counts. */
static void mystery_catch(Radio* r, uint32_t freq, float peak) {
    Catch c;
    memset(&c, 0, sizeof(c));
    c.src = SrcSubGhz;
    strncpy(c.proto, mystery_name(freq), sizeof(c.proto) - 1);
    uint32_t bucket = freq / 50000; /* same transmitter = same 50 kHz slot */
    c.id_hash = hash_bytes(hash_str(2166136261u, c.proto), (const uint8_t*)&bucket, sizeof(bucket));
    c.freq = freq;
    snprintf(
        c.detail,
        sizeof(c.detail),
        "%lu.%02lu MHz %d dBm",
        (unsigned long)(freq / 1000000),
        (unsigned long)(freq % 1000000) / 10000,
        (int)peak);
    set_pending(r, &c);
}

/* Like the Frequency Analyzer: sniff every channel's signal strength in a
 * tight loop (~0.1 s for the whole list), on a hit search the exact
 * frequency in 25 kHz steps around it, then listen there with the
 * decoders. Runs in its own thread so the screen stays smooth. */
static int32_t sweep_thread(void* ctx) {
    Radio* r = ctx;
    uint32_t muted_f = 0, muted_until = 0; /* a channel that never shuts up */
    while(r->sweep_run) {
        if(r->paused && !r->hold) {
            furi_delay_ms(20);
            continue;
        }
        uint32_t now = furi_get_tick();
        if(!r->hold) {
            /* coarse pass over all channels */
            float best = -130.0f;
            uint32_t best_f = 0;
            for(uint8_t i = 0; i < r->sweep_n && r->sweep_run && !r->paused; i++) {
                r->hop_i = i;
                uint32_t f = r->sweep[i];
                r->freq = f;
                if(f == muted_f && (int32_t)(furi_get_tick() - muted_until) < 0) continue;
                float rssi = sweep_probe(r, f);
                if(rssi > best) {
                    best = rssi;
                    best_f = f;
                }
            }
            r->rssi = best;
            if(!r->sweep_run || r->paused || best < SWEEP_RSSI_BUSY || !best_f) continue;
            /* fine pass around the peak */
            uint32_t fine_f = best_f;
            for(int32_t d = -200000; d <= 200000; d += 25000) {
                uint32_t f = (uint32_t)((int32_t)best_f + d);
                if(!subghz_devices_is_frequency_valid(r->dev, f)) continue;
                float rssi = sweep_probe(r, f);
                if(rssi > best) {
                    best = rssi;
                    fine_f = f;
                }
            }
            /* lock on and let the decoders listen */
            subghz_rx_start(r, fine_f);
            r->freq = fine_f;
            r->lock_base = best_f;
            r->hold = true;
            r->hold_t = furi_get_tick();
            r->strong_t = r->hold_t;
            r->peak = best;
            r->decoded = false;
            continue;
        }
        /* locked */
        furi_delay_ms(25);
        now = furi_get_tick();
        float rssi = subghz_devices_get_rssi(r->dev);
        r->rssi = rssi;
        if(rssi > SWEEP_RSSI_BUSY - 6.0f) r->strong_t = now;
        if(rssi > r->peak) r->peak = rssi;
        if(r->paused) continue; /* a catch is on screen */
        bool quiet = now - r->strong_t > SWEEP_QUIET_MS;
        bool timeout = now - r->hold_t > SWEEP_HOLD_MS;
        if(quiet || timeout) {
            subghz_rx_stop(r);
            r->hold = false;
            uint32_t heard = r->strong_t - r->hold_t;
            if(timeout && !quiet) {
                /* still loud after the whole hold: a constant carrier or
                 * interference, not a remote - ignore it for a minute */
                muted_f = r->lock_base;
                muted_until = now + 60000;
            } else if(!r->decoded && heard >= 120) {
                mystery_catch(r, r->freq, r->peak);
            }
        }
    }
    return 0;
}

static void sweep_begin(Radio* r) {
    if(r->sweep_thread || r->band != BandHop || r->sweep_n < 2) return;
    r->hold = false;
    r->sweep_run = true;
    r->sweep_thread = furi_thread_alloc_ex("SignalPetSweep", 2048, sweep_thread, r);
    furi_thread_start(r->sweep_thread);
}

static void sweep_end(Radio* r) {
    if(!r->sweep_thread) return;
    r->sweep_run = false;
    furi_thread_join(r->sweep_thread);
    furi_thread_free(r->sweep_thread);
    r->sweep_thread = NULL;
    if(r->hold) subghz_rx_stop(r);
    r->hold = false;
}

static bool subghz_start(Radio* r) {
    r->tmp = furi_string_alloc();
    r->env = subghz_environment_alloc();
    subghz_environment_set_came_atomo_rainbow_table_file_name(r->env, EXT_PATH("subghz/assets/came_atomo"));
    subghz_environment_set_alutech_at_4n_rainbow_table_file_name(
        r->env, EXT_PATH("subghz/assets/alutech_at_4n"));
    subghz_environment_set_nice_flor_s_rainbow_table_file_name(r->env, EXT_PATH("subghz/assets/nice_flor_s"));
    subghz_environment_set_protocol_registry(r->env, (void*)&subghz_protocol_registry);
    r->receiver = subghz_receiver_alloc_init(r->env);
    subghz_receiver_set_filter(r->receiver, SubGhzProtocolFlag_Decodable);
    subghz_receiver_set_rx_callback(r->receiver, subghz_cb, r);

    r->worker = subghz_worker_alloc();
    subghz_worker_set_overrun_callback(r->worker, (SubGhzWorkerOverrunCallback)subghz_receiver_reset);
    subghz_worker_set_pair_callback(r->worker, (SubGhzWorkerPairCallback)subghz_receiver_decode);
    subghz_worker_set_context(r->worker, r->receiver);

    subghz_devices_init();

    /* external module? (same detection as the official Weather Station) */
    r->ext = false;
    r->otg_ours = false;
    const SubGhzDevice* ext = subghz_devices_get_by_name(CC1101_EXT_NAME);
    if(ext) {
        if(!furi_hal_power_is_otg_enabled()) {
            for(uint8_t i = 0; i < 5 && !furi_hal_power_is_otg_enabled(); i++) {
                furi_hal_power_enable_otg();
                furi_delay_ms(10);
            }
            r->otg_ours = furi_hal_power_is_otg_enabled();
        }
        if(subghz_devices_is_connect(ext)) {
            r->ext = true;
            r->dev = ext;
            subghz_devices_begin(ext);
        } else if(r->otg_ours) {
            furi_hal_power_disable_otg();
            r->otg_ours = false;
        }
    }
    if(!r->ext) r->dev = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    if(!r->dev) return false;
    subghz_devices_reset(r->dev);
    subghz_devices_idle(r->dev);
    subghz_devices_load_preset(r->dev, FuriHalSubGhzPresetOok650Async, NULL);
    r->sweep_n = 0;
    for(size_t i = 0; i < SWEEP_MAX; i++)
        if(subghz_devices_is_frequency_valid(r->dev, sweep_freq[i])) r->sweep[r->sweep_n++] = sweep_freq[i];
    r->hop_i = 0;
    r->hold = false;
    if(r->band == BandHop && r->sweep_n > 1) {
        r->freq = r->sweep[0];
        sweep_begin(r);
    } else {
        subghz_rx_start(r, band_freq[r->band % 3]);
    }
    return true;
}

static void subghz_stop(Radio* r) {
    sweep_end(r);
    if(r->dev) {
        subghz_rx_stop(r);
        subghz_devices_sleep(r->dev);
        if(r->ext) subghz_devices_end(r->dev);
        r->dev = NULL;
    }
    if(r->otg_ours) furi_hal_power_disable_otg();
    r->otg_ours = false;
    r->ext = false;
    subghz_devices_deinit();
    if(r->worker) subghz_worker_free(r->worker);
    if(r->receiver) subghz_receiver_free(r->receiver);
    if(r->env) subghz_environment_free(r->env);
    if(r->tmp) furi_string_free(r->tmp);
    r->worker = NULL;
    r->receiver = NULL;
    r->env = NULL;
    r->tmp = NULL;
}

/* ================================================================ NFC */

static const NfcProtocol nfc_priority[] = {
    NfcProtocolMfDesfire,
    NfcProtocolMfPlus,
    NfcProtocolMfClassic,
    NfcProtocolMfUltralight,
    NfcProtocolSlix,
    NfcProtocolSt25tb,
    NfcProtocolFelica,
    NfcProtocolIso14443_4a,
    NfcProtocolIso14443_4b,
    NfcProtocolIso15693_3,
    NfcProtocolIso14443_3a,
    NfcProtocolIso14443_3b,
};

static NfcProtocol nfc_base_of(NfcProtocol p) {
    switch(p) {
    case NfcProtocolIso14443_3a:
    case NfcProtocolIso14443_4a:
    case NfcProtocolMfUltralight:
    case NfcProtocolMfClassic:
    case NfcProtocolMfPlus:
    case NfcProtocolMfDesfire:
        return NfcProtocolIso14443_3a;
    case NfcProtocolIso14443_3b:
    case NfcProtocolIso14443_4b:
        return NfcProtocolIso14443_3b;
    case NfcProtocolIso15693_3:
    case NfcProtocolSlix:
        return NfcProtocolIso15693_3;
    case NfcProtocolFelica:
        return NfcProtocolFelica;
    default:
        return NfcProtocolInvalid;
    }
}

static void nfc_scan_cb(NfcScannerEvent event, void* context) {
    Radio* r = context;
    if(event.type != NfcScannerEventTypeDetected || r->nfc_detected) return;
    NfcProtocol leaf = NfcProtocolInvalid;
    for(size_t k = 0; k < sizeof(nfc_priority) / sizeof(nfc_priority[0]) && leaf == NfcProtocolInvalid; k++) {
        for(size_t i = 0; i < event.data.protocol_num; i++) {
            if(event.data.protocols[i] == nfc_priority[k]) {
                leaf = nfc_priority[k];
                break;
            }
        }
    }
    if(leaf == NfcProtocolInvalid && event.data.protocol_num) leaf = event.data.protocols[0];
    r->nfc_leaf = leaf;
    r->nfc_detected = true;
    wake(r);
}

static void uid_store(Radio* r, const uint8_t* uid, size_t len) {
    if(!uid) len = 0;
    if(len > sizeof(r->uid)) len = sizeof(r->uid);
    if(len) memcpy(r->uid, uid, len);
    r->uid_len = (uint8_t)len;
    r->uid_done = true;
}

static NfcCommand nfc_poll_cb(NfcGenericEvent event, void* context) {
    Radio* r = context;
    if(r->uid_done) return NfcCommandStop;
    size_t len = 0;
    switch(event.protocol) {
    case NfcProtocolIso14443_3a: {
        const Iso14443_3aPollerEvent* e = event.event_data;
        if(e->type == Iso14443_3aPollerEventTypeReady) {
            const Iso14443_3aData* d = (const Iso14443_3aData*)nfc_poller_get_data(r->poller);
            uid_store(r, iso14443_3a_get_uid(d, &len), len);
        }
        break;
    }
    case NfcProtocolIso14443_3b: {
        const Iso14443_3bPollerEvent* e = event.event_data;
        if(e->type == Iso14443_3bPollerEventTypeReady) {
            const Iso14443_3bData* d = (const Iso14443_3bData*)nfc_poller_get_data(r->poller);
            uid_store(r, iso14443_3b_get_uid(d, &len), len);
        }
        break;
    }
    case NfcProtocolIso15693_3: {
        const Iso15693_3PollerEvent* e = event.event_data;
        if(e->type == Iso15693_3PollerEventTypeReady) {
            const Iso15693_3Data* d = (const Iso15693_3Data*)nfc_poller_get_data(r->poller);
            uid_store(r, iso15693_3_get_uid(d, &len), len);
        }
        break;
    }
    case NfcProtocolFelica: {
        const FelicaPollerEvent* e = event.event_data;
        if(e->type == FelicaPollerEventTypeReady || e->type == FelicaPollerEventTypeIncomplete) {
            const FelicaData* d = (const FelicaData*)nfc_poller_get_data(r->poller);
            uid_store(r, felica_get_uid(d, &len), len);
        }
        break;
    }
    default:
        break;
    }
    if(r->uid_done) {
        wake(r);
        return NfcCommandStop;
    }
    return NfcCommandContinue;
}

static void rnfc_begin_scan(Radio* r) {
    r->nfc_detected = false;
    r->uid_done = false;
    r->uid_len = 0;
    r->phase = NfcPhaseScan;
    r->sensing = false;
    nfc_scanner_start(r->scanner, nfc_scan_cb, r);
}

static bool rnfc_start(Radio* r) {
    r->nfc = nfc_alloc();
    r->scanner = nfc_scanner_alloc(r->nfc);
    rnfc_begin_scan(r);
    return true;
}

static void rnfc_poller_end(Radio* r) {
    if(r->poller) {
        nfc_poller_stop(r->poller);
        nfc_poller_free(r->poller);
        r->poller = NULL;
    }
}

static void rnfc_finish(Radio* r) {
    rnfc_poller_end(r);
    Catch c;
    memset(&c, 0, sizeof(c));
    c.src = SrcNfc;
    uint8_t leaf = r->nfc_leaf < NfcProtocolNum ? r->nfc_leaf : NfcProtocolIso14443_3a;
    strncpy(c.proto, nfc_names[leaf], sizeof(c.proto) - 1);
    uint32_t h = hash_bytes(2166136261u, &leaf, 1);
    if(r->uid_len) {
        h = hash_bytes(h, r->uid, r->uid_len);
        char hex[32];
        hex_into(hex, sizeof(hex), r->uid, r->uid_len);
        snprintf(c.detail, sizeof(c.detail), "UID %s", hex);
    } else {
        snprintf(c.detail, sizeof(c.detail), "UID unknown");
    }
    c.id_hash = h;
    r->phase = NfcPhaseDone;
    r->sensing = false;
    set_pending(r, &c);
}

static void rnfc_tick(Radio* r) {
    if(r->phase == NfcPhaseScan && r->nfc_detected) {
        nfc_scanner_stop(r->scanner);
        r->sensing = true;
        r->nfc_base = nfc_base_of(r->nfc_leaf);
        if(r->nfc_base == NfcProtocolInvalid) {
            rnfc_finish(r);
            return;
        }
        r->phase = NfcPhaseUid;
        r->uid_t0 = furi_get_tick();
        r->poller = nfc_poller_alloc(r->nfc, r->nfc_base);
        nfc_poller_start(r->poller, nfc_poll_cb, r);
    } else if(r->phase == NfcPhaseUid) {
        if(r->uid_done || furi_get_tick() - r->uid_t0 > 1500) rnfc_finish(r);
    }
}

static void rnfc_stop(Radio* r) {
    if(r->phase == NfcPhaseScan) nfc_scanner_stop(r->scanner);
    rnfc_poller_end(r);
    if(r->scanner) nfc_scanner_free(r->scanner);
    if(r->nfc) nfc_free(r->nfc);
    r->scanner = NULL;
    r->nfc = NULL;
}

/* ================================================================ RFID */

static void rfid_cb(LFRFIDWorkerReadResult result, ProtocolId protocol, void* context) {
    Radio* r = context;
    if(result == LFRFIDWorkerReadSenseCardStart) {
        r->sensing = true;
        wake(r);
    } else if(result == LFRFIDWorkerReadSenseCardEnd) {
        r->sensing = false;
    } else if(result == LFRFIDWorkerReadDone) {
        r->rfid_proto = protocol;
        r->rfid_done = true;
        wake(r);
    }
}

static bool rfid_start(Radio* r) {
    r->rfid_dict = protocol_dict_alloc(lfrfid_protocols, LFRFIDProtocolMax);
    r->rfid = lfrfid_worker_alloc(r->rfid_dict);
    lfrfid_worker_start_thread(r->rfid);
    r->rfid_done = false;
    lfrfid_worker_read_start(r->rfid, LFRFIDWorkerReadTypeAuto, rfid_cb, r);
    return true;
}

static void rfid_tick(Radio* r) {
    if(!r->rfid_done) return;
    r->rfid_done = false;
    lfrfid_worker_stop(r->rfid);
    r->sensing = false;
    ProtocolId p = r->rfid_proto;
    Catch c;
    memset(&c, 0, sizeof(c));
    c.src = SrcRfid;
    strncpy(c.proto, protocol_dict_get_name(r->rfid_dict, p), sizeof(c.proto) - 1);
    uint8_t buf[16];
    size_t n = protocol_dict_get_data_size(r->rfid_dict, p);
    if(n > sizeof(buf)) n = sizeof(buf);
    protocol_dict_get_data(r->rfid_dict, p, buf, n);
    c.id_hash = hash_bytes(hash_str(2166136261u, c.proto), buf, n);
    char hex[32];
    hex_into(hex, sizeof(hex), buf, n > 8 ? 8 : n);
    snprintf(c.detail, sizeof(c.detail), "%s", hex);
    set_pending(r, &c);
}

static void rfid_stop(Radio* r) {
    if(r->rfid) {
        lfrfid_worker_stop(r->rfid);
        lfrfid_worker_stop_thread(r->rfid);
        lfrfid_worker_free(r->rfid);
    }
    if(r->rfid_dict) protocol_dict_free(r->rfid_dict);
    r->rfid = NULL;
    r->rfid_dict = NULL;
}

/* ================================================================ IR */

static void ir_cb(void* context, InfraredWorkerSignal* sig) {
    Radio* r = context;
    Catch c;
    memset(&c, 0, sizeof(c));
    c.src = SrcIr;
    if(infrared_worker_signal_is_decoded(sig)) {
        const InfraredMessage* m = infrared_worker_get_decoded_signal(sig);
        if(!m || m->repeat) return;
        strncpy(c.proto, infrared_get_protocol_name(m->protocol), sizeof(c.proto) - 1);
        /* one remote = protocol + address; the button is just detail */
        uint32_t h = hash_str(2166136261u, c.proto);
        h = hash_bytes(h, (const uint8_t*)&m->address, sizeof(m->address));
        c.id_hash = h;
        snprintf(
            c.detail,
            sizeof(c.detail),
            "Addr %02lX  Cmd %02lX",
            (unsigned long)m->address,
            (unsigned long)m->command);
    } else {
        const uint32_t* t;
        size_t n;
        infrared_worker_get_raw_signal(sig, &t, &n);
        if(n < 12) return; /* stray light */
        strncpy(c.proto, "Raw IR", sizeof(c.proto) - 1);
        uint32_t h = hash_str(2166136261u, c.proto);
        uint32_t nn = (uint32_t)(n / 4);
        h = hash_bytes(h, (const uint8_t*)&nn, sizeof(nn));
        for(size_t i = 0; i < n && i < 8; i++) {
            uint16_t q = (uint16_t)(t[i] / 300);
            h = hash_bytes(h, (const uint8_t*)&q, sizeof(q));
        }
        c.id_hash = h;
        snprintf(c.detail, sizeof(c.detail), "RAW %u", (unsigned)n);
    }
    set_pending(r, &c);
}

static bool ir_start(Radio* r) {
    r->ir = infrared_worker_alloc();
    infrared_worker_rx_enable_signal_decoding(r->ir, true);
    infrared_worker_rx_enable_blink_on_receiving(r->ir, false);
    infrared_worker_rx_set_received_signal_callback(r->ir, ir_cb, r);
    infrared_worker_rx_start(r->ir);
    return true;
}

static void ir_stop(Radio* r) {
    if(r->ir) {
        infrared_worker_rx_stop(r->ir);
        infrared_worker_free(r->ir);
    }
    r->ir = NULL;
}

/* ================================================================ iButton */

static void ib_cb(void* context) {
    Radio* r = context;
    r->ib_done = true;
    wake(r);
}

static bool ib_start(Radio* r) {
    r->ib_protocols = ibutton_protocols_alloc();
    r->ib_key = ibutton_key_alloc(ibutton_protocols_get_max_data_size(r->ib_protocols));
    r->ib = ibutton_worker_alloc(r->ib_protocols);
    ibutton_worker_start_thread(r->ib);
    ibutton_worker_read_set_callback(r->ib, ib_cb, r);
    r->ib_done = false;
    ibutton_worker_read_start(r->ib, r->ib_key);
    return true;
}

static void ib_tick(Radio* r) {
    if(!r->ib_done) return;
    r->ib_done = false;
    ibutton_worker_stop(r->ib);
    if(!ibutton_protocols_is_valid(r->ib_protocols, r->ib_key)) {
        ibutton_worker_read_start(r->ib, r->ib_key);
        return;
    }
    Catch c;
    memset(&c, 0, sizeof(c));
    c.src = SrcIbutton;
    iButtonProtocolId id = ibutton_key_get_protocol_id(r->ib_key);
    strncpy(c.proto, ibutton_protocols_get_name(r->ib_protocols, id), sizeof(c.proto) - 1);
    FuriString* s = furi_string_alloc();
    ibutton_protocols_render_uid(r->ib_protocols, r->ib_key, s);
    const char* u = furi_string_get_cstr(s);
    c.id_hash = hash_str(hash_str(2166136261u, c.proto), u);
    size_t n = 0;
    while(u[n] && u[n] != '\n' && u[n] != '\r' && n < sizeof(c.detail) - 1) n++;
    snprintf(c.detail, sizeof(c.detail), "%.*s", (int)n, u);
    furi_string_free(s);
    set_pending(r, &c);
}

static void ib_stop(Radio* r) {
    if(r->ib) {
        ibutton_worker_stop(r->ib);
        ibutton_worker_stop_thread(r->ib);
        ibutton_worker_free(r->ib);
    }
    if(r->ib_key) ibutton_key_free(r->ib_key);
    if(r->ib_protocols) ibutton_protocols_free(r->ib_protocols);
    r->ib = NULL;
    r->ib_key = NULL;
    r->ib_protocols = NULL;
}

/* ================================================================ public */

Radio* radio_alloc(FuriMessageQueue* wake_queue) {
    Radio* r = malloc(sizeof(Radio));
    memset(r, 0, sizeof(Radio));
    r->wake = wake_queue;
    r->mx = furi_mutex_alloc(FuriMutexTypeNormal);
    r->rssi = -100.0f;
    return r;
}

void radio_free(Radio* r) {
    radio_stop(r);
    furi_mutex_free(r->mx);
    free(r);
}

bool radio_start(Radio* r, Source src, uint8_t band) {
    radio_stop(r);
    r->src = src;
    r->band = band;
    r->has_pending = false;
    r->paused = false;
    r->sensing = false;
    r->error = false;
    bool ok = false;
    switch(src) {
    case SrcSubGhz:
        ok = subghz_start(r);
        break;
    case SrcNfc:
        ok = rnfc_start(r);
        break;
    case SrcRfid:
        ok = rfid_start(r);
        break;
    case SrcIr:
        ok = ir_start(r);
        break;
    default:
        ok = ib_start(r);
        break;
    }
    r->running = true;
    r->error = !ok;
    return ok;
}

void radio_stop(Radio* r) {
    if(!r->running) return;
    switch(r->src) {
    case SrcSubGhz:
        subghz_stop(r);
        break;
    case SrcNfc:
        rnfc_stop(r);
        break;
    case SrcRfid:
        rfid_stop(r);
        break;
    case SrcIr:
        ir_stop(r);
        break;
    default:
        ib_stop(r);
        break;
    }
    r->running = false;
    r->sensing = false;
}

void radio_set_band(Radio* r, uint8_t band) {
    r->band = band;
    if(!r->running || r->src != SrcSubGhz || !r->dev) return;
    sweep_end(r);
    subghz_rx_stop(r);
    r->hop_i = 0;
    r->hold = false;
    if(band == BandHop && r->sweep_n > 1)
        sweep_begin(r);
    else
        subghz_rx_start(r, band_freq[band % 3]);
}

void radio_tick(Radio* r) {
    if(!r->running || r->error) return;
    switch(r->src) {
    case SrcSubGhz:
        if(!r->dev) break;
        /* the sweep thread keeps rssi/freq up to date in band "All" */
        if(!r->sweep_thread) r->rssi = subghz_devices_get_rssi(r->dev);
        break;
    case SrcNfc:
        rnfc_tick(r);
        break;
    case SrcRfid:
        rfid_tick(r);
        break;
    case SrcIbutton:
        ib_tick(r);
        break;
    default:
        break;
    }
}

bool radio_take(Radio* r, Catch* out) {
    bool got = false;
    furi_mutex_acquire(r->mx, FuriWaitForever);
    if(r->has_pending) {
        *out = r->pending;
        r->has_pending = false;
        got = true;
    }
    furi_mutex_release(r->mx);
    return got;
}

/* Listen again after a catch was shown. */
void radio_resume(Radio* r) {
    if(!r->running) return;
    furi_mutex_acquire(r->mx, FuriWaitForever);
    r->has_pending = false;
    r->paused = false;
    furi_mutex_release(r->mx);
    switch(r->src) {
    case SrcNfc:
        if(r->phase == NfcPhaseDone) rnfc_begin_scan(r);
        break;
    case SrcRfid:
        r->rfid_done = false;
        lfrfid_worker_read_start(r->rfid, LFRFIDWorkerReadTypeAuto, rfid_cb, r);
        break;
    case SrcIbutton:
        r->ib_done = false;
        ibutton_worker_read_start(r->ib, r->ib_key);
        break;
    default:
        break;
    }
}

RadioStatus radio_status(Radio* r) {
    if(!r->running) return RadioIdle;
    if(r->error) return RadioError;
    if(r->sensing) return RadioSensing;
    return RadioListening;
}

/* Quick look for a CC1101 board on the GPIO (radio must be idle). */
bool radio_probe_external(void) {
    subghz_devices_init();
    bool found = false;
    const SubGhzDevice* ext = subghz_devices_get_by_name(CC1101_EXT_NAME);
    if(ext) {
        bool otg = furi_hal_power_is_otg_enabled();
        if(!otg) {
            furi_hal_power_enable_otg();
            furi_delay_ms(15);
        }
        found = subghz_devices_is_connect(ext);
        if(!otg) furi_hal_power_disable_otg();
    }
    subghz_devices_deinit();
    return found;
}

bool radio_external(Radio* r) {
    return r->running && r->src == SrcSubGhz && r->ext;
}

float radio_rssi(Radio* r) {
    return r->rssi;
}

/* sweep position 0..255 for the scan screen */
uint8_t radio_sweep_pos(Radio* r) {
    if(!r->running || r->band != BandHop || r->sweep_n < 2) return 0;
    return (uint8_t)(r->hop_i * 255 / (r->sweep_n - 1));
}

uint32_t radio_freq(Radio* r) {
    return r->freq;
}
