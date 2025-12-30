#include "ir_sensor_handler.h"

#include <Arduino.h>
#include <SparkFun_STHS34PF80_Arduino_Library.h>

#include "terminal_handler.h"

StaticTask_t xIRHandlerBuf;
TaskHandle_t xIRHandler;
StackType_t xIRHandlerStack[IR_STACK_SIZE];

STHS34PF80_I2C irSensor;
sths34pf80_tmos_drdy_status_t irDataReady;
sths34pf80_tmos_func_status_t irStatus;

void vIRHandler(void* params) {
  if (Wire.begin(5, 6, 400000U) != true) {
    termFlags.term_stalled = 1;
    Serial.println("[IR] I2C bus init failed");
    termFlags.term_stalled = 0;
    ESP.restart();
  }
  if (irSensor.begin() != true ||
      irSensor.setTmosODR(STHS34PF80_TMOS_ODR_AT_8Hz) != 0) {
    termFlags.term_stalled = 1;
    Serial.println("[IR] I2C sensor init failed");
    termFlags.term_stalled = 0;
    ESP.restart();
  }
  while (true) {
    irSensor.getDataReady(&irDataReady);
    if (irDataReady.drdy) {
      irSensor.getStatus(&irStatus);
    }
    delay(IR_SCAN_DLY);
  }
}
