#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "esp_err.h"
#include "freertos/event_groups.h"

// WiFi credentials are set via 'idf.py menuconfig' → WiFi Configuration.
// Never commit real credentials to source control.
#define WIFI_SSID CONFIG_WIFI_SSID
#define WIFI_PASS CONFIG_WIFI_PASS

#define NETWORK_TASK_STACK_SIZE 8192
#define NETWORK_TASK_PRIORITY   5
#define NETWORK_TASK_CORE_ID    0

void network_task(void *pvParameters);

#endif // NETWORK_MANAGER_H