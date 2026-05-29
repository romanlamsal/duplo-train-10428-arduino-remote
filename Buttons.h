#pragma once

enum class ButtonPress {
  None,
  Forward,
  Backward,
  Stop,
  Honk,
  Light,
};

void setupButtons();
ButtonPress handleButtons();

// Reads the throttle potentiometer on GPIO 34 (ADC1).
// Returns motor magnitude in [10..100], scaled linearly from the ADC.
int readThrottle();
