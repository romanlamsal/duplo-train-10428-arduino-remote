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

float readPotentiometer();
float readPotentiometerSampled(int sampleSize);
