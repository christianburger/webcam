#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

// =============================================================================
//  wifi_manager.h  –  Multi-AP WiFi connection manager for ESP32
//
//  Responsibilities
//  ────────────────
//  • Holds a table of up to 4 SSID/password pairs (configured via menuconfig).
//  • On start: scans for visible APs, connects to the known AP with the
//    strongest signal (best RSSI wins, not list order).
//  • While connected: samples RSSI every WIFI_MONITOR_INTERVAL_MS ms.
//    - RSSI < WIFI_RSSI_WARN_THRESHOLD  for N consecutive ticks → degraded,
//      background scan, switch to stronger AP if one is found.
//    - RSSI < WIFI_RSSI_CRITICAL_THRESHOLD (single reading) → immediate scan.
//  • On disconnect: scans immediately and reconnects; retries indefinitely.
//  • Exposes a thread-safe info snapshot for the health endpoint.
//
//  Public event group bits
//  ───────────────────────
//    WM_CONNECTED_BIT    – set while IP is held; cleared on disconnect.
//    WM_DISCONNECTED_BIT – set while not connected; cleared on IP acquired.
// =============================================================================

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <stdint.h>
#include <stdbool.h>

// ─── Public event-group bits ──────────────────────────────────────────────────
#define WM_CONNECTED_BIT    BIT0
#define WM_DISCONNECTED_BIT BIT1

// ─── State enum ───────────────────────────────────────────────────────────────
typedef enum {
    WM_STATE_IDLE       = 0,  // not yet started
    WM_STATE_SCANNING,        // performing an active scan
    WM_STATE_CONNECTING,      // association in progress
    WM_STATE_CONNECTED,       // IP held, signal OK
    WM_STATE_DEGRADED,        // IP held, signal below warning threshold
} wifi_manager_state_t;

// ─── Thread-safe status snapshot ─────────────────────────────────────────────
typedef struct {
    wifi_manager_state_t state;
    char                 ssid[33];    // current (or last-attempted) SSID
    int8_t               rssi_dbm;    // last sampled RSSI; 0 when disconnected
    char                 ip[16];      // dotted-decimal IPv4 or "" when no IP
    uint32_t             uptime_s;    // seconds since last successful connect
    bool                 degraded;    // true while RSSI below warn threshold
    uint32_t             switches;    // total AP switches since boot
    uint32_t             reconnects;  // total reconnect cycles since boot
} wifi_manager_info_t;

// ─── Public API ───────────────────────────────────────────────────────────────

/**
 * Initialise the WiFi stack and start the manager task.
 *
 * Must be called after nvs_flash_init() and before any use of the
 * WM_CONNECTED_BIT event group.  Calls esp_netif_init() and
 * esp_event_loop_create_default() internally — do not call those elsewhere.
 *
 * @return ESP_OK on success, propagated ESP-IDF error otherwise.
 */
esp_err_t wifi_manager_init(void);

/**
 * Copy a thread-safe snapshot of the current WiFi status into *out*.
 * Safe to call from any task at any time.
 */
void wifi_manager_get_info(wifi_manager_info_t *out);

/**
 * Return the event group used to signal connection state.
 * WM_CONNECTED_BIT is set while the manager holds an IP address.
 */
EventGroupHandle_t wifi_manager_get_event_group(void);

/** Human-readable label for a state value (never NULL). */
const char *wifi_manager_state_str(wifi_manager_state_t s);

#endif /* WIFI_MANAGER_H */
