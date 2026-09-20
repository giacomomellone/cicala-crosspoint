#pragma once

#include <algorithm>

struct CicalaLayout {
  struct Box {
    int x, y, width, height;
  };
  Box screen;

  bool portrait() const { return screen.height >= screen.width; }
  Box header() const { return {screen.x + 32, screen.y, screen.width - 64, portrait() ? 76 : 62}; }
  Box control(int index, int count) const {
    const int height = portrait() ? 80 : 66;
    const int usable = screen.width - 64 - 12 * (count - 1);
    const int left = index * usable / count;
    const int right = (index + 1) * usable / count;
    return {screen.x + 32 + left + index * 12, screen.y + screen.height - height - 22, right - left, height};
  }
  Box body() const {
    const int top = screen.y + (portrait() ? 108 : 86);
    return {screen.x + 32, top, screen.width - 64, control(0, 1).y - top - 30};
  }
  Box question() const {
    const auto area = body();
    const int offset = portrait() ? 37 : 3;
    return {area.x + 4, area.y + offset, area.width - 8, std::min(portrait() ? 458 : 248, area.height - offset)};
  }
  Box row(int index, bool filters) const {
    const auto area = body();
    const int columns = !portrait() && !filters ? 2 : 1;
    const int rows = filters ? 3 : (5 + columns - 1) / columns;
    const int offset = portrait() ? (filters ? 36 : 19) : 0;
    const int step =
        std::min(filters ? (portrait() ? 108 : 74) : (portrait() ? 81 : 66), (area.height - offset) / rows);
    const int width = (area.width - (columns - 1) * 12) / columns;
    return {area.x + (index % columns) * (width + 12), area.y + offset + (index / columns) * step, width,
            step - (filters ? 10 : 7)};
  }
};
