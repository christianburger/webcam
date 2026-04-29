#ifndef CAM_LOG_H
#define CAM_LOG_H

// =============================================================================
//  cam_log.h  –  Structured logging tags for the ESP32 camera project
//
//  Tags follow the pattern  module|sub  and are plain C string literals so
//  the linker deduplicates them and ESP_LOG_LEVEL() filtering works:
//
//    idf.py monitor | grep " cam|"     ← all camera events
//    idf.py monitor | grep "cam|init"  ← camera init only
//    idf.py monitor | grep "poll|"     ← all poller events
//
//  Log-level convention used throughout this project:
//    ESP_LOGE  error path, action required
//    ESP_LOGW  unexpected but recoverable condition
//    ESP_LOGI  important one-time event (init complete, WiFi up, …)
//    ESP_LOGD  per-tick / high-frequency data (compile out in release)
//    ESP_LOGV  internal step tracing (compile out in release)
// =============================================================================

#include "esp_log.h"

// ─── Module roots ─────────────────────────────────────────────────────────────
#define TG_CAM   "cam"      // camera driver + capture loop
#define TG_NET   "net"      // WiFi / mDNS / HTTP server lifecycle
#define TG_CTL   "ctl"      // HTTP request handlers
#define TG_POLL  "poll"     // cloud polling task
#define TG_PERI  "peri"     // peripherals (servos, LED, relay)

// ─── Sub-module combinations  (compile-time string concat) ────────────────────
#define TG_CAM_INIT  TG_CAM  "|init"   // camera power-up, sensor detect, config
#define TG_CAM_STRM  TG_CAM  "|strm"   // per-frame capture stats
#define TG_NET_WIFI  TG_NET  "|wifi"   // WiFi association / retry
#define TG_NET_HTTP  TG_NET  "|http"   // HTTP server start / route registration
#define TG_CTL_CAPT  TG_CTL  "|capt"   // /capture handler
#define TG_CTL_STRM  TG_CTL  "|strm"   // /stream handler
#define TG_CTL_CMD   TG_CTL  "|cmd"    // control handlers (pan/tilt/led/switch)
#define TG_POLL_TICK TG_POLL "|tick"   // per-checkin outcome
#define TG_POLL_CMD  TG_POLL "|cmd"    // commands received from backend
#define TG_PERI_INIT TG_PERI "|init"   // peripheral hardware init
#define TG_PERI_SRV  TG_PERI "|srv"    // servo moves

// ─── Periodic stats helper ────────────────────────────────────────────────────
//  Emits one INFO log every N increments of `counter`.  Counter must be > 0
//  before the first emission (so the first log appears at N, not 0).
//
//  Usage:
//    LOG_EVERY(TG_CAM_STRM, 60, frame_count,
//              "frames=%lu drops=%lu heap=%lu", frame_count, drops, heap);
#define LOG_EVERY(tag, n, counter, fmt, ...) \
    do { if ((counter) % (n) == 0) ESP_LOGI(tag, fmt, ##__VA_ARGS__); } while (0)

#endif /* CAM_LOG_H */
