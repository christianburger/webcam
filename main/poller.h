#ifndef POLLER_H
#define POLLER_H

// =============================================================================
//  poller.h  –  ESP32 cloud relay polling task
//
//  Flow per tick:
//    1. esp_camera_fb_get() → capture frame
//    2. JPEG encode (if sensor not already JPEG)
//    3. AES-128-CBC encrypt → base64
//    4. esp_camera_fb_return()  ← buffer released BEFORE the network call
//    5. POST /device/checkin { frame, status }
//    6. Parse { commands:[…] } → execute via peripherals API
//    7. Sleep for remainder of POLLER_INTERVAL_MS
//
//  Thread safety:
//    Calls esp_camera_fb_get/return independently of frame_queue.
//    With fb_count=2 one buffer may be held by stream_handler while poller
//    holds the other.  The buffer is returned in step 4, before any HTTPS I/O,
//    so it is never held across a blocking network call.
//    Peripherals API (servo_set_*, led_set, switch_set) are individually atomic.
// =============================================================================

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// ─── Backend ─────────────────────────────────────────────────────────────────
#define POLLER_BACKEND_URL \
    "https://esp32-cam-relay.burger-christian.workers.dev/device/checkin"

// ─── Authentication ───────────────────────────────────────────────────────────
//  Must match DEVICE_SECRET set as a Cloudflare Worker secret.
#define POLLER_DEVICE_KEY   "43067cdef728d962eddff77fa2fb2b87033214e2211436dc162b0cede7e5d278"

// ─── AES-128-CBC frame encryption ─────────────────────────────────────────────
//  Exactly 16 ASCII characters.  Only device + browser hold this key.
//  The backend stores and relays the encrypted blob without decrypting it.
#define POLLER_AES_KEY      "Regrub{}@2026048"
_Static_assert(sizeof(POLLER_AES_KEY) - 1 == 16,
               "POLLER_AES_KEY must be exactly 16 characters");

// ─── Timing ──────────────────────────────────────────────────────────────────
#define POLLER_INTERVAL_MS  3000    // checkin period (~20 frames/min)
#define POLLER_TIMEOUT_MS   15000   // HTTPS timeout (must cover TLS handshake)

// ─── Task settings ───────────────────────────────────────────────────────────
#define POLLER_TASK_STACK       14336   // 14 KB — TLS + base64 stack
#define POLLER_TASK_PRIORITY    (configMAX_PRIORITIES - 3)
#define POLLER_TASK_CORE        1

// ─── Command types ────────────────────────────────────────────────────────────
//  "pan"    value: 0–180 (degrees)
//  "tilt"   value: 0–180 (degrees)
//  "led"    value: 0=off | 1=on
//  "switch" value: 0=off | 1=on
//  "center" value: ignored — both servos → 90°

// ─── Public API ──────────────────────────────────────────────────────────────
void poller_task(void *pvParameters);

#endif // POLLER_H
