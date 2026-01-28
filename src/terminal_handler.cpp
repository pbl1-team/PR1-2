#include "terminal_handler.h"

#include <Arduino.h>

volatile term_flags_t termFlags;

StaticTask_t xTerminalHandlerBuf;
TaskHandle_t xTerminalHandler = NULL;
StackType_t xTerminalHandlerStack[TERM_STACK_SIZE];

char term_sbuf[TERM_SBUF_SIZE];
static size_t term_sbuf_ptr;

StaticTask_t xPumpHandlerBuf;
TaskHandle_t xPumpHandler;
StackType_t xPumpHandlerStack[PUMP_STACK_SIZE];

static char pump_sbuf[PUMP_SBUF_SIZE];
static size_t pump_sbuf_len;

void vTerminalHandler(void* params) {
  const uint8_t C_PER_ITER = 10;
  unsigned long st_time, it_time;
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

        // Buffer up
        if (tmp == '\r') {
          tmp = '\0';
        }
        if (term_sbuf_ptr + 1 >= TERM_SBUF_SIZE) {
          term_sbuf_ptr = 0;
        }
        if (tmp != '\n') {
          term_sbuf[term_sbuf_ptr] = tmp;
          term_sbuf[term_sbuf_ptr + 1] = '\0';
          ++term_sbuf_ptr;
          continue;
        }

        // Handle commands
        term_sbuf_ptr = 0;
        if (!strcmp("P-RESET", term_sbuf)) {
          termFlags.pump_reset_req = 1;
          continue;
        }
        Serial.printf("[TERM] Invalid cmd: %s\n", term_sbuf);
      }
    }

    // * Yield until next batch scan
    it_time = millis() - st_time;
    delay(it_time < TERM_SCAN_DLY ? TERM_SCAN_DLY - it_time : 0);
  }
}

#define PUMP_READLINE()                                                        \
  pump_sbuf_len = Serial2.readBytesUntil('\n', pump_sbuf, PUMP_SBUF_SIZE - 1); \
  pump_sbuf[pump_sbuf_len] = '\0'

void vPumpHandler(void* params) {
PumpHandler_begin:
  termFlags.pump_failed = 0;
  Serial2.begin(115200, SERIAL_8N1, 4, 3, false, 150);
  // Discard initial buffer contents if any
  delay(1000);
  while (Serial2.available()) {
    Serial2.read();
  }
  Serial2.printf("RESET\r\n");
  PUMP_READLINE();
  termFlags.term_stalled = 1;
  Serial.printf("[PUMP] Pump controller hello msg: \n%s\n", pump_sbuf);
  termFlags.term_stalled = 0;

  Serial2.printf("SET SNS-LB 145\n");
  PUMP_READLINE();
  PUMP_READLINE();
  while (true) {
    delay(PUMP_SCAN_DLY);
    if (termFlags.pump_reset_req) {
      termFlags.pump_reset_req = 0;
      goto PumpHandler_begin;
    }
    Serial2.printf("STAT\n");
    for (int i = 0; i < 6; i++) {
      PUMP_READLINE();
      // Serial.printf("[PUMP] %s\n", pump_sbuf);
      if (i != 2) {
        continue;
      }
      if (pump_sbuf_len != 6 || strncmp("PUMP", pump_sbuf, 4)) {
        termFlags.pump_failed = 1;
      }
      if (!termFlags.pump_failed && pump_sbuf[5] == 'E') {
        termFlags.pump_failed = 1;
        termFlags.term_stalled = 1;
        Serial.println("[PUMP] Pump fault detected\n");
        termFlags.term_stalled = 0;
      }
    }
  }
}
