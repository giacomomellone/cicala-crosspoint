#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

#define DRAM_ATTR
#define IRAM_ATTR
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 2
#define INPUT_PULLUP 3
#define CHANGE 4

namespace simulation {
inline unsigned long now = 0, assertion = 0, completion = 100;
inline int working = HIGH;
inline bool allocationFails = false;
}  // namespace simulation
inline unsigned long millis() { return simulation::now; }
inline void delay(unsigned long ms) { simulation::now += ms; }
inline void delayMicroseconds(unsigned long) { ++simulation::now; }
inline int digitalRead(int) {
  return simulation::now >= simulation::assertion && simulation::now < simulation::completion ? simulation::working
                                                                                              : !simulation::working;
}
inline void digitalWrite(int, int) {}
inline void pinMode(int, int) {}
inline int digitalPinToInterrupt(int pin) { return pin; }
inline void attachInterrupt(int, void (*)(), int) {}
inline void detachInterrupt(int) {}
inline struct SerialStub {
  explicit operator bool() const { return false; }
  template <typename... Args>
  void printf(const char*, Args...) {}
} Serial;
