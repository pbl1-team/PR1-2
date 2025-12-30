#pragma once

#include <Arduino.h>

// The serial handler might call string functions which need some stack
#define TERM_STACK_SIZE 4096
#define TERM_SBUF_SIZE 1024
#define TERM_SCAN_DLY 4

#define PUMP_STACK_SIZE 4096
#define PUMP_SCAN_DLY 400
#define PUMP_SBUF_SIZE 240

extern StaticTask_t xTerminalHandlerBuf;
extern TaskHandle_t xTerminalHandler;
extern StackType_t xTerminalHandlerStack[TERM_STACK_SIZE];

extern char term_sbuf[TERM_SBUF_SIZE];

extern StaticTask_t xPumpHandlerBuf;
extern TaskHandle_t xPumpHandler;
extern StackType_t xPumpHandlerStack[PUMP_STACK_SIZE];

typedef struct term_flags {
  uint8_t term_stalled : 1;
  uint8_t pump_failed : 1;
  uint8_t pump_reset_req : 1;
} term_flags_t;

extern volatile term_flags_t termFlags;

void vTerminalHandler(void* params);
void vPumpHandler(void* params);
