#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

// =============================================================================
//  network_manager.h  –  HTTP server + mDNS lifecycle
//
//  network_task() waits for WM_CONNECTED_BIT from wifi_manager, then starts
//  mDNS and the HTTP server.  On disconnect it tears nothing down — the HTTP
//  server keeps listening so it is immediately ready when WiFi reconnects.
// =============================================================================

#define NETWORK_TASK_STACK_SIZE  8192
#define NETWORK_TASK_PRIORITY    5
#define NETWORK_TASK_CORE_ID     1

void network_task(void *pvParameters);

#endif /* NETWORK_MANAGER_H */
