#pragma once
#include <NimBLEDevice.h>

// LEGO palette indices accepted by the 10428 headlight.
// 0..10 follow the standard LWP3 colour table; 11 was observed in the
// official-app cycle but its actual hue hasn't been catalogued yet.
enum class TrainColor : uint8_t {
  Off       = 0,
  Pink      = 1,
  Magenta   = 2,
  Blue      = 3,
  LightBlue = 4,
  Cyan      = 5,
  Green     = 6,
  Yellow    = 7,
  Orange    = 8,
  Red       = 9,
  White     = 10,
  Color11   = 11,
};

class Train {
public:
  bool connect();
  void disconnect();
  bool isConnected() const;

  void setMotorSpeed(int speed);          // clamped to -100..100
  void honk();
  void changeLights(TrainColor color);
  void cycleLights();                     // advances through the LEGO-app palette
  void sendRaw(const uint8_t* data, size_t len);

  // Sign of last observed speedometer reading (-1, 0, +1). 0 until a
  // Port-Value-Single notification on PORT_SPEEDOMETER arrives.
  int observedDirection() const;

private:
  static const uint8_t PORT_MOTOR       = 0x32;  // confirmed
  static const uint8_t PORT_SPEEDOMETER = 0x36;  // confirmed (devType 0x2c)

  NimBLEClient*               client = nullptr;
  NimBLERemoteCharacteristic* chr    = nullptr;
};
