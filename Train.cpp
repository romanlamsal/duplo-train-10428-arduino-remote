#include "Train.h"
#include <Arduino.h>

static const NimBLEUUID SVC_LPF2("00001623-1212-efde-1623-785feabcd123");
static const NimBLEUUID CHR_LPF2("00001624-1212-efde-1623-785feabcd123");
static const uint8_t    MFG_DUPLO_OLD = 0x20;
static const uint8_t    MFG_DUPLO_NEW = 0x21;

static NimBLEAdvertisedDevice* gFound = nullptr;

// Most recently observed signed speedometer reading on port 0x36.
// Updated by onNotify, read by Train::observedSpeed().
static volatile int8_t gObservedSpeed = 0;

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

static void onNotify(NimBLERemoteCharacteristic* c, uint8_t* data, size_t len, bool isNotify) {
  Serial.print("notify:");
  for (size_t i = 0; i < len; i++) Serial.printf(" %02x", data[i]);
  Serial.println();
  // Port-Value-Single on speedometer (port 0x36): [05 00 45 36 <int8>].
  // Width confirmed by sniffing on the 10428 (see duplo-train-10428.md).
  if (len == 5 && data[2] == 0x45 && data[3] == 0x36) {
    gObservedSpeed = (int8_t)data[4];
  }
  // Attached-IO: [len, 0x00, 0x04, portId, 0x01=attached, devTypeLo, devTypeHi, ...]
  if (len >= 7 && data[2] == 0x04 && data[4] == 0x01) {
    uint16_t devType = (uint16_t)data[5] | ((uint16_t)data[6] << 8);
    Serial.printf("  attached port=0x%02x devType=0x%04x\n", data[3], devType);
  }
  // Port Information (reply to 0x21 mode-info request):
  // [len, 0x00, 0x43, port, infoType=0x01, capabilities, totalModes, inModesLE(2), outModesLE(2)]
  if (len >= 11 && data[2] == 0x43 && data[4] == 0x01) {
    uint16_t inMask  = (uint16_t)data[7] | ((uint16_t)data[8]  << 8);
    uint16_t outMask = (uint16_t)data[9] | ((uint16_t)data[10] << 8);
    Serial.printf("  port=0x%02x caps=0x%02x totalModes=%u inputModes=0x%04x outputModes=0x%04x\n",
                  data[3], data[5], data[6], inMask, outMask);
  }
  // Port Mode Information (reply to 0x22): [len, 0x00, 0x44, port, mode, modeInfoType, ...]
  if (len >= 6 && data[2] == 0x44) {
    uint8_t infoType = data[5];
    Serial.printf("  port=0x%02x mode=%u modeInfoType=0x%02x", data[3], data[4], infoType);
    if (infoType == 0x00) {
      Serial.print(" name=\"");
      for (size_t i = 6; i < len && data[i] != 0; i++) Serial.write(data[i]);
      Serial.print("\"");
    } else if (infoType == 0x01 && len >= 14) {
      // RAW: two IEEE-754 LE floats — min, max
      float lo, hi;
      memcpy(&lo, data + 6,  4);
      memcpy(&hi, data + 10, 4);
      Serial.printf(" raw min=%f max=%f", lo, hi);
    } else if (infoType == 0x05 && len >= 8) {
      // MAPPING: 2 bytes — input flags, output flags
      Serial.printf(" mappingIn=0x%02x mappingOut=0x%02x", data[6], data[7]);
    } else if (infoType == 0x08) {
      // CAPABILITY_BITS: up to 6 bytes
      Serial.print(" capBits=");
      for (size_t i = 6; i < len; i++) Serial.printf("%02x", data[i]);
    } else if (infoType == 0x80 && len >= 10) {
      // VALUE_FORMAT: numValues, datasetType (0x00=8b 0x01=16b 0x02=32b 0x03=float), figures, decimals
      Serial.printf(" numValues=%u datasetType=0x%02x figures=%u decimals=%u",
                    data[6], data[7], data[8], data[9]);
    }
    Serial.println();
  }
}

bool Train::connect() {
  gFound = nullptr;
  Serial.println("scanning...");
  auto scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(new ScanCB(), false);
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);
  scan->getResults(10 * 1000, false);

  if (!gFound) { Serial.println("not found, retry"); return false; }

  Serial.println("connecting...");
  client = NimBLEDevice::createClient();
  if (!client->connect(gFound)) {
    Serial.println("connect failed");
    NimBLEDevice::deleteClient(client);
    client = nullptr;
    return false;
  }
  Serial.println("connected");

  auto svc = client->getService(SVC_LPF2);
  if (!svc) {
    Serial.println("no service");
    client->disconnect();
    NimBLEDevice::deleteClient(client);
    client = nullptr;
    return false;
  }
  chr = svc->getCharacteristic(CHR_LPF2);
  if (!chr) {
    Serial.println("no char");
    client->disconnect();
    NimBLEDevice::deleteClient(client);
    client = nullptr;
    return false;
  }

  chr->subscribe(true, onNotify);

  // Subscribe to EVENTS notifications on port 0x34 mode 1.
  // Gates port-output writes to EVENTS and surfaces action-brick scan events.
  uint8_t enableEvents[] = {0x0a, 0x00, 0x41, 0x34, 0x01, 0x01, 0x00, 0x00, 0x00, 0x01};
  chr->writeValue(enableEvents, sizeof(enableEvents), false);

  // Subscribe to speedometer notifications on port 0x36 mode 0.
  // Sign of the reported value is our source of truth for the train's
  // current direction of travel (used by the pot throttle).
  gObservedSpeed = 0;
  uint8_t enableSpeed[] = {0x0a, 0x00, 0x41, 0x36, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01};
  chr->writeValue(enableSpeed, sizeof(enableSpeed), false);

  return true;
}

int Train::observedSpeed() const {
  return gObservedSpeed;
}

void Train::disconnect() {
  if (!client) return;
  client->disconnect();
  NimBLEDevice::deleteClient(client);
  client = nullptr;
  chr    = nullptr;
}

bool Train::isConnected() const {
  return client && client->isConnected();
}

void Train::setMotorSpeed(int v) {
  if (!isConnected()) return;
  if (v < -100) v = -100;
  if (v >  100) v =  100;
  uint8_t s = (uint8_t)(int8_t)v;
  uint8_t pkt[] = {0x08, 0x00, 0x81, PORT_MOTOR, 0x11, 0x51, 0x00, s};
  chr->writeValue(pkt, sizeof(pkt), false);
}

void Train::honk() {
  if (!isConnected()) return;
  // Port 0x34 EVENTS opcode 0x0107 (play sound), param 0 (horn).
  // Captured from the official LEGO DUPLO Interactive Trains app.
  // Side effect: train briefly flashes the headlight yellow.
  uint8_t pkt[] = {0x0b, 0x00, 0x81, 0x34, 0x11, 0x51, 0x01,
                   0x07, 0x01,
                   0x00, 0x00};
  chr->writeValue(pkt, sizeof(pkt), false);
}

void Train::changeLights(TrainColor color) {
  if (!isConnected()) return;
  // Port 0x34 EVENTS opcode 0x0104 (set light), param = colour index (u16 LE).
  uint8_t pkt[] = {0x0b, 0x00, 0x81, 0x34, 0x11, 0x51, 0x01,
                   0x04, 0x01,
                   static_cast<uint8_t>(color), 0x00};
  chr->writeValue(pkt, sizeof(pkt), false);
}

void Train::cycleLights() {
  // Palette + order observed in the official LEGO DUPLO Interactive Trains app's
  // "next colour" cycle. Skips colours not seen on the wire (e.g. 2/3/4/5/6).
  static const TrainColor PALETTE[] = {
    TrainColor::Color11,
    TrainColor::Orange,
    TrainColor::Yellow,
    TrainColor::Red,
    TrainColor::White,
    TrainColor::Off,
    TrainColor::Pink,
  };
  static size_t i = 0;
  changeLights(PALETTE[i]);
  i = (i + 1) % (sizeof(PALETTE) / sizeof(PALETTE[0]));
}

void Train::sendRaw(const uint8_t* data, size_t len) {
  if (!isConnected()) return;
  chr->writeValue(data, len, false);
}
