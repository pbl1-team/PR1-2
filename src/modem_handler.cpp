#include "modem_handler.h"

#include <Arduino.h>
#include <ArduinoHttpClient.h>
#include <TinyGsmClient.h>

#include "camera_handler.h"
#include "terminal_handler.h"

const char* upload_url = "/api/captured/image";

static const char* upload_server = "trop-mock.p4tkry.pl";
static const char* modem_apn = "internet";
static const char* modem_user = "internet";
static const char* modem_passwd = "internet";

volatile modem_flags_t modemFlags;

TinyGsm gsmModem(Serial1);
TinyGsmClientSecure gsmClient(gsmModem);
HttpClient htClient(gsmClient, upload_server, 443);

#define MODEM_RESTART(msg)                   \
  {                                          \
    termFlags.term_stalled = 1;              \
    Serial.print(msg);                       \
    Serial.println(", modem restarting..."); \
    termFlags.term_stalled = 0;              \
    goto vModemHandler_start;                \
  }

StaticTask_t xModemHandlerBuf;
TaskHandle_t xModemHandler;
StackType_t xModemHandlerStack[MODEM_STACK_SIZE];

void vModemHandler(void* params) {
vModemHandler_start:
  modemFlags.modem_initialised = 0;
  modemFlags.modem_connected = 0;
  modemFlags.sending_in_progress = 0;
  modemFlags.c_retries = 0;
  htClient.stop();
  pinMode(MODEM_PWR_PIN, OUTPUT);
  digitalWrite(MODEM_PWR_PIN, LOW);
  Serial1.begin(921600, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);

  delay(500);
  digitalWrite(MODEM_PWR_PIN, HIGH);
  delay(4000);

  if (!gsmModem.init()) {
    MODEM_RESTART("Init failed");
  }
  modemFlags.modem_initialised = 1;
  if (!gsmModem.waitForNetwork(15000L)) {
    MODEM_RESTART("Failed to connect to operator network");
  }
  if (!gsmModem.gprsConnect(modem_apn, modem_user, modem_passwd)) {
    MODEM_RESTART("Failed to connect to internet");
  }
  modemFlags.modem_connected = 1;
  termFlags.term_stalled = 1;
  Serial.println("Modem connected");
  termFlags.term_stalled = 0;

  uint16_t cnt = 0;
  while (true) {
    if (modemFlags.sending_in_progress) {
      // Upload the resulting file
      Serial.printf("Sending %d bytes of stuff\n", fb->len);

      htClient.connectionKeepAlive();
      htClient.setTimeout(120000);
      htClient.setHttpResponseTimeout(110000);
      htClient.setHttpWaitForDataDelay(5);
      htClient.beginRequest();

      int res_code;
      int res = htClient.post(upload_url);
      Serial.printf("Initial code: %d\n", res);

      if (res == 0) {
        htClient.sendHeader(HTTP_HEADER_CONTENT_TYPE, "image/jpeg");
        htClient.sendHeader(HTTP_HEADER_CONTENT_LENGTH, fb->len);
        htClient.sendHeader("X-Device-Id", X_DEVICE_ID);
        htClient.endRequest();
        htClient.write(fb->buf, fb->len);

        res_code = htClient.responseStatusCode();
        htClient.skipResponseHeaders();
      }
      htClient.stop();

      Serial.printf("Image sent to: %s\nReturn code: %i\n", upload_url,
                    res_code);

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
        if (!gsmModem.testAT()) {
          MODEM_RESTART("Went away");
        }
      }
      if (cnt % 79 == 0) {
        if (!gsmModem.isNetworkConnected()) {
          MODEM_RESTART("Disconnected from operator network");
        }
      }
      if (cnt % 179 == 0) {
        if (!gsmModem.isGprsConnected()) {
          MODEM_RESTART("Disconnected from internet");
        }
      }
      ++cnt;
    }
    delay(20);
  }
}
