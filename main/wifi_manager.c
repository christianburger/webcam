// =============================================================================
//  wifi_manager.c  –  Multi-AP WiFi manager
//
//  State machine (manager_task)
//  ────────────────────────────
//
//    IDLE
//      │  wifi_manager_init()
//      ▼
//    SCANNING ◄────────────────────────────────────────────────────────────────┐
//      │  scan done: pick best visible known AP                                 │
//      │  no match → wait INTER_CYCLE_DELAY_S, loop                            │
//      ▼                                                                        │
//    CONNECTING                                                                 │
//      │  GOT_IP                            all retries exhausted              │
//      ├──────────────► CONNECTED ────── (disconnect event) ──────────────────►┤
//      │                   │                                                    │
//      │              monitor tick                                              │
//      │                   │ rssi OK → stay                                    │
//      │                   │ rssi weak N times → DEGRADED ─► scan & maybe switch
//      │                   │ rssi critical → scan & maybe switch               │
//      │                                                                        │
//      └── retries done → back to SCANNING ────────────────────────────────────┘
//
//  Thread-safety
//  ─────────────
//  s_info is written only from manager_task; reads happen from HTTP handler
//  tasks.  A spinlock (portMUX) protects the struct copy — sections are
//  short (memcpy of ~60 bytes) so spinning is safe.
// =============================================================================

#include "wifi_manager.h"
#include "cam_log.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include <string.h>
#include <stdint.h>

// ─── Credential table (populated from Kconfig at compile time) ────────────────

typedef struct { const char *ssid; const char *pass; } cred_t;

// All four slots are always compiled in.  Slots whose SSID is the empty
// string (the Kconfig default for disabled slots) are skipped at runtime
// wherever the array is iterated — preprocessor #if cannot index strings.
static const cred_t s_creds[] = {
    { CONFIG_WIFI_SSID_1, CONFIG_WIFI_PASS_1 },
    { CONFIG_WIFI_SSID_2, CONFIG_WIFI_PASS_2 },
    { CONFIG_WIFI_SSID_3, CONFIG_WIFI_PASS_3 },
    { CONFIG_WIFI_SSID_4, CONFIG_WIFI_PASS_4 },
};
#define CRED_COUNT ((int)(sizeof(s_creds) / sizeof(s_creds[0])))

// ─── Internal event bits (bits 2+ are private to this module) ─────────────────
// Bits 0-1 are the public WM_CONNECTED_BIT / WM_DISCONNECTED_BIT.
#define _EVT_GOT_IP       BIT2
#define _EVT_DISCONNECTED BIT3
#define _EVT_SCAN_DONE    BIT4   // unused (we use blocking scan)

// ─── Tuning (all values derived from Kconfig) ─────────────────────────────────
#define RSSI_WARN       CONFIG_WIFI_RSSI_WARN_THRESHOLD
#define RSSI_CRITICAL   CONFIG_WIFI_RSSI_CRITICAL_THRESHOLD
#define DEGRADE_N       CONFIG_WIFI_DEGRADED_CONFIRM_N
#define MONITOR_MS      CONFIG_WIFI_MONITOR_INTERVAL_MS
#define RETRIES_PER_NET CONFIG_WIFI_RETRIES_PER_NETWORK
#define CYCLE_DELAY_S   CONFIG_WIFI_INTER_CYCLE_DELAY_S

#define CONNECT_TIMEOUT_MS  12000
#define SCAN_MAX_APS        20

// ─── Module state ─────────────────────────────────────────────────────────────

static EventGroupHandle_t   s_evt;
static esp_netif_t         *s_netif       = NULL;
static wifi_manager_state_t s_state       = WM_STATE_IDLE;
static wifi_manager_info_t  s_info        = {0};
static portMUX_TYPE         s_info_mux    = portMUX_INITIALIZER_UNLOCKED;

static int64_t  s_connected_at_us = 0;
static uint32_t s_switches        = 0;
static uint32_t s_reconnects      = 0;
static int      s_degrade_streak  = 0;

// ─── Helpers ──────────────────────────────────────────────────────────────────

const char *wifi_manager_state_str(wifi_manager_state_t s) {
    switch (s) {
        case WM_STATE_IDLE:       return "IDLE";
        case WM_STATE_SCANNING:   return "SCANNING";
        case WM_STATE_CONNECTING: return "CONNECTING";
        case WM_STATE_CONNECTED:  return "CONNECTED";
        case WM_STATE_DEGRADED:   return "DEGRADED";
        default:                  return "UNKNOWN";
    }
}

static void set_state(wifi_manager_state_t next) {
    if (s_state != next) {
        ESP_LOGI(TG_NET_WIFI, "state %s → %s",
                 wifi_manager_state_str(s_state),
                 wifi_manager_state_str(next));
        s_state = next;
    }
}

/**
 * Flush s_info to the public snapshot (call from manager_task only).
 * Caller must already have populated all fields except state/uptime/counters,
 * which this function fills in.
 */
static void publish_info(const char *ssid, int8_t rssi, bool degraded) {
    esp_netif_ip_info_t ip = {0};
    if (s_netif) esp_netif_get_ip_info(s_netif, &ip);

    char ip_str[16] = "";
    if (ip.ip.addr)
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip.ip));

    uint32_t uptime = (s_connected_at_us > 0)
        ? (uint32_t)((esp_timer_get_time() - s_connected_at_us) / 1000000ULL)
        : 0u;

    portENTER_CRITICAL(&s_info_mux);
    s_info.state     = s_state;
    s_info.rssi_dbm  = rssi;
    s_info.degraded  = degraded;
    s_info.uptime_s  = uptime;
    s_info.switches  = s_switches;
    s_info.reconnects = s_reconnects;
    memcpy(s_info.ip, ip_str, sizeof(s_info.ip));
    if (ssid) strlcpy(s_info.ssid, ssid, sizeof(s_info.ssid));
    portEXIT_CRITICAL(&s_info_mux);
}

// ─── WiFi + IP event handlers ─────────────────────────────────────────────────

static void on_wifi_event(void *arg, esp_event_base_t base,
                          int32_t id, void *data) {
    if (id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = data;
        ESP_LOGW(TG_NET_WIFI, "STA disconnected  reason=%d", d->reason);
        xEventGroupClearBits(s_evt, WM_CONNECTED_BIT);
        xEventGroupSetBits  (s_evt, WM_DISCONNECTED_BIT | _EVT_DISCONNECTED);
    }
}

static void on_ip_event(void *arg, esp_event_base_t base,
                        int32_t id, void *data) {
    if (id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        ESP_LOGI(TG_NET_WIFI, "got IP: " IPSTR, IP2STR(&e->ip_info.ip));
        xEventGroupClearBits(s_evt, WM_DISCONNECTED_BIT | _EVT_DISCONNECTED);
        xEventGroupSetBits  (s_evt, WM_CONNECTED_BIT | _EVT_GOT_IP);
    }
}

// ─── Scan helpers ─────────────────────────────────────────────────────────────

/**
 * Perform a blocking active scan and return the credential index of the
 * known AP with the strongest visible RSSI, or -1 if none are visible.
 *
 * Uses the scan result to rank credentials — this ensures the manager
 * always connects to the physically closest / strongest network rather
 * than following a fixed priority list.
 */
static int scan_pick_best(void) {
    wifi_scan_config_t cfg = {
        .ssid        = NULL,
        .bssid       = NULL,
        .channel     = 0,
        .show_hidden = false,
        .scan_type   = WIFI_SCAN_TYPE_ACTIVE,
    };

    esp_err_t r = esp_wifi_scan_start(&cfg, true /* blocking */);
    if (r != ESP_OK) {
        ESP_LOGE(TG_NET_WIFI, "scan_start failed: %s", esp_err_to_name(r));
        return -1;
    }

    uint16_t count = SCAN_MAX_APS;
    wifi_ap_record_t aps[SCAN_MAX_APS];
    memset(aps, 0, sizeof(aps));
    esp_wifi_scan_get_ap_records(&count, aps);
    ESP_LOGI(TG_NET_WIFI, "scan: %u APs found", count);

    int    best_cred = -1;
    int8_t best_rssi = INT8_MIN;

    for (int ci = 0; ci < CRED_COUNT; ci++) {
        if (!s_creds[ci].ssid || s_creds[ci].ssid[0] == '\0') continue;
        for (uint16_t ai = 0; ai < count; ai++) {
            if (strcmp((const char *)aps[ai].ssid, s_creds[ci].ssid) == 0) {
                ESP_LOGI(TG_NET_WIFI, "  found known AP \"%s\" ch%d rssi=%d",
                         aps[ai].ssid, aps[ai].primary, aps[ai].rssi);
                if (aps[ai].rssi > best_rssi) {
                    best_rssi = aps[ai].rssi;
                    best_cred = ci;
                }
            }
        }
    }

    if (best_cred >= 0)
        ESP_LOGI(TG_NET_WIFI, "scan winner: [%d] \"%s\" rssi=%d dBm",
                 best_cred, s_creds[best_cred].ssid, best_rssi);
    else
        ESP_LOGW(TG_NET_WIFI, "scan: no known AP visible");

    return best_cred;
}

// ─── Connect helpers ──────────────────────────────────────────────────────────

/**
 * Disconnect cleanly and wait for the STA_DISCONNECTED event (or timeout).
 * Always clears internal event bits before returning so the next wait is clean.
 */
static void disconnect_blocking(void) {
    xEventGroupClearBits(s_evt, _EVT_DISCONNECTED | _EVT_GOT_IP);
    esp_wifi_disconnect();
    /* If already disconnected no event fires; 2 s timeout is sufficient. */
    xEventGroupWaitBits(s_evt, _EVT_DISCONNECTED,
                        pdTRUE /* clear */, pdFALSE, pdMS_TO_TICKS(2000));
    xEventGroupClearBits(s_evt, _EVT_DISCONNECTED | _EVT_GOT_IP);
}

/**
 * Push credentials to the driver and call esp_wifi_connect().
 * Does NOT wait for the outcome — caller must wait on _EVT_GOT_IP / _EVT_DISCONNECTED.
 */
static esp_err_t start_connect(int ci) {
    wifi_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strlcpy((char *)cfg.sta.ssid,     s_creds[ci].ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, s_creds[ci].pass, sizeof(cfg.sta.password));
    cfg.sta.threshold.authmode = (s_creds[ci].pass[0] == '\0')
                                 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    cfg.sta.pmf_cfg.capable = true;

    ESP_LOGI(TG_NET_WIFI, "connecting → \"%s\"", cfg.sta.ssid);

    portENTER_CRITICAL(&s_info_mux);
    strlcpy(s_info.ssid, s_creds[ci].ssid, sizeof(s_info.ssid));
    portEXIT_CRITICAL(&s_info_mux);

    esp_err_t r = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (r == ESP_OK) r = esp_wifi_connect();
    return r;
}

// ─── RSSI monitor ─────────────────────────────────────────────────────────────

/**
 * Sample RSSI from the current AP, update s_info, and return true if the
 * manager should trigger a background scan to find a better AP.
 */
static bool monitor_rssi(void) {
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        /* Can't read AP info — may be a transient state; the disconnect event
           handler will kick in if we actually lost the connection. */
        return false;
    }

    bool degraded = (ap.rssi < RSSI_WARN);
    publish_info(NULL, ap.rssi, degraded);

    if (!degraded) {
        if (s_degrade_streak > 0)
            ESP_LOGI(TG_NET_WIFI, "RSSI recovered: %d dBm", ap.rssi);
        s_degrade_streak = 0;
        if (s_state == WM_STATE_DEGRADED) set_state(WM_STATE_CONNECTED);
        return false;
    }

    s_degrade_streak++;
    ESP_LOGW(TG_NET_WIFI, "RSSI %d dBm (warn=%d, critical=%d)  streak=%d/%d",
             ap.rssi, RSSI_WARN, RSSI_CRITICAL, s_degrade_streak, DEGRADE_N);

    if (s_state == WM_STATE_CONNECTED) set_state(WM_STATE_DEGRADED);

    if (ap.rssi < RSSI_CRITICAL) {
        ESP_LOGE(TG_NET_WIFI, "RSSI critical — immediate scan");
        s_degrade_streak = 0;
        return true;
    }

    if (s_degrade_streak >= DEGRADE_N) {
        s_degrade_streak = 0;
        return true;
    }

    return false;
}

// ─── Manager task ─────────────────────────────────────────────────────────────

static void manager_task(void *arg) {
    ESP_LOGI(TG_NET_WIFI, "manager task started  creds=%d  core=%d",
             CRED_COUNT, xPortGetCoreID());

    while (1) {

        // ── SCAN: find the strongest visible known AP ──────────────────────
        set_state(WM_STATE_SCANNING);
        publish_info(NULL, 0, false);

        int ci = scan_pick_best();
        if (ci < 0) {
            ESP_LOGW(TG_NET_WIFI, "no known AP visible — waiting %ds", CYCLE_DELAY_S);
            vTaskDelay(pdMS_TO_TICKS((uint32_t)CYCLE_DELAY_S * 1000));
            continue;   // scan again
        }

        // ── CONNECT: try the chosen AP, retrying up to RETRIES_PER_NET ────
        set_state(WM_STATE_CONNECTING);
        bool connected = false;
        s_reconnects++;

        for (int attempt = 1; attempt <= RETRIES_PER_NET && !connected; attempt++) {
            disconnect_blocking();

            if (start_connect(ci) != ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }

            EventBits_t bits = xEventGroupWaitBits(
                s_evt,
                _EVT_GOT_IP | _EVT_DISCONNECTED,
                pdTRUE, pdFALSE,
                pdMS_TO_TICKS(CONNECT_TIMEOUT_MS));

            if (bits & _EVT_GOT_IP) {
                connected = true;
            } else {
                ESP_LOGW(TG_NET_WIFI, "[%d] \"%s\" attempt %d/%d failed",
                         ci, s_creds[ci].ssid, attempt, RETRIES_PER_NET);
                vTaskDelay(pdMS_TO_TICKS(1500));
            }
        }

        if (!connected) {
            ESP_LOGW(TG_NET_WIFI, "\"%s\" unreachable — rescanning",
                     s_creds[ci].ssid);
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        // ── CONNECTED: monitor until disconnect or degradation ─────────────
        s_connected_at_us = esp_timer_get_time();
        s_degrade_streak  = 0;
        set_state(WM_STATE_CONNECTED);
        publish_info(s_creds[ci].ssid, 0, false);
        ESP_LOGI(TG_NET_WIFI, "connected to \"%s\"  (reconnect #%lu)",
                 s_creds[ci].ssid, (unsigned long)s_reconnects);

        while (1) {
            /* Sleep for the monitor interval, but wake immediately on disconnect. */
            EventBits_t bits = xEventGroupWaitBits(
                s_evt,
                _EVT_DISCONNECTED,
                pdTRUE, pdFALSE,
                pdMS_TO_TICKS(MONITOR_MS));

            if (bits & _EVT_DISCONNECTED) {
                ESP_LOGW(TG_NET_WIFI, "connection lost — scanning for new AP");
                s_connected_at_us = 0;
                break;  // back to outer scan phase
            }

            // Periodic RSSI check
            bool should_scan = monitor_rssi();
            if (!should_scan) continue;

            // Signal is weak — scan for a better AP (non-disconnecting on 5.x)
            set_state(WM_STATE_SCANNING);
            int better = scan_pick_best();

            if (better < 0) {
                /* No known AP visible at all — likely deep RF shadow.
                   Stay connected and hope it recovers. */
                ESP_LOGW(TG_NET_WIFI, "degraded but no alternatives — staying");
                set_state(WM_STATE_DEGRADED);
                continue;
            }

            if (better == ci) {
                /* Current AP is still the best option. */
                ESP_LOGI(TG_NET_WIFI, "\"%s\" is still the best — staying",
                         s_creds[ci].ssid);
                set_state(WM_STATE_DEGRADED);
                continue;
            }

            /* A genuinely better AP was found — switch to it. */
            s_switches++;
            ESP_LOGI(TG_NET_WIFI,
                     "switching \"%s\" → \"%s\"  (switch #%lu)",
                     s_creds[ci].ssid, s_creds[better].ssid,
                     (unsigned long)s_switches);
            ci = better;
            s_connected_at_us = 0;
            xEventGroupClearBits(s_evt, WM_CONNECTED_BIT);
            xEventGroupSetBits  (s_evt, WM_DISCONNECTED_BIT);
            break;  // reconnect loop in outer while(1) takes over
        }
    } /* outer while(1) */
}

// ─── Public API ───────────────────────────────────────────────────────────────

esp_err_t wifi_manager_init(void) {
    s_evt = xEventGroupCreate();
    if (!s_evt) return ESP_ERR_NO_MEM;

    xEventGroupSetBits(s_evt, WM_DISCONNECTED_BIT);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    /* Power-save off: keep high throughput for MJPEG streaming */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID,    on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT,   IP_EVENT_STA_GOT_IP, on_ip_event,   NULL));

    ESP_LOGI(TG_NET_WIFI, "wifi_manager_init: %d credential slot(s)", CRED_COUNT);
    for (int i = 0; i < CRED_COUNT; i++)
        ESP_LOGI(TG_NET_WIFI, "  [%d] \"%s\"", i, s_creds[i].ssid);

    BaseType_t r = xTaskCreatePinnedToCore(
        manager_task, "wifi_mgr", 4096, NULL,
        configMAX_PRIORITIES - 2, NULL, 1);
    if (r != pdPASS) {
        ESP_LOGE(TG_NET_WIFI, "manager task create failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}

void wifi_manager_get_info(wifi_manager_info_t *out) {
    portENTER_CRITICAL(&s_info_mux);
    *out = s_info;
    out->state = s_state;   // always authoritative (written only in manager_task)
    portEXIT_CRITICAL(&s_info_mux);
}

EventGroupHandle_t wifi_manager_get_event_group(void) {
    return s_evt;
}
