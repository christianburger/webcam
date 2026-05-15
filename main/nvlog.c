// =============================================================================
//  nvlog.c  –  Non-volatile circular log
//
//  Entry layout (32 bytes, packed):
//    ts_s  uint32  – esp_timer uptime in seconds at write time
//    u32   uint32  – large numeric payload (heap, IP, frame count …)
//    boot  uint16  – boot counter (incremented in nvlog_init)
//    u16   uint16  – small numeric payload (RSSI, reason, streak …)
//    code  uint8   – nvlog_code_t
//    lvl   uint8   – NVL_I / NVL_W / NVL_E
//    s     char[16]– short string (SSID, IP, description)
//               ────
//               32 bytes total  →  200 × 32 = 6 400 bytes NVS blob
//
//  NVS namespace  "nvlog"
//    key "buf"    – 6400-byte blob (ring buffer content)
//    key "head"   – uint16 (next-write index, 0-199)
//    key "cnt"    – uint16 (entries written, capped at NVL_CAP)
//    key "boot"   – uint16 (boot counter, incremented on init)
// =============================================================================

#include "nvlog.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

#define NVL_CAP          200u
#define NVL_NS           "nvlog"
#define NVL_KEY_BUF      "buf"
#define NVL_KEY_HEAD     "head"
#define NVL_KEY_CNT      "cnt"
#define NVL_KEY_BOOT     "boot"
#define NVL_FLUSH_EVERY  15u                     // flush every N INFO writes
#define NVL_COOLDOWN_US  (10LL * 1000000LL)     // min gap between auto-flushes

// ─── Entry struct ─────────────────────────────────────────────────────────────

typedef struct {
    uint32_t ts_s;
    uint32_t u32;
    uint16_t boot;
    uint16_t u16;
    uint8_t  code;
    uint8_t  lvl;
    char     s[18];
} __attribute__((packed)) nvlog_entry_t;

_Static_assert(sizeof(nvlog_entry_t) == 32, "nvlog_entry_t must be 32 bytes");

// ─── State ────────────────────────────────────────────────────────────────────

static nvlog_entry_t s_ring[NVL_CAP];
static uint16_t      s_head       = 0;    // next-write slot
static uint16_t      s_cnt        = 0;    // total written, capped at NVL_CAP
static uint16_t      s_boot       = 0;    // current boot number
static uint8_t       s_since_flush = 0;
static int64_t       s_last_flush_us = 0;

// Simple mutex — nvlog_write may be called from multiple tasks simultaneously.
// We use a statically allocated mutex so it is valid before the scheduler starts.
static StaticSemaphore_t  s_mutex_buf;
static SemaphoreHandle_t  s_mutex = NULL;

static inline void lock(void)   { if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY); }
static inline void unlock(void) { if (s_mutex) xSemaphoreGive(s_mutex); }

// ─── Code-name table (stored in flash, zero RAM cost) ─────────────────────────

static const char *code_name(uint8_t c) {
    switch (c) {
        case 0x00: return "SYS.BOOT";
        case 0x01: return "SYS.HEAP";
        case 0x10: return "CAM.INIT+";
        case 0x11: return "CAM.INIT!";
        case 0x12: return "CAM.REINIT";
        case 0x13: return "CAM.NULL";
        case 0x14: return "CAM.DROP";
        case 0x15: return "CAM.ENC!";
        case 0x20: return "STR.START";
        case 0x21: return "STR.END";
        case 0x22: return "STR.TMO";
        case 0x23: return "STR.ERR";
        case 0x24: return "STR.DROP";
        case 0x30: return "WIF.SCAN";
        case 0x31: return "WIF.CONN";
        case 0x32: return "WIF.UP";
        case 0x33: return "WIF.DOWN";
        case 0x34: return "WIF.SWITCH";
        case 0x35: return "WIF.RSSI";
        case 0x36: return "WIF.CYCLE";
        case 0x37: return "WIF.DEGRAD";
        case 0x40: return "HTTP.START";
        case 0x41: return "HTTP.ERR";
        default:   return "???";
    }
}

// ─── Internal flush (caller must hold s_mutex) ────────────────────────────────
// Copies ring to a stack snapshot under lock, then releases lock before the
// slow NVS write.  The snapshot may be 1-2 entries behind if a concurrent
// write races the memcpy — acceptable for a diagnostic log.

static void flush_locked(void) {
    // Snapshot metadata under the lock (already held by caller)
    static nvlog_entry_t snap[NVL_CAP];   // static: kept off task stack
    memcpy(snap, s_ring, sizeof(s_ring));
    uint16_t head = s_head;
    uint16_t cnt  = s_cnt;
    s_since_flush = 0;

    // Release lock before the slow NVS operations
    unlock();

    nvs_handle_t h;
    if (nvs_open(NVL_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, NVL_KEY_BUF,  snap, sizeof(snap));
        nvs_set_u16 (h, NVL_KEY_HEAD, head);
        nvs_set_u16 (h, NVL_KEY_CNT,  cnt);
        nvs_commit(h);
        nvs_close(h);
        s_last_flush_us = esp_timer_get_time();
    }

    lock();   // re-acquire for caller
}

// ─── Public API ───────────────────────────────────────────────────────────────

void nvlog_init(void) {
    s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_buf);

    memset(s_ring, 0, sizeof(s_ring));

    nvs_handle_t h;
    if (nvs_open(NVL_NS, NVS_READWRITE, &h) == ESP_OK) {
        // Load stored ring
        size_t sz = sizeof(s_ring);
        nvs_get_blob(h, NVL_KEY_BUF,  s_ring, &sz);
        nvs_get_u16 (h, NVL_KEY_HEAD, &s_head);
        nvs_get_u16 (h, NVL_KEY_CNT,  &s_cnt);
        nvs_get_u16 (h, NVL_KEY_BOOT, &s_boot);

        // Increment and persist boot counter
        s_boot++;
        nvs_set_u16(h, NVL_KEY_BOOT, s_boot);
        nvs_commit(h);
        nvs_close(h);
    }

    ESP_LOGI("nvlog", "init boot=%u head=%u cnt=%u", s_boot, s_head, s_cnt);
}

void nvlog_dump_serial(void) {
    lock();
    uint16_t cnt  = (s_cnt < NVL_CAP) ? s_cnt : NVL_CAP;
    uint16_t head = s_head;
    unlock();

    printf("\n===== NVLOG DUMP  boot=%u  entries=%u =====\n", s_boot, cnt);
    if (cnt == 0) { printf("(empty)\n"); }

    // Oldest entry = (head - cnt + CAP) % CAP
    uint16_t start = (uint16_t)((head + NVL_CAP - cnt) % NVL_CAP);
    for (uint16_t i = 0; i < cnt; i++) {
        uint16_t idx = (uint16_t)((start + i) % NVL_CAP);
        lock();
        nvlog_entry_t e = s_ring[idx];  // copy under lock
        unlock();
        char lc = (e.lvl == NVL_E) ? 'E' : (e.lvl == NVL_W) ? 'W' : 'I';
        printf("[NVL] B%03u T+%lus %c %s  u16=%u u32=%lu s=%s\n",
               e.boot, (unsigned long)e.ts_s, lc, code_name(e.code),
               e.u16, (unsigned long)e.u32, e.s);
    }
    printf("===== END NVLOG =====\n\n");
    fflush(stdout);
}

void nvlog_write(uint8_t code, uint8_t lvl, uint16_t u16, uint32_t u32,
                 const char *s) {
    uint32_t ts = (uint32_t)(esp_timer_get_time() / 1000000ULL);

    lock();

    nvlog_entry_t *e = &s_ring[s_head];
    e->ts_s = ts;
    e->u32  = u32;
    e->boot = s_boot;
    e->u16  = u16;
    e->code = code;
    e->lvl  = lvl;
    if (s) { strncpy(e->s, s, sizeof(e->s) - 1); e->s[sizeof(e->s)-1] = '\0'; }
    else   { e->s[0] = '\0'; }

    s_head = (uint16_t)((s_head + 1u) % NVL_CAP);
    if (s_cnt < NVL_CAP) s_cnt++;
    s_since_flush++;

    bool do_flush = false;
    if (lvl >= NVL_W) {
        // Flush on warn/error with cooldown to prevent flash hammering
        int64_t now = esp_timer_get_time();
        do_flush = (now - s_last_flush_us) >= NVL_COOLDOWN_US;
    } else {
        do_flush = (s_since_flush >= NVL_FLUSH_EVERY);
    }

    if (do_flush) flush_locked();  // releases and re-acquires lock internally

    unlock();
}

void nvlog_flush(void) {
    lock();
    flush_locked();   // releases and re-acquires internally
    unlock();
}
