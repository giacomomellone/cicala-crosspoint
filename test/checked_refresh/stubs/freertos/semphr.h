#pragma once
#include <initializer_list>

#include "Arduino.h"
#include "FreeRTOS.h"
using SemaphoreHandle_t = void*;
inline SemaphoreHandle_t xSemaphoreCreateBinary() {
  return simulation::allocationFails ? nullptr : reinterpret_cast<void*>(1);
}
inline void xSemaphoreGiveFromISR(SemaphoreHandle_t, BaseType_t*) {}
inline int xSemaphoreTake(SemaphoreHandle_t, unsigned long ticks) {
  using namespace simulation;
  for (const auto edge : {assertion, completion}) {
    if (edge > now && edge <= now + ticks) {
      now = edge;
      return pdTRUE;
    }
  }
  now += ticks;
  return pdFALSE;
}
