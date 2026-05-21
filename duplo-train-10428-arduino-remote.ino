#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"
#include <NimBLEDevice.h>
#include "Train.h"
#include "Buttons.h"

static Train train;

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  delay(1500);
  Serial.println("boot");
  NimBLEDevice::init("esp32-duplo");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  setupButtons();
}

void loop() {
  if (!train.isConnected()) {
    train.disconnect();
    if (!train.connect()) { delay(1000); return; }
    Serial.println("subscribed. enter speed -100..100 then newline:");
  }

  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) {
      // ignore
    } else if (line.startsWith("raw ")) {
      uint8_t buf[20];
      size_t n = 0;
      int i = 4;
      while (i < (int)line.length() && n < sizeof(buf)) {
        while (i < (int)line.length() && line[i] == ' ') i++;
        if (i + 1 >= (int)line.length()) break;
        char hex[3] = { line[i], line[i+1], 0 };
        buf[n++] = (uint8_t)strtoul(hex, nullptr, 16);
        i += 2;
      }
      train.sendRaw(buf, n);
      Serial.printf(">> raw %u bytes\n", (unsigned)n);
    } else if (line.startsWith("pi ")) {
      // pi <port_hex>  → request mode info for that port (LWP3 0x21, infoType 0x01)
      uint8_t port = (uint8_t)strtoul(line.substring(3).c_str(), nullptr, 16);
      uint8_t pkt[] = {0x05, 0x00, 0x21, port, 0x01};
      train.sendRaw(pkt, sizeof(pkt));
      Serial.printf(">> port-info request port=0x%02x\n", port);
    } else if (line.startsWith("pm ")) {
      // pm <port_hex> <mode_dec>  → request NAME (0x00), RAW range (0x01), MAPPING (0x05),
      //                            CAPABILITY_BITS (0x08), VALUE_FORMAT (0x80)
      int sp = line.indexOf(' ', 3);
      if (sp > 3) {
        uint8_t port = (uint8_t)strtoul(line.substring(3, sp).c_str(), nullptr, 16);
        uint8_t mode = (uint8_t)line.substring(sp + 1).toInt();
        uint8_t infoTypes[] = {0x00, 0x01, 0x05, 0x08, 0x80};
        for (uint8_t t : infoTypes) {
          uint8_t pkt[] = {0x06, 0x00, 0x22, port, mode, t};
          train.sendRaw(pkt, sizeof(pkt));
        }
        Serial.printf(">> port-mode-info request port=0x%02x mode=%u\n", port, mode);
      } else {
        Serial.println("usage: pm <port_hex> <mode_dec>");
      }
    } else if (line == "honk") {
      train.honk();
      Serial.println(">> honk");
    } else if (line == "light") {
      train.cycleLights();
      Serial.println(">> light");
    } else {
      int v = line.toInt();
      train.setMotorSpeed(v);
      Serial.printf(">> motor %d\n", v);
    }
  }

  ButtonPress pressedButton = handleButtons();
  switch (pressedButton) {
    case ButtonPress::Forward:  train.setMotorSpeed( 45); break;
    case ButtonPress::Backward: train.setMotorSpeed(-45); break;
    case ButtonPress::Stop:     train.setMotorSpeed(  0); break;
    case ButtonPress::Honk:     train.honk();             break;
    case ButtonPress::Light:    train.cycleLights();      break;
    case ButtonPress::None: break;
  }
  if (pressedButton != ButtonPress::None) {
    delay(230);
  }

  delay(20);
}
