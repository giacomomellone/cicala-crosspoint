#pragma once
#include "Arduino.h"
#define MSBFIRST 1
#define SPI_MODE0 0
struct SPISettings {
  SPISettings(uint32_t = 0, int = 0, int = 0) {}
};
inline struct SPIStub {
  void begin(int, int, int, int) {}
  void beginTransaction(SPISettings) {}
  void endTransaction() {}
  void transfer(uint8_t) {}
  void writeBytes(const uint8_t*, size_t) {}
} SPI;
