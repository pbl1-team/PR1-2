#pragma once

#include <Arduino.h>

// The serial handler might call string functions which need some stack
#define TERM_STACK_SIZE 4096
#define TERM_SCAN_DLY 4

extern StaticTask_t xTerminalHandlerBuf;
extern TaskHandle_t xTerminalHandler;
extern StackType_t xTerminalHandlerStack[TERM_STACK_SIZE];

typedef struct term_flags {
  uint8_t term_stalled : 1;
} term_flags_t;

extern volatile term_flags_t termFlags;

void vTerminalHandler(void* params);
