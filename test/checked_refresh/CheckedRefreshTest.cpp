#include <gtest/gtest.h>

#include "EpdBus.h"

extern "C" void freeink_board_epd_power(bool) {}
extern "C" void freeink_board_epd_reset(bool) {}

namespace {
freeink::EpdBus bus;
unsigned begins = 0, ends = 0;
void beginHook() { ++begins; }
void endHook() { ++ends; }
bool slice(int8_t, uint8_t) {
  delay(1);
  return true;
}
void setup(bool polling, unsigned long assertion, unsigned long completion) {
  simulation::now = 0;
  simulation::assertion = assertion;
  simulation::completion = completion;
  simulation::working = HIGH;
  begins = ends = 0;
  bus.begin({1, 2, 3, 4, 5, 6}, 40000000, freeink::BusyPolarity::ActiveHigh);
  simulation::now = 0;
  bus.setBusyWaitHooks(beginHook, endHook);
  bus.setBusyWaitSliceHook(polling ? slice : nullptr);
  bus.clearWaitFailure();
}
}  // namespace

TEST(CheckedRefresh, MissingSemaphoreStillChecksBusyAssertion) {
  simulation::allocationFails = true;
  setup(false, 100000, 100001);
  bus.waitRefreshComplete();
  EXPECT_TRUE(bus.waitFailed());
  simulation::allocationFails = false;
}

TEST(CheckedRefresh, BusyCompletionAndDelayedAssertion) {
  for (bool polling : {false, true}) {
    for (unsigned long assertion : {0ul, 5ul}) {
      setup(polling, assertion, 100);
      bus.waitRefreshComplete();
      EXPECT_FALSE(bus.waitFailed());
      EXPECT_GE(simulation::now, 100u);
      EXPECT_EQ(begins, ends);
    }
  }
}

TEST(CheckedRefresh, StuckAndMissingBusyFailInBothWaitPaths) {
  for (bool polling : {false, true}) {
    setup(polling, 0, 100000);
    bus.waitRefreshComplete();
    EXPECT_TRUE(bus.waitFailed());
    EXPECT_GE(simulation::now, 30000u);
    EXPECT_EQ(begins, ends);
    setup(polling, 100000, 100001);
    bus.waitRefreshComplete();
    EXPECT_TRUE(bus.waitFailed());
  }
}

TEST(CheckedRefresh, FailureStaysLatchedUntilNextOperation) {
  setup(true, 0, 100000);
  bus.waitBusy();
  EXPECT_TRUE(bus.waitFailed());
  simulation::completion = 0;
  bus.waitBusy();
  EXPECT_TRUE(bus.waitFailed());
  bus.endCheckedWait();
  bus.clearWaitFailure();
  bus.waitBusy();
  EXPECT_FALSE(bus.waitFailed());
}

TEST(CheckedRefresh, ProUltraChipUsesIdleHighAfterOneTick) {
  for (bool polling : {false, true}) {
    for (unsigned long completion : {0ul, 100ul}) {
      setup(polling, 0, completion);
      bus.begin({1, 2, 3, 4, 5, 6}, 40000000, freeink::BusyPolarity::UcIdleHigh);
      simulation::now = 0;
      simulation::working = LOW;
      bus.waitRefreshComplete();
      EXPECT_FALSE(bus.waitFailed());
      EXPECT_GE(simulation::now, std::max(1ul, completion));
      EXPECT_EQ(begins, ends);
    }
  }
}

TEST(CheckedRefresh, ProUltraChipStuckBusyRejectsTheRefresh) {
  setup(true, 0, 100000);
  bus.begin({1, 2, 3, 4, 5, 6}, 40000000, freeink::BusyPolarity::UcIdleHigh);
  simulation::now = 0;
  simulation::working = LOW;
  bus.waitRefreshComplete();
  EXPECT_TRUE(bus.waitFailed());
  EXPECT_GT(simulation::now, 30000u);
  EXPECT_LT(simulation::now, 30100u);
  EXPECT_EQ(begins, ends);
}

TEST(CheckedRefresh, ProUncheckedWaitRetainsTheSdkCompletionContract) {
  setup(true, 0, 31000);
  bus.begin({1, 2, 3, 4, 5, 6}, 40000000, freeink::BusyPolarity::UcIdleHigh);
  simulation::now = 0;
  simulation::working = LOW;
  bus.endCheckedWait();
  bus.waitRefreshComplete();
  EXPECT_FALSE(bus.waitFailed());
  EXPECT_EQ(simulation::now, 31000u);
  EXPECT_EQ(begins, ends);
}
