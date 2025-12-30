#include <Arduino.h>
#include <esp_camera.h>

#include "camera_handler.h"
#include "ir_sensor_handler.h"
#include "modem_handler.h"
#include "terminal_handler.h"

#define FPS 5
#define FPS_DLY 1000 / FPS
static unsigned long it_st_time, it_time;

typedef struct userFlags {
  uint8_t take_pic : 1;
  uint8_t sensor_pressed : 1;
  uint8_t ir_presense_detected : 1;
  uint8_t ir_motion_detected : 1;
} user_flags_t;
static volatile user_flags_t userFlags;

#define ASENSOR_PIN A0
#define ASENSOR_SCAN_DLY 20
#define ASENSOR_WINDOW_SIZE (20 * 1000 / ASENSOR_SCAN_DLY)
#define ASENSOR_WINDOW2_SIZE (3 * 1000 / ASENSOR_SCAN_DLY)
const uint16_t asensor_base_val = 75;
const uint8_t asensor_multiplier = 16;
static uint64_t asensor_avg, asensor2_avg;

static StaticTimer_t xTimer1Buf;
static TimerHandle_t xTimer1;
static void vTimer1Callback(TimerHandle_t xExpiredTimer) {
  uint64_t adcval = analogRead(ASENSOR_PIN);

  // Read IR sensor data
  if (irDataReady.drdy) {
    irDataReady.drdy = 0;
    userFlags.ir_motion_detected = irStatus.mot_flag;
    userFlags.ir_presense_detected = irStatus.pres_flag;
  }

  if (!userFlags.ir_presense_detected) {
    // Update long average window
    asensor_avg -= asensor_avg / ASENSOR_WINDOW_SIZE;
    asensor_avg += ((adcval + asensor_base_val) << asensor_multiplier) /
                   ASENSOR_WINDOW_SIZE;
  }
  // Update short average window
  asensor2_avg -= asensor2_avg / ASENSOR_WINDOW2_SIZE;
  asensor2_avg += (adcval << asensor_multiplier) / ASENSOR_WINDOW2_SIZE;

  if (!userFlags.take_pic) {
    // Detect press
    if (!userFlags.sensor_pressed &&
        (asensor2_avg + userFlags.ir_motion_detected * asensor_base_val) * 100 >
            asensor_avg * 107) {
      userFlags.take_pic = 1;
      userFlags.sensor_pressed = 1;
    }
    // Detect depress
    if (userFlags.sensor_pressed &&
        (asensor2_avg - userFlags.ir_motion_detected * asensor_base_val) * 100 <
            asensor_avg * 104) {
      userFlags.sensor_pressed = 0;
      // To avoid sudden re-trigger and accelerate threshold level readjust
      asensor2_avg = asensor2_avg * 7 / 10;
      asensor_avg = asensor_avg * 9 / 10;
    }
  }
}

void setup() {
  // Initialise terminal
  Serial.begin(CONFIG_MONITOR_BAUD);
  termFlags.term_stalled = 1;
  pinMode(LED_BUILTIN, OUTPUT);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  asensor_avg = (asensor_base_val << asensor_multiplier);
  // ? Timer -> the sensor reading and JUST reading code should go into this
  // ? timer
  xTimer1 =
      xTimerCreateStatic("T1", ASENSOR_SCAN_DLY / portTICK_PERIOD_MS, pdTRUE,
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

  xPumpHandler = xTaskCreateStaticPinnedToCore(
      //
      vPumpHandler, "PUMP", PUMP_STACK_SIZE,
      //
      (void*)0x00, tskIDLE_PRIORITY,
      //
      xPumpHandlerStack, &xPumpHandlerBuf,
      // CPU0
      0
      //
  );

  xModemHandler = xTaskCreateStaticPinnedToCore(
      //
      vModemHandler, "MODEM", MODEM_STACK_SIZE, (void*)0x00,
      //
      (tskIDLE_PRIORITY + 10),
      //
      xModemHandlerStack, &xModemHandlerBuf,
      // CPU1
      1
      //
  );

  xIRHandler = xTaskCreateStaticPinnedToCore(
      //
      vIRHandler, "IRSNS", IR_STACK_SIZE, (void*)0x00,
      //
      (tskIDLE_PRIORITY + 5),
      //
      xIRHandlerStack, &xIRHandlerBuf,
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
