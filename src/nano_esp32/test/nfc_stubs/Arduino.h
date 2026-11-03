#pragma once
#include "../console_stubs/Arduino.h"
#include <cstring>
constexpr int LOW = 0, HIGH = 1, INPUT = 0;
extern int testPins[64];
inline int digitalRead(int pin) { return testPins[pin]; }
inline void digitalWrite(int pin, int value) { testPins[pin] = value; }
inline void pinMode(int, int) {}
inline void delayMicroseconds(unsigned) {}
