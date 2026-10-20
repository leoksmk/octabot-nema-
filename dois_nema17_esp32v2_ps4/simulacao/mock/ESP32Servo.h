#pragma once
#include "Arduino.h"
struct ESP32PWM { static void allocateTimer(int) {} };
class Servo {
 public:
  int pin = -1, ang = -1, writes = 0;
  void setPeriodHertz(int) {}
  void attach(int p, int, int) { pin = p; }
  void write(int a) { ang = a; writes++; }
};
