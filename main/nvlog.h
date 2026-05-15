#ifndef NVLOG_H
#define NVLOG_H

// =============================================================================
//  nvlog.h  –  Non-volatile circular log (200 entries × 32 bytes = 6400 bytes)
//
//  Stored as a single NVS blob so it survives reboots and power cycles.
//  Each entry carries a boot-count so sessions are distinguishable in the dump.
//
//  Code byte: high nibble = module, low nibble = event index.
//  This lets log entries carry maximum information in minimum flash space:
//  the string "WIF.DOWN reason=5" costs ~20 bytes vs a full ESP_LOGE string.
//
//  Flush strategy
//  ──────────────
//  - WARN / ERROR:  flush if last flush was > NVLOG_FLUSH_COOLDOWN_S ago
//  - INFO:          accumulate; flush every NVLOG_FLUSH_EVERY writes
//  - Explicit:      nvlog_flush() always writes regardless of cooldown
//
//  Callers outside this module must NOT hold a scheduler lock when calling
//  nvlog_write() — the internal flush calls NVS which uses FreeRTOS APIs.
// =============================================================================

#include <stdint.h>
#include <stdbool.h>

// ─── Log codes ───────────────────────────────────────────────────────────────
// Encoded as uint8 to minimise flash string overhead.
typedef enum {
    // System  0x0_
    NVC_SYS_BOOT     = 0x00,  // u32=heap_free
    NVC_SYS_HEAP_LO  = 0x01,  // u32=heap_free, u16=threshold_kb

    // Camera  0x1_
    NVC_CAM_INIT_OK  = 0x10,  // u16=attempt
    NVC_CAM_INIT_ERR = 0x11,  // u16=attempt, u32=esp_err
    NVC_CAM_REINIT   = 0x12,  // u16=null_streak, u32=heap
    NVC_CAM_NULL     = 0x13,  // u16=streak, u32=heap
    NVC_CAM_DROP     = 0x14,  // u32=total_cam_drops
    NVC_CAM_ENC_ERR  = 0x15,  // u16=pixfmt, u32=heap

    // Stream  0x2_
    NVC_STR_START    = 0x20,  // u32=heap
    NVC_STR_END      = 0x21,  // u16=frames_k (frames/1000), u32=dur_s
    NVC_STR_TIMEOUT  = 0x22,  // u16=frame_idx
    NVC_STR_ERR      = 0x23,  // u16=(uint16)esp_err, u32=frame_idx
    NVC_STR_DROP     = 0x24,  // u16=drops_this_window, u32=total_drops

    // WiFi    0x3_
    NVC_WIF_SCAN     = 0x30,  // u16=ap_count, u32=best_rssi_neg (cast int8→uint32)
    NVC_WIF_CONN     = 0x31,  // s=ssid
    NVC_WIF_UP       = 0x32,  // s=ip, u32=reconnect_num
    NVC_WIF_DOWN     = 0x33,  // u16=reason, u32=uptime_s
    NVC_WIF_SWITCH   = 0x34,  // s=new_ssid, u16=drop_window, u32=rssi_neg
    NVC_WIF_RSSI     = 0x35,  // u16=rssi_neg (cast), s=ssid
    NVC_WIF_CYCLE    = 0x36,  // u16=cycle_num
    NVC_WIF_DEGRADE  = 0x37,  // u16=streak, u32=rssi_neg

    // HTTP    0x4_
    NVC_HTTP_START   = 0x40,  // (no payload)
    NVC_HTTP_ERR     = 0x41,  // u16=err_code
} nvlog_code_t;

// ─── Level ───────────────────────────────────────────────────────────────────
#define NVL_I  0u
#define NVL_W  1u
#define NVL_E  2u

// ─── Convenience macros ───────────────────────────────────────────────────────
#define NVLOGI(code, u16, u32, s)  nvlog_write((uint8_t)(code), NVL_I, (uint16_t)(u16), (uint32_t)(u32), (s))
#define NVLOGW(code, u16, u32, s)  nvlog_write((uint8_t)(code), NVL_W, (uint16_t)(u16), (uint32_t)(u32), (s))
#define NVLOGE(code, u16, u32, s)  nvlog_write((uint8_t)(code), NVL_E, (uint16_t)(u16), (uint32_t)(u32), (s))

// ─── Public API ───────────────────────────────────────────────────────────────

/**
 * Load stored log from NVS and increment the boot counter.
 * Must be called after nvs_flash_init() and before any task is created.
 */
void nvlog_init(void);

/**
 * Print every stored entry to serial in chronological order.
 * Call immediately after nvlog_init(), before spawning tasks.
 */
void nvlog_dump_serial(void);

/**
 * Append one entry to the in-RAM ring buffer and conditionally flush to NVS.
 * Safe to call from any task context; must NOT be called from ISR or within
 * a portENTER_CRITICAL section.
 */
void nvlog_write(uint8_t code, uint8_t lvl, uint16_t u16, uint32_t u32,
                 const char *s);

/**
 * Force an immediate NVS write regardless of cooldown.
 * Call before deliberately rebooting or entering deep sleep.
 */
void nvlog_flush(void);

#endif /* NVLOG_H */
