#include <gtest/gtest.h>

#include "src/cicala/CicalaLayout.h"

namespace {
void inside(CicalaLayout::Box inner, CicalaLayout::Box outer) {
  EXPECT_GT(inner.width, 0);
  EXPECT_GT(inner.height, 0);
  EXPECT_GE(inner.x, outer.x);
  EXPECT_GE(inner.y, outer.y);
  EXPECT_LE(inner.x + inner.width, outer.x + outer.width);
  EXPECT_LE(inner.y + inner.height, outer.y + outer.height);
}
}  // namespace

TEST(CicalaLayout, ControlsAndContentStayWithinOrientedViewableArea) {
  for (const auto screen :
       {CicalaLayout::Box{0, 0, 480, 800}, {0, 0, 800, 480}, {12, 8, 456, 772}, {16, 12, 768, 448}, {0, 0, 320, 480}}) {
    CicalaLayout layout{screen};
    inside(layout.header(), screen);
    inside(layout.body(), screen);
    inside(layout.question(), layout.body());
    EXPECT_LE(layout.header().y + layout.header().height, layout.body().y);
    for (int count = 1; count <= 3; ++count) {
      for (int index = 0; index < count; ++index) {
        const auto button = layout.control(index, count);
        inside(button, screen);
        EXPECT_LE(layout.body().y + layout.body().height, button.y);
        EXPECT_GE(button.height, 66);
        if (index)
          EXPECT_GE(button.x - (layout.control(index - 1, count).x + layout.control(index - 1, count).width), 12);
      }
    }
    for (const bool filters : {false, true})
      for (int index = 0; index < (filters ? 3 : 5); ++index) inside(layout.row(index, filters), layout.body());
  }
}

TEST(CicalaLayout, AdjacentRowsNeverShareATouchArea) {
  for (const auto screen : {CicalaLayout::Box{0, 0, 480, 800}, {0, 0, 800, 480}}) {
    CicalaLayout layout{screen};
    for (const bool filters : {false, true}) {
      const int count = filters ? 3 : 5;
      for (int i = 0; i < count; ++i) {
        for (int j = i + 1; j < count; ++j) {
          const auto a = layout.row(i, filters), b = layout.row(j, filters);
          EXPECT_TRUE(a.x + a.width <= b.x || b.x + b.width <= a.x || a.y + a.height <= b.y || b.y + b.height <= a.y);
        }
      }
    }
  }
}
