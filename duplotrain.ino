#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"
#include <NimBLEDevice.h>

static const NimBLEUUID SVC_LPF2("00001623-1212-efde-1623-785feabcd123");
static const NimBLEUUID CHR_LPF2("00001624-1212-efde-1623-785feabcd123");
static const uint8_t    MFG_DUPLO_OLD = 0x20;
static const uint8_t    MFG_DUPLO_NEW = 0x21;

// Pins
const int BTN_FWD = 12;
const int BTN_BWD = 13;
const int BTN_STP = 14;
const int BTN_HNK = 27;
const int BTN_LGT = 26;
const int LED_PIN = 2; 

class Train {
  void connect() {

  }

  bool isConnected() {

  }
};

void setupButtons() {
    // Set up pins with internal pull-ups
  pinMode(BTN_FWD, INPUT_PULLUP);
  pinMode(BTN_BWD, INPUT_PULLUP);
  pinMode(BTN_STP, INPUT_PULLUP);
  pinMode(BTN_HNK, INPUT_PULLUP);
  pinMode(BTN_LGT, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
}

void handleButtons() {
}

static NimBLEAdvertisedDevice* gFound = nullptr;

class ScanCB : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* d) override {
    if (!d->isAdvertisingService(SVC_LPF2)) return;
    std::string m = d->getManufacturerData();
    if (m.size() <= 3) return;
    uint8_t t = (uint8_t)m[3];
    if (t != MFG_DUPLO_OLD && t != MFG_DUPLO_NEW) return;
    Serial.printf("found train: %s rssi=%d hubtype=0x%02x\n",
                  d->getAddress().toString().c_str(), d->getRSSI(), t);
    gFound = (NimBLEAdvertisedDevice*)d;
    NimBLEDevice::getScan()->stop();
  }
};

void onNotify(NimBLERemoteCharacteristic* c, uint8_t* data, size_t len, bool isNotify) {
  Serial.print("notify:");
  for (size_t i = 0; i < len; i++) Serial.printf(" %02x", data[i]);
  Serial.println();
}

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
  gFound = nullptr;
  Serial.println("scanning...");
  auto scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(new ScanCB(), false);
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);
  scan->getResults(10 * 1000, false);

  if (!gFound) { Serial.println("not found, retry"); return; }

  Serial.println("connecting...");
  auto client = NimBLEDevice::createClient();
  if (!client->connect(gFound)) {
    Serial.println("connect failed");
    NimBLEDevice::deleteClient(client);
    delay(1000);
    return;
  }
  Serial.println("connected");

  auto svc = client->getService(SVC_LPF2);
  if (!svc) { Serial.println("no service"); client->disconnect(); return; }
  auto chr = svc->getCharacteristic(CHR_LPF2);
  if (!chr) { Serial.println("no char"); client->disconnect(); return; }

  chr->subscribe(true, onNotify);
  Serial.println("subscribed. enter speed -100..100 then newline:");

  while (client->isConnected()) {
    if (Serial.available()) {
      int v = Serial.parseInt();
      while (Serial.available()) Serial.read(); // drain newline/junk
      if (v < -100) v = -100;
      if (v >  100) v =  100;
      uint8_t s = (uint8_t)(int8_t)v;
      uint8_t pkt[] = {0x08, 0x00, 0x81, 0x32, 0x11, 0x51, 0x00, s};
      chr->writeValue(pkt, sizeof(pkt), false);
      Serial.printf(">> motor %d\n", v);
    }

    // Button Controls
    // FORWARD
    if (digitalRead(BTN_FWD) == LOW) {
      uint8_t s = (uint8_t)(int8_t)45;
      uint8_t pkt[] = {0x08, 0x00, 0x81, 0x32, 0x11, 0x51, 0x00, s};
      chr->writeValue(pkt, sizeof(pkt), false);
      delay(200);
    }

    // BACKWARD
    if (digitalRead(BTN_BWD) == LOW) {
      uint8_t s = (uint8_t)(int8_t)-45;
      uint8_t pkt[] = {0x08, 0x00, 0x81, 0x32, 0x11, 0x51, 0x00, s};
      chr->writeValue(pkt, sizeof(pkt), false);
      delay(200);
    }

    // STOP
    if (digitalRead(BTN_STP) == LOW) {
      uint8_t s = (uint8_t)(int8_t)0;
      uint8_t pkt[] = {0x08, 0x00, 0x81, 0x32, 0x11, 0x51, 0x00, s};
      chr->writeValue(pkt, sizeof(pkt), false);
      delay(200);
    }


    delay(20);
  }
  Serial.println("disconnected");
  NimBLEDevice::deleteClient(client);
}
