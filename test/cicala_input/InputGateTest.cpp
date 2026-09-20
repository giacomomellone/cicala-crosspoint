#include <gtest/gtest.h>

#include "src/cicala/CicalaInputGate.h"

TEST(CicalaInput, RefreshPressesCannotAdvanceOnTheirLaterRelease) {
  CicalaInputGate gate;
  EXPECT_EQ(gate.update(false, 0, 1), 1u);
  EXPECT_EQ(gate.update(true, 2, 0), 0u);
  EXPECT_EQ(gate.update(false, 2, 0), 0u);
  EXPECT_EQ(gate.update(false, 0, 2), 0u);
  EXPECT_EQ(gate.update(false, 2, 0), 0u);
  EXPECT_EQ(gate.update(false, 0, 2), 2u);
  EXPECT_EQ(gate.update(true, 0, 4), 0u);
}

TEST(CicalaInput, HeldTouchCannotActivateARowAfterRefresh) {
  constexpr uint32_t touch = 1u << 31;
  CicalaInputGate gate;
  EXPECT_EQ(gate.update(true, touch, 0), 0u);
  EXPECT_EQ(gate.update(false, touch, 0), 0u);
  EXPECT_EQ(gate.update(false, 0, touch), 0u);
  EXPECT_EQ(gate.update(false, touch, 0), 0u);
  EXPECT_EQ(gate.update(false, 0, touch), touch);
}
