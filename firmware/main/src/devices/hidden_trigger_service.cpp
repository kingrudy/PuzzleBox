#include "devices/hidden_trigger_service.h"

#include <Arduino.h>

#include "devices/device_pins.h"

void HiddenTriggerService::begin() {
  pinMode(pins::kHiddenSensor1, INPUT);          // external pull-up required
  pinMode(pins::kHiddenSensor2, INPUT_PULLUP);
}

void HiddenTriggerService::poll() {
  sensor1Active_ = digitalRead(pins::kHiddenSensor1) == LOW;
  sensor2Active_ = digitalRead(pins::kHiddenSensor2) == LOW;
}
