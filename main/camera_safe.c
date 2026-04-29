#ifndef CAMERA_SAFE_H
#define CAMERA_SAFE_H

#include "esp_camera.h"
#include "freertos/semphr.h"

extern SemaphoreHandle_t camera_mutex;

camera_fb_t* safe_camera_fb_get(void);
void safe_camera_fb_return(camera_fb_t *fb);

#endif /* CAMERA_SAFE_H */