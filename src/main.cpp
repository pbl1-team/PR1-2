#include <Arduino.h>
#include <HTTPClient.h>
#include <esp_camera.h>

#include "camera_handler.h"
#include "modem_handler.h"
#include "terminal_handler.h"

#define FPS 10
#define FPS_DLY 1000 / FPS
static unsigned long it_st_time, it_time;

typedef struct userFlags {
  uint8_t sensor_prev : 1;
  uint8_t take_pic : 1;
} user_flags_t;
static volatile user_flags_t userFlags;

static StaticTimer_t xTimer1Buf;
static TimerHandle_t xTimer1;
static void vTimer1Callback(TimerHandle_t xExpiredTimer) {
  // FIXME: remove dummy code
  userFlags.take_pic = 1;
}

void setup() {
  // Initialise terminal
  Serial.begin(CONFIG_MONITOR_BAUD);
  termFlags.term_stalled = 1;

  // ? Timer -> the sensor reading and JUST reading code should go into this
  // ? timer
  pinMode(LED_BUILTIN, OUTPUT);
  xTimer1 = xTimerCreateStatic("T1", 2000 / portTICK_PERIOD_MS, pdTRUE,
                               (void*)0x00, vTimer1Callback, &xTimer1Buf);
  xTimerStart(xTimer1, 0);

  /*
  ? Terminal handler task -> put all the serial terminal handling code there
  * The task will read next C_PER_ITER characters and then go to sleep for
  * TERM_SCAN_DLY [ms]
  */
  xTerminalHandler = xTaskCreateStaticPinnedToCore(
      //
      vTerminalHandler, "TERM", TERM_STACK_SIZE,
      //
      (void*)0x00, (tskIDLE_PRIORITY + 1),
      //
      xTerminalHandlerStack, &xTerminalHandlerBuf,
      // CPU0
      0
      //
  );

  xModemHandler = xTaskCreateStaticPinnedToCore(
      //
      vModemHandler, "MODEM", MODEM_STACK_SIZE, (void*)0x00,
      //
      (tskIDLE_PRIORITY + 5),
      //
      xModemHandlerStack, &xModemHandlerBuf,
      // CPU1
      1
      //
  );

  initCamera();

  // LED_BUILTIN HIGH -> OFF
  digitalWrite(LED_BUILTIN, HIGH);
  termFlags.term_stalled = 0;
}

void loop() {
  // Get loop start time
  it_st_time = millis();

  if (userFlags.take_pic) {
    // Read camera frame
    digitalWrite(LED_BUILTIN, LOW);
    discardStaleFrame();
    delay(300);

    fb = esp_camera_fb_get();

    digitalWrite(LED_BUILTIN, HIGH);

    // Check if frame exists and upload it if so
    if (fb) {
      if (modemFlags.modem_initialised && modemFlags.modem_connected) {
        modemFlags.sending_in_progress = 1;

        while (modemFlags.sending_in_progress) {
          delay(FPS_DLY);
        }
      }

      esp_camera_fb_return(fb);
    } else {
      Serial.println("Failed to read camera frame!");
    }

    userFlags.take_pic = 0;
  }

  // Yield until next scan is due
  it_time = millis() - it_st_time;
  delay(it_time < FPS_DLY ? FPS_DLY - it_time : 0);
}
