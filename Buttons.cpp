#include "Buttons.h"
#include <Arduino.h>

static const int BTN_FWD = 12;
static const int BTN_BWD = 13;
static const int BTN_STP = 14;
static const int BTN_HNK = 27;
static const int BTN_LGT = 26;
static const int LED_PIN =  2;

void setupButtons() {
  pinMode(BTN_FWD, INPUT_PULLUP);
  pinMode(BTN_BWD, INPUT_PULLUP);
  pinMode(BTN_STP, INPUT_PULLUP);
  pinMode(BTN_HNK, INPUT_PULLUP);
  pinMode(BTN_LGT, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
}

ButtonPress handleButtons() {
  if (digitalRead(BTN_STP) == LOW) { delay(200); return ButtonPress::Stop;     }
  if (digitalRead(BTN_FWD) == LOW) { delay(200); return ButtonPress::Forward;  }
  if (digitalRead(BTN_BWD) == LOW) { delay(200); return ButtonPress::Backward; }
  if (digitalRead(BTN_HNK) == LOW) { delay(200); return ButtonPress::Honk;     }
  if (digitalRead(BTN_LGT) == LOW) { delay(200); return ButtonPress::Light;    }
  return ButtonPress::None;
}
