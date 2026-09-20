#pragma once
#ifdef CICALA_ENABLED
#include <GfxRenderer.h>

#include "CicalaLogoBits.h"

namespace cicala_logo {
inline void draw(const GfxRenderer& renderer, int x, int y, int size, bool black = true) {
  const uint8_t* bits = size == 32 ? mark32 : mark36;
  const int stride = (size + 7) / 8;
  for (int row = 0; row < size; ++row)
    for (int column = 0; column < size; ++column)
      if (!(bits[row * stride + column / 8] & (0x80 >> (column % 8)))) renderer.drawPixel(x + column, y + row, black);
}
}  // namespace cicala_logo
#endif
