// SPDX-License-Identifier: MIT
#include <dwrite.h>
#include <cstddef>
extern "C" __declspec(dllexport) int WpfGlyphMetricsOffset(int index) {
    const int offsets[] = {
        sizeof(DWRITE_GLYPH_METRICS),
        offsetof(DWRITE_GLYPH_METRICS, leftSideBearing),
        offsetof(DWRITE_GLYPH_METRICS, advanceWidth),
        offsetof(DWRITE_GLYPH_METRICS, rightSideBearing),
        offsetof(DWRITE_GLYPH_METRICS, topSideBearing),
        offsetof(DWRITE_GLYPH_METRICS, advanceHeight),
        offsetof(DWRITE_GLYPH_METRICS, bottomSideBearing),
        offsetof(DWRITE_GLYPH_METRICS, verticalOriginY)
    };
    return index >= 0 && index < 8 ? offsets[index] : -1;
}
extern "C" __declspec(dllexport) int WpfGlyphMetricsRead(const DWRITE_GLYPH_METRICS* value) {
    return value && value->leftSideBearing == -3 && value->advanceWidth == 1200 &&
        value->rightSideBearing == -5 && value->topSideBearing == 17 &&
        value->advanceHeight == 2048 && value->bottomSideBearing == -9 && value->verticalOriginY == 1550;
}
