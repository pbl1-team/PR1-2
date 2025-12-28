#include "terminal_handler.h"

#include <Arduino.h>

volatile term_flags_t termFlags;

StaticTask_t xTerminalHandlerBuf;
TaskHandle_t xTerminalHandler = NULL;
StackType_t xTerminalHandlerStack[TERM_STACK_SIZE];

void vTerminalHandler(void* params) {
  const uint8_t C_PER_ITER = 10;
  unsigned long st_time, it_time;
  // ! A task cannot exit (or the board will crash)
  while (true) {
    st_time = millis();

    // * Grab up to C_PER_ITER characters per run
    for (uint8_t i = 0; i < C_PER_ITER; i++) {
      if (!Serial.available()) {
        break;
      }

      char tmp = Serial.read();
      if (!termFlags.term_stalled) {
        // Echo
        Serial.write(tmp);
        // Buffer up and handle serial comms here
      }
    }

    // * Yield until next batch scan
    it_time = millis() - st_time;
    delay(it_time < TERM_SCAN_DLY ? TERM_SCAN_DLY - it_time : 0);
  }
}
