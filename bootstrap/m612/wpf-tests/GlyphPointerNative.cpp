// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include <dwrite.h>
#include <cstdint>
extern "C" __declspec(dllexport) int NativeGlyphs(DWRITE_GLYPH_METRICS* values, unsigned count) {
    if (reinterpret_cast<std::uintptr_t>(values) % alignof(DWRITE_GLYPH_METRICS)) return 0;
    for (unsigned i = 0; i < count; ++i) {
        values[i].leftSideBearing = -17 - static_cast<int>(i);
        values[i].advanceWidth = 70000 + i;
        values[i].rightSideBearing = 19 + i;
        values[i].topSideBearing = -23 - static_cast<int>(i);
        values[i].advanceHeight = 80000 + i;
        values[i].bottomSideBearing = -29 - static_cast<int>(i);
        values[i].verticalOriginY = 31 + i;
    }
    return sizeof(DWRITE_GLYPH_METRICS) == 28 ? 42 : 0;
}
