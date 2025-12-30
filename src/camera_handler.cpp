#include "camera_handler.h"

#include <Arduino.h>
#include <esp_camera.h>

#include "board_config.h"

camera_config_t camera_config;
camera_fb_t* fb = NULL;

void initCamera(void) {
  camera_config.ledc_channel = LEDC_CHANNEL_0;
  camera_config.ledc_timer = LEDC_TIMER_0;
  camera_config.pin_d0 = Y2_GPIO_NUM;
  camera_config.pin_d1 = Y3_GPIO_NUM;
  camera_config.pin_d2 = Y4_GPIO_NUM;
  camera_config.pin_d3 = Y5_GPIO_NUM;
  camera_config.pin_d4 = Y6_GPIO_NUM;
  camera_config.pin_d5 = Y7_GPIO_NUM;
  camera_config.pin_d6 = Y8_GPIO_NUM;
  camera_config.pin_d7 = Y9_GPIO_NUM;
  camera_config.pin_xclk = XCLK_GPIO_NUM;
  camera_config.pin_pclk = PCLK_GPIO_NUM;
  camera_config.pin_vsync = VSYNC_GPIO_NUM;
  camera_config.pin_href = HREF_GPIO_NUM;
  camera_config.pin_sccb_sda = SIOD_GPIO_NUM;
  camera_config.pin_sccb_scl = SIOC_GPIO_NUM;
  camera_config.pin_pwdn = PWDN_GPIO_NUM;
  camera_config.pin_reset = RESET_GPIO_NUM;
  camera_config.xclk_freq_hz = 20000000;
  camera_config.frame_size = FRAMESIZE_SVGA;
  camera_config.pixel_format = PIXFORMAT_JPEG;
  camera_config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  camera_config.fb_location = CAMERA_FB_IN_PSRAM;
  camera_config.jpeg_quality = 12;
  camera_config.fb_count = 1;

  // camera init
  esp_err_t err = esp_camera_init(&camera_config);
  if (err != ESP_OK) {
    Serial.printf("[CAMERA] Camera init failed with error 0x%x", err);
    ESP.restart();
  }

  // camera settings
  sensor_t* s = esp_camera_sensor_get();
  s->set_whitebal(s, 1);  // 0 = disable , 1 = enable
  s->set_awb_gain(s, 1);  // 0 = disable , 1 = enable
  s->set_wb_mode(s, 0);
  s->set_bpc(s, 1);      // 0 = disable , 1 = enable
  s->set_wpc(s, 1);      // 0 = disable , 1 = enable
  s->set_lenc(s, 0);     // 0 = disable , 1 = enable
  s->set_hmirror(s, 0);  // 0 = disable , 1 = enable
  s->set_dcw(s, 0);      // 0 = disable , 1 = enable
}

void discardStaleFrame(void) {
  // Get and discard the first frame
  camera_fb_t* fb = NULL;
  fb = esp_camera_fb_get();
  if (fb) {
    esp_camera_fb_return(fb);
  }
}
