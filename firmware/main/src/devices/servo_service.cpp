#include "devices/servo_service.h"

#include "devices/device_pins.h"

void ServoService::begin() {
  servo_.setPeriodHertz(50);
  servo_.attach(pins::kServoSignal, kMinPulseUs, kMaxPulseUs);
  close();
}

void ServoService::open() {
  servo_.write(kOpenDegrees);
  isOpen_ = true;
}

void ServoService::close() {
  servo_.write(kClosedDegrees);
  isOpen_ = false;
}
