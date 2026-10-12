// SPDX-License-Identifier: MIT
#pragma once
#if defined(WPF_SUBSETTER_NATIVE)
#define WPF_SUBSETTER_API __declspec(dllexport)
#else
#define WPF_SUBSETTER_API __declspec(dllimport)
#endif
extern "C" {
WPF_SUBSETTER_API short WpfCreateSubset(const unsigned char* source, unsigned long sourceSize,
    unsigned char** destination, unsigned long* destinationSize, unsigned long* written,
    const unsigned short* glyphs, unsigned short glyphCount, unsigned long directoryOffset);
WPF_SUBSETTER_API void WpfFreeSubset(void* memory);
}
