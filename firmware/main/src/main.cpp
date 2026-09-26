#include <Arduino.h>

#include "app/app_controller.h"

namespace {
AppController appController;
}

void setup() { appController.begin(); }

void loop() { appController.tick(); }
