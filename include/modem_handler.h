#pragma once

#include <Arduino.h>

#define MODEM_PWR_PIN D1
#define MODEM_RX_PIN D7
#define MODEM_TX_PIN D6

extern const char* X_DEVICE_ID;
const uint8_t MODEM_MAX_RETRIES = 3;

typedef struct modem_flags {
  uint8_t modem_initialised : 1;
  uint8_t modem_connected : 1;
  uint8_t sending_in_progress : 1;
  uint8_t c_retries : 3;
} modem_flags_t;
extern volatile modem_flags_t modemFlags;

extern const char* upload_url;

#define MODEM_STACK_SIZE 4096

extern StaticTask_t xModemHandlerBuf;
extern TaskHandle_t xModemHandler;
extern StackType_t xModemHandlerStack[MODEM_STACK_SIZE];
void vModemHandler(void* params);

// ============================================================================
// Configuration - Adjust these for your hardware
// ============================================================================
#define MODEM_SERIAL Serial1
#define MODEM_BAUD 921600
#define MODEM_DTR_PIN -1  // DTR pin (optional, set to -1 if not used)

// Timeouts
#define AT_TIMEOUT_SHORT 1000   // 1s for simple commands
#define AT_TIMEOUT_MEDIUM 5000  // 5s for network ops
#define AT_TIMEOUT_LONG 30000   // 30s for SSL/connection
#define AT_TIMEOUT_XLONG 60000  // 60s for slow operations
#define MODEM_CONN_TMOUT 15000

// Buffer sizes
#define RESPONSE_BUFFER_SIZE 2048  // Store up to 2KB response

// SSL Session ID (0-5 available on SIM7000)
#define SSL_SESSION_ID 0

// ============================================================================
// Result codes
// ============================================================================
enum ModemResult {
  MODEM_OK = 0,
  MODEM_ERROR,
  MODEM_TIMEOUT,
  MODEM_BUSY,
  MODEM_NO_CARRIER,
  MODEM_CLOSED
};

// ============================================================================
// Modem state
// ============================================================================
struct ModemState {
  char responseBuffer[RESPONSE_BUFFER_SIZE];
  size_t responseLen;
  bool initialized;
  bool connected;
};

extern ModemState modem;

// ============================================================================
// Core functions
// ============================================================================
void modemInit();
ModemResult modemSendAT(const char* cmd, const char* expectedResp,
                        uint32_t timeout);
ModemResult modemSendATF(const char* expectedResp, uint32_t timeout,
                         const char* fmt, ...);

// ============================================================================
// Setup functions
// ============================================================================
ModemResult modemWaitReady();
ModemResult modemSetupNetwork(const char* apn, const char* user = "",
                              const char* pass = "");
ModemResult modemConfigureSSL();
ModemResult modemConfigureDTR();

// ============================================================================
// SSL Connection functions (using CA* commands)
// ============================================================================
ModemResult modemSSLOpen(const char* host, uint16_t port);
ModemResult modemSSLClose();

ModemResult modemSSLReceiveData(uint32_t timeout);
ModemResult modemSSLSendData(const uint8_t* data, size_t len);

// ============================================================================
// High-level:  Send HTTP POST with JPEG and get response
// ============================================================================
ModemResult modemHttpPostJpeg(
    const char* host, uint16_t port, const char* path, const uint8_t* jpegData,
    size_t jpegLen,
    const char* extraHeaders = nullptr  // Optional extra headers
);

// ============================================================================
// Utility
// ============================================================================
void modemSleep();
void modemWake();
int modemGetSignalQuality();
void modemDTRLow();   // Pull DTR low (active)
void modemDTRHigh();  // Pull DTR high (for exiting transparent mode if
                      // configured)
