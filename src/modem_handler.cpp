#include "modem_handler.h"

#include <Arduino.h>

#include "camera_handler.h"
#include "terminal_handler.h"

const char* upload_url = "/api/captured/image";
static const char* upload_server = "mkcieslak.synology.me";
const char* X_DEVICE_ID = "X-Device-Id: 2137420";
static char headers[2048];

volatile modem_flags_t modemFlags;

#define MODEM_RESTART(msg)                             \
  {                                                    \
    termFlags.term_stalled = 1;                        \
    Serial.printf("[MODEM] %s, restarting...\n", msg); \
    termFlags.term_stalled = 0;                        \
    goto vModemHandler_start;                          \
  }

StaticTask_t xModemHandlerBuf;
TaskHandle_t xModemHandler;
StackType_t xModemHandlerStack[MODEM_STACK_SIZE];

void vModemHandler(void* params) {
  pinMode(MODEM_PWR_PIN, OUTPUT);
  digitalWrite(MODEM_PWR_PIN, LOW);
  MODEM_SERIAL.setRxBufferSize(1024);  // Increase RX buffer
  delay(2500);
vModemHandler_start:
  modemFlags.modem_initialised = 0;
  modemFlags.modem_connected = 0;
  modemFlags.sending_in_progress = 0;
  modemFlags.c_retries = 0;
  digitalWrite(MODEM_PWR_PIN, LOW);
  delay(1500);

  digitalWrite(MODEM_PWR_PIN, HIGH);
  modemInit();
  delay(5000);

  if (modemWaitReady() != MODEM_OK ||
      modemSendAT("ATE0", "OK", AT_TIMEOUT_SHORT) != MODEM_OK) {
    MODEM_RESTART("Init failed");
  }
  modemFlags.modem_initialised = 1;

  if (modemSetupNetwork("internet", "internet", "internet") != MODEM_OK) {
    MODEM_RESTART("Failed to connect to internet");
  }
  modemFlags.modem_connected = 1;

  Serial.println("[MODEM] Connected");

  uint16_t cnt = 0;
  while (true) {
    if (modemFlags.sending_in_progress) {
      // Upload the resulting file
      Serial.printf("[MODEM] Sending %d bytes of stuff\n", fb->len);

      ModemResult res;
      int res_code = -1;
      if (modemConfigureSSL() != MODEM_OK) {
        Serial.println("[MODEM] SSL config failed");
        goto vModemHandler_endreq;
      }
      Serial.println("[MODEM] SSL configured");

      res = modemHttpPostJpeg(upload_server,  // Host
                              2137,           // Port
                              upload_url,     // Path
                              fb->buf,        // JPEG data
                              fb->len,        // Length
                              X_DEVICE_ID     // Extra headers
      );

      if (res == MODEM_OK || res == MODEM_CLOSED) {
        Serial.println("[MODEM] Upload complete!");
      } else {
        Serial.printf("[MODEM] Upload failed:  %d\n", res);
      }

    vModemHandler_endreq:
      if (res_code != 200 && modemFlags.c_retries < MODEM_MAX_RETRIES) {
        ++modemFlags.c_retries;
      }
      if (res_code == 200) {
        modemFlags.c_retries = 0;
      }

      modemFlags.sending_in_progress = 0;
    } else {
      if (modemFlags.c_retries >= MODEM_MAX_RETRIES) {
        MODEM_RESTART("Max retries reached");
      }
      if (cnt % 29 == 0) {
        if (modemSendAT("AT", "OK", AT_TIMEOUT_SHORT) != MODEM_OK) {
          MODEM_RESTART("Went away");
        }
      }
      if (cnt % 79 == 0) {
        uint8_t flag = 0;
        ModemResult res = modemSendAT("AT+CREG?", "+CREG:", AT_TIMEOUT_SHORT);
        if (res == MODEM_OK) {
          // Check for registered (1) or roaming (5)
          if (strstr(modem.responseBuffer, ",1") ||
              strstr(modem.responseBuffer, ",5")) {
            flag = 1;
          }
        }
        if (!flag) {
          MODEM_RESTART("Disconnected from operator network");
        }
      }
      if (cnt % 179 == 0) {
        uint8_t flag = 0;
        ModemResult res = modemSendAT("AT+CGREG?", "+CGREG:", AT_TIMEOUT_SHORT);
        if (res == MODEM_OK) {
          if (strstr(modem.responseBuffer, ",1") ||
              strstr(modem.responseBuffer, ",5")) {
            Serial.println("[MODEM] Alive and well");
            flag = 1;
          }
        }
        if (!flag) {
          MODEM_RESTART("Disconnected from internet");
        }
      }

      ++cnt;
    }
    delay(20);
  }
}

#include <stdarg.h>

ModemState modem = {0};

// ============================================================================
// Core Functions
// ============================================================================

void modemInit() {
  MODEM_SERIAL.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
  memset(&modem, 0, sizeof(modem));

// Configure DTR pin if defined
#if MODEM_DTR_PIN >= 0
  pinMode(MODEM_DTR_PIN, OUTPUT);
  digitalWrite(MODEM_DTR_PIN, LOW);  // Keep DTR low (active) by default
#endif

  delay(100);
}

void modemDTRLow() {
#if MODEM_DTR_PIN >= 0
  digitalWrite(MODEM_DTR_PIN, LOW);
#endif
}

void modemDTRHigh() {
#if MODEM_DTR_PIN >= 0
  digitalWrite(MODEM_DTR_PIN, HIGH);
#endif
}

// Drain any pending data from serial buffer
static void modemDrain() {
  while (MODEM_SERIAL.available()) {
    MODEM_SERIAL.read();
    delay(1);  // Yield
  }
}

// Read response until we see expected string, "ERROR", or timeout
// Stores everything in modem.responseBuffer
ModemResult modemSendAT(const char* cmd, const char* expectedResp,
                        uint32_t timeout) {
  modemDrain();

  // Clear response buffer
  modem.responseLen = 0;
  memset(modem.responseBuffer, 0, RESPONSE_BUFFER_SIZE);

  // Send command
  if (cmd != nullptr && strlen(cmd) > 0) {
    MODEM_SERIAL.print(cmd);
    MODEM_SERIAL.print("\r\n");
    MODEM_SERIAL.flush();  // Wait for TX to complete
    delay(1);              // Small yield after sending
  }

  uint32_t start = millis();
  bool foundExpected = false;
  bool foundError = false;

  while (millis() - start < timeout) {
    while (MODEM_SERIAL.available() &&
           modem.responseLen < RESPONSE_BUFFER_SIZE - 1) {
      char c = MODEM_SERIAL.read();
      modem.responseBuffer[modem.responseLen++] = c;
      modem.responseBuffer[modem.responseLen] = '\0';

      // Check for expected response
      if (expectedResp && strstr(modem.responseBuffer, expectedResp)) {
        foundExpected = true;
      }

      // Check for error responses
      if (strstr(modem.responseBuffer, "ERROR") ||
          strstr(modem.responseBuffer, "+CASTATE:  0") ||
          strstr(modem.responseBuffer, "+CADATAIND: 0,0")) {
        foundError = true;
      }
    }

    // If we found expected response, wait a bit for trailing data
    if (foundExpected) {
      delay(50);
      while (MODEM_SERIAL.available() &&
             modem.responseLen < RESPONSE_BUFFER_SIZE - 1) {
        modem.responseBuffer[modem.responseLen++] = MODEM_SERIAL.read();
      }
      modem.responseBuffer[modem.responseLen] = '\0';
      return MODEM_OK;
    }

    if (foundError && !foundExpected) {
      return MODEM_ERROR;
    }

    delay(10);  // Yield to other tasks
  }

  return foundExpected ? MODEM_OK : MODEM_TIMEOUT;
}

// Printf-style AT command
ModemResult modemSendATF(const char* expectedResp, uint32_t timeout,
                         const char* fmt, ...) {
  char cmdBuf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(cmdBuf, sizeof(cmdBuf), fmt, args);
  va_end(args);
  return modemSendAT(cmdBuf, expectedResp, timeout);
}

// ============================================================================
// Setup Functions
// ============================================================================

ModemResult modemWaitReady() {
  // Try AT a few times until modem responds
  for (int i = 0; i < 10; i++) {
    if (modemSendAT("AT", "OK", AT_TIMEOUT_SHORT) == MODEM_OK) {
      modem.initialized = true;
      return MODEM_OK;
    }
    delay(50);
  }
  return MODEM_TIMEOUT;
}

ModemResult modemConfigureDTR() {
  // Also configure with AT&D for standard behavior
  // AT&D1 : ON->OFF transition causes escape to command mode
  return modemSendAT("AT&D1", "OK", AT_TIMEOUT_SHORT);
}

ModemResult modemSetupNetwork(const char* apn, const char* user,
                              const char* pass) {
  ModemResult res;

  // Disable echo
  res = modemSendAT("ATE0", "OK", AT_TIMEOUT_SHORT);
  if (res != MODEM_OK) return res;
  delay(50);

  res = modemSendAT("AT+CLTS=1", "OK", AT_TIMEOUT_SHORT);
  if (res != MODEM_OK) return res;
  delay(50);

  // Configure DTR behavior
  modemConfigureDTR();
  delay(50);

  // Wait for network registration
  Serial.println("[MODEM] Waiting for network registration...");
  for (int i = 0; i < 60; i++) {
    res = modemSendAT("AT+CREG?", "+CREG:", AT_TIMEOUT_SHORT);
    if (res == MODEM_OK) {
      // Check for registered (1) or roaming (5)
      if (strstr(modem.responseBuffer, ",1") ||
          strstr(modem.responseBuffer, ",5")) {
        break;
      }
    }
    delay(500);
  }
  if (res != MODEM_OK) {
    return MODEM_ERROR;
  }

  // Check GPRS registration too
  Serial.println("[MODEM] Waiting for GPRS registration...");
  for (int i = 0; i < 30; i++) {
    res = modemSendAT("AT+CGREG?", "+CGREG:", AT_TIMEOUT_SHORT);
    if (res == MODEM_OK) {
      if (strstr(modem.responseBuffer, ",1") ||
          strstr(modem.responseBuffer, ",5")) {
        break;
      }
    }
    delay(500);
  }
  if (res != MODEM_OK) {
    return MODEM_ERROR;
  }

  // Deactivate any existing PDP context first
  modemSendAT("AT+CNACT=0,0", "OK", AT_TIMEOUT_MEDIUM);
  delay(500);

  // Configure PDP context for app network (used by CA* commands)
  // AT+CNACT=<pdpidx>,<action>
  // pdpidx = 0, action = 1 (activate)

  // First set APN for PDP context 0
  res = modemSendATF("OK", AT_TIMEOUT_SHORT, "AT+CSTT=\"%s\",\"%s\",\"%s\"",
                     apn, user ? user : "", pass ? pass : "");
  if (res != MODEM_OK) {
    Serial.println("[MODEM] Failed to configure APN, continuing...");
  }
  delay(100);

  // Activate PDP context
  Serial.println("[MODEM] Activating PDP context...");
  res = modemSendAT("AT+CNACT=1", "OK", AT_TIMEOUT_LONG);
  if (res != MODEM_OK) {
    // Check if already active
    res = modemSendAT("AT+CNACT?", "+CNACT:  1,", AT_TIMEOUT_SHORT);
    if (res != MODEM_OK) {
      Serial.println("[MODEM] Failed to activate network");
      return res;
    }
  }
  delay(500);

  // Verify we got an IP
  res = modemSendAT("AT+CNACT?", "+CNACT: 1,", AT_TIMEOUT_SHORT);
  if (res != MODEM_OK) {
    Serial.println("[MODEM] Network not active");
    return res;
  }
  Serial.printf("[MODEM] Network active:  %s\n", modem.responseBuffer);

  return MODEM_OK;
}

ModemResult modemConfigureSSL() {
  ModemResult res;

  modemSendATF("OK", AT_TIMEOUT_MEDIUM, "AT+CACLOSE=%d", SSL_SESSION_ID);

  res = modemSendAT("AT+CACFG=\"transwaittm\",1", "OK", AT_TIMEOUT_SHORT);
  if (res != MODEM_OK) return res;
  delay(50);

  res = modemSendAT("AT+CACFG=\"transpktsize\",1024", "OK", AT_TIMEOUT_SHORT);
  if (res != MODEM_OK) return res;
  delay(50);

  res = modemSendATF("OK", AT_TIMEOUT_SHORT, "AT+CACID=%d", SSL_SESSION_ID);
  if (res != MODEM_OK) return res;
  delay(50);

  // Configure SSL parameters for session 0
  // AT+CSSLCFG="sslversion",<ssl_ctx_index>,<sslversion>
  // sslversion:  0=SSL3.0, 1=TLS1.0, 2=TLS1.1, 3=TLS1.2
  res = modemSendATF("OK", AT_TIMEOUT_SHORT, "AT+CSSLCFG=\"sslversion\",%d,3",
                     SSL_SESSION_ID);
  if (res != MODEM_OK) return res;
  delay(50);

  // Set SSL context to use for CAOPEN
  // AT+CASSLCFG=<cid>,"SSL",<ssl_ctx_index>
  res = modemSendATF("OK", AT_TIMEOUT_SHORT, "AT+CASSLCFG=%d,\"ssl\",1",
                     SSL_SESSION_ID);
  if (res != MODEM_OK) return res;
  delay(50);

  res = modemSendATF("OK", AT_TIMEOUT_SHORT, "AT+CSSLCFG=\"ctxindex\",%d",
                     SSL_SESSION_ID);
  if (res != MODEM_OK) return res;
  delay(50);

  return MODEM_OK;
}

// ============================================================================
// SSL Connection Functions (CA* commands)
// ============================================================================

ModemResult modemSSLOpen(const char* host, uint16_t port) {
  ModemResult res;

  res = modemSendATF("OK", AT_TIMEOUT_SHORT, "AT+CSSLCFG=\"sni\",%d,\"%s\"",
                     SSL_SESSION_ID, host);
  if (res != MODEM_OK) return res;

  // Open SSL connection
  // AT+CAOPEN=<cid>,<pdp_index>,<conn_type>,<server>,<port>
  // cid = session id (0-5)
  // pdp_index = PDP context (0)
  // conn_type = "TCP" or "UDP"
  // Returns +CAOPEN: <cid>,<result> where result 0 = success

  Serial.printf("[MODEM] Opening SSL connection to %s:%d.. .\n", host, port);
  res = modemSendATF("+CAOPEN: 0,0", AT_TIMEOUT_LONG, "AT+CAOPEN=%d,\"%s\",%d",
                     SSL_SESSION_ID, host, port);

  if (res != MODEM_OK) {
    Serial.printf("[MODEM] CAOPEN failed: %s\n", modem.responseBuffer);
    return res;
  }

  modem.connected = true;
  Serial.println("[MODEM] SSL connection established");
  return MODEM_OK;
}

// Send data using command mode instead of transparent mode
// AT+CASEND=<cid>,<length>
// Then send exactly <length> bytes
// Modem responds with OK when done

ModemResult modemSSLSendData(const uint8_t* data, size_t len) {
  ModemResult res;
  const size_t MAX_CHUNK = 250;  // safe chunk size
  size_t sent = 0;

  while (sent < len) {
    size_t chunkSize = min(MAX_CHUNK, len - sent);

    // Tell modem we're sending N bytes
    res = modemSendATF(">", AT_TIMEOUT_MEDIUM, "AT+CASEND=%d,%d",
                       SSL_SESSION_ID, chunkSize);
    if (res != MODEM_OK) {
      Serial.printf("[MODEM] CASEND prompt failed: %d\n", res);
      return res;
    }

    // Now send exactly chunkSize bytes - modem is counting
    size_t written = 0;
    while (written < chunkSize) {
      size_t toWrite = min((size_t)256, chunkSize - written);
      MODEM_SERIAL.write(data + sent + written, toWrite);
      written += toWrite;
      // Small yield OK here - modem is buffering until it gets chunkSize bytes
      delay(1);
    }
    MODEM_SERIAL.flush();

    // Wait for OK confirming data was sent
    // Read response (might get +CASEND:  0,<len> and OK)
    uint32_t start = millis();
    bool gotOK = false;
    modem.responseLen = 0;

    while (millis() - start < AT_TIMEOUT_MEDIUM) {
      while (MODEM_SERIAL.available() &&
             modem.responseLen < RESPONSE_BUFFER_SIZE - 1) {
        modem.responseBuffer[modem.responseLen++] = MODEM_SERIAL.read();
        modem.responseBuffer[modem.responseLen] = '\0';
      }

      if (strstr(modem.responseBuffer, "OK")) {
        gotOK = true;
        break;
      }
      if (strstr(modem.responseBuffer, "ERROR")) {
        Serial.printf("[MODEM] CASEND error: %s\n", modem.responseBuffer);
        return MODEM_ERROR;
      }
      delay(10);
    }

    if (!gotOK) {
      Serial.println("[MODEM] CASEND timeout waiting for OK");
      return MODEM_TIMEOUT;
    }

    sent += chunkSize;
    Serial.printf("[MODEM] Sent %u/%u bytes\n", sent, len);
  }

  return MODEM_OK;
}

// Receive data using AT+CARECV
ModemResult modemSSLReceiveData(uint32_t timeout) {
  modem.responseLen = 0;
  memset(modem.responseBuffer, 0, RESPONSE_BUFFER_SIZE);

  uint32_t start = millis();

  while (millis() - start < timeout) {
    // Check if data is available
    // +CADATAIND: <cid>,<length> is URC indicating data available

    // Try to receive up to buffer size
    ModemResult res = modemSendATF(
        "+CARECV:", AT_TIMEOUT_MEDIUM, "AT+CARECV=%d,%d", SSL_SESSION_ID,
        RESPONSE_BUFFER_SIZE - modem.responseLen - 1);

    if (res == MODEM_OK) {
      // Parse +CARECV: <cid>,<len>,<data>
      char* p = strstr(modem.responseBuffer, "+CARECV:");
      if (p) {
        int cid, len;
        if (sscanf(p, "+CARECV:  %d,%d,", &cid, &len) == 2) {
          // Find start of data (after second comma)
          char* dataStart = strchr(p + 8, ',');
          if (dataStart) {
            dataStart = strchr(dataStart + 1, ',');
            if (dataStart) {
              dataStart++;  // Skip comma
              // Copy data to response buffer
              size_t toCopy = min((size_t)len,
                                  RESPONSE_BUFFER_SIZE - modem.responseLen - 1);
              memcpy(modem.responseBuffer + modem.responseLen, dataStart,
                     toCopy);
              modem.responseLen += toCopy;
            }
          }

          if (len > 0) {
            start = millis();  // Reset timeout on data received
            continue;
          }
        }
      }
    }

    // Check if connection closed
    res = modemSendATF("+CASTATE:", AT_TIMEOUT_SHORT, "AT+CASTATE?");
    if (strstr(modem.responseBuffer, "+CASTATE: 0") ||
        strstr(modem.responseBuffer, "+CASTATE: 3")) {
      // 0 = closed, 3 = closing
      break;
    }

    delay(100);  // Poll interval
  }

  return modem.responseLen > 0 ? MODEM_OK : MODEM_TIMEOUT;
}

ModemResult modemSSLClose() {
  // AT+CACLOSE=<cid>
  ModemResult res =
      modemSendATF("OK", AT_TIMEOUT_MEDIUM, "AT+CACLOSE=%d", SSL_SESSION_ID);
  modem.connected = false;
  delay(100);

  return res;
}

// ============================================================================
// High-Level HTTP POST
// ============================================================================

ModemResult modemHttpPostJpeg(const char* host, uint16_t port, const char* path,
                              const uint8_t* jpegData, size_t jpegLen,
                              const char* extraHeaders) {
  ModemResult res;

  // Build HTTP request headers
  int headerLen;

  if (extraHeaders && strlen(extraHeaders) > 0) {
    headerLen = snprintf(headers, sizeof(headers),
                         "POST %s HTTP/1.1\r\n"
                         "Host: %s\r\n"
                         "User-Agent: ESP32-SIM7000E\r\n"
                         "Accept: */*\r\n"
                         "Content-Type: image/jpeg\r\n"
                         "%s\r\n"
                         "Content-Length: %u\r\n"
                         "Connection: close\r\n"
                         "\r\n",
                         path, host, extraHeaders, (unsigned int)jpegLen);
  } else {
    headerLen = snprintf(headers, sizeof(headers),
                         "POST %s HTTP/1.1\r\n"
                         "Host: %s\r\n"
                         "User-Agent: ESP32-SIM7000E\r\n"
                         "Accept: */*\r\n"
                         "Content-Type: image/jpeg\r\n"
                         "Content-Length: %u\r\n"
                         "Connection: close\r\n"
                         "\r\n",
                         path, host, (unsigned int)jpegLen);
  }

  Serial.printf("--[REQ]--\n%s\n--[EREQ]--\n", headers);

  // Open SSL connection
  res = modemSSLOpen(host, port);
  if (res != MODEM_OK) {
    Serial.printf("[MODEM] Failed to open SSL connection: %d\n", res);
    return res;
  }
  delay(50);

  // Send HTTP headers
  Serial.printf("[MODEM] Sending %d bytes of headers.. .\n", headerLen);
  res = modemSSLSendData((const uint8_t*)headers, headerLen);
  if (res != MODEM_OK) {
    Serial.printf("[MODEM] Failed to send headers: %d\n", res);
    modemSSLClose();
    return res;
  }

  // Send JPEG data
  Serial.printf("[MODEM] Sending %d bytes of JPEG data...\n", jpegLen);
  res = modemSSLSendData(jpegData, jpegLen);
  if (res != MODEM_OK) {
    Serial.printf("[MODEM] Failed to send JPEG data: %d\n", res);
    modemSSLClose();
    return res;
  }

  // Server should process and respond, then close connection (Connection:
  // close) Wait for response
  res = modemSSLReceiveData(MODEM_CONN_TMOUT);

  delay(100);

  Serial.println("--- Response ---");
  Serial.println(modem.responseBuffer);
  Serial.println("--- End Response ---");

  // Close connection (might already be closed)
  modemSSLClose();

  return modem.responseLen > 0 ? MODEM_OK : res;
}

// ============================================================================
// Utility Functions
// ============================================================================

void modemSleep() {
  // Put modem in minimum functionality mode
  modemSendAT("AT+CFUN=0", "OK", AT_TIMEOUT_MEDIUM);
}

void modemWake() {
  // Wake up to full functionality
  modemSendAT("AT+CFUN=1", "OK", AT_TIMEOUT_LONG);
  delay(3000);  // Give it time to re-register
}

int modemGetSignalQuality() {
  if (modemSendAT("AT+CSQ", "+CSQ:", AT_TIMEOUT_SHORT) == MODEM_OK) {
    char* p = strstr(modem.responseBuffer, "+CSQ:");
    if (p) {
      int rssi = atoi(p + 6);
      return rssi;  // 0-31, 99=unknown
    }
  }
  return -1;
}
