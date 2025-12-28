#include "modem_handler.h"

#include <Arduino.h>

#include "camera_handler.h"
#include "terminal_handler.h"

const char* X_DEVICE_ID = "2137420";

const char* server_url = "https://trop-mock.p4tkry.pl";
const char* upload_url = "/api/captured/image";

volatile modem_flags_t modemFlags;

Botletics_modem_LTE gsmModem = Botletics_modem_LTE();

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
  Serial1.begin(921600, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);

  gsmModem.powerOn(MODEM_PWR_PIN);
  if (!gsmModem.begin(Serial1)) {
    MODEM_RESTART("Init failed");
  }

  gsmModem.setFunctionality(1);
  gsmModem.setNetworkSettings(F("internet"), F("internet"), F("internet"));
  gsmModem.setHTTPSRedirect(true);
  gsmModem.enableRTC(true);

  uint16_t cnt = 0;
  while (!gsmModem.getNetworkStatus()) {
    delay(MODEM_TASK_DLY);
    ++cnt;
    if (cnt >= 150) {
      MODEM_RESTART("Network registration failed");
    }
  };

  Serial.println("Modem initialised");
  modemFlags.modem_initialised = 1;

  if (!gsmModem.openWirelessConnection(true)) {
    MODEM_RESTART("Internet connection failed");
  }
  modemFlags.modem_connected = 1;
  Serial.println("Modem connected");

  cnt = 0;
  while (true) {
    if (modemFlags.sending_in_progress) {
      // Upload the resulting file
      Serial.printf("Sending %d bytes of stuff\n", fb->len);
      bool res_code = false, res = false;

      gsmModem.HTTP_addHeader("Connection", "keep-alive", 10);
      gsmModem.HTTP_addHeader("Cache-control", "no-cache", 8);
      gsmModem.HTTP_addHeader("Content-Type", "image/jpeg", 10);
      gsmModem.HTTP_addHeader("X-Device-Id", X_DEVICE_ID, strlen(X_DEVICE_ID));
      res = gsmModem.HTTP_connect(server_url);

      Serial.printf("Server connection status: %d\n", res);
      if (res) {
        res_code = gsmModem.HTTP_POST(upload_url, (char*)fb->buf, 100);
      }
      if (!res_code && modemFlags.c_retries < MODEM_MAX_RETRIES) {
        Serial.println("HTTP upload failed");
        ++modemFlags.c_retries;
      } else if (res_code) {
        Serial.printf("Image sent to: %s\nReturn code: %i\n", upload_url,
                      res_code);
        modemFlags.c_retries = 0;
      }

      modemFlags.sending_in_progress = 0;
    } else {
      if (modemFlags.c_retries >= MODEM_MAX_RETRIES) {
        MODEM_RESTART("Max retries reached");
      }
      if (cnt % 79 == 0) {
        if (!gsmModem.getNetworkStatus()) {
          MODEM_RESTART("Disconnected from operator network");
        }
      }
      if (cnt % 179 == 0) {
        if (!gsmModem.wirelessConnStatus()) {
          MODEM_RESTART("Disconnected from internet");
        }
      }
      ++cnt;
    }
    delay(MODEM_TASK_DLY);
  }
}
