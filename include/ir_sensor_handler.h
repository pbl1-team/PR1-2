#pragma once

#include <Arduino.h>
#include <SparkFun_STHS34PF80_Arduino_Library.h>
#include <Wire.h>

// The serial handler might call string functions which need some stack
#define IR_STACK_SIZE 4096

const uint32_t IR_SCAN_DLY = 10;

extern StaticTask_t xIRHandlerBuf;
extern TaskHandle_t xIRHandler;
extern StackType_t xIRHandlerStack[IR_STACK_SIZE];

extern sths34pf80_tmos_drdy_status_t irDataReady;
extern sths34pf80_tmos_func_status_t irStatus;

void vIRHandler(void* params);
