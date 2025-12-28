#pragma once
#define TINY_GSM_MODEM_SIM7000SSL
#define TINY_GSM_RX_BUFFER 4096
#define TINY_GSM_YIELD_MS 1

#include <ArduinoHttpClient.h>
#include <TinyGsmClient.h>
#include <stdint.h>

#define MODEM_PWR_PIN 41
#define MODEM_RX_PIN D7
#define MODEM_TX_PIN D6

const int X_DEVICE_ID = 2137420;
const uint8_t MODEM_MAX_RETRIES = 3;

typedef struct modem_flags {
  uint8_t modem_initialised : 1;
  uint8_t modem_connected : 1;
  uint8_t sending_in_progress : 1;
  uint8_t c_retries : 3;
} modem_flags_t;
extern volatile modem_flags_t modemFlags;

extern TinyGsm gsmModem;
extern TinyGsmClientSecure gsmClient;
extern HttpClient htClient;

extern const char* upload_url;

#define MODEM_STACK_SIZE 4096

extern StaticTask_t xModemHandlerBuf;
extern TaskHandle_t xModemHandler;
extern StackType_t xModemHandlerStack[MODEM_STACK_SIZE];
void vModemHandler(void* params);
