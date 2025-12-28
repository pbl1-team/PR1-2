#pragma once

#include <esp_camera.h>

extern camera_config_t camera_config;
extern camera_fb_t* fb;

void initCamera(void);

void discardStaleFrame(void);
