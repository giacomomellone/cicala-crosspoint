#pragma once
#include <cstdint>

class CicalaInputGate {
 public:
  uint32_t update(bool busy, uint32_t down, uint32_t released) {
    if (busy) blocked |= down;
    const uint32_t accepted = busy ? 0 : released & ~blocked;
    blocked &= down;
    return accepted;
  }

 private:
  uint32_t blocked = 0;
};
