// SPDX-License-Identifier: MIT
// Build the actual upstream TtfDelta algorithm with the trusted MinGW toolchain.
#define WPF_SUBSETTER_NATIVE 1
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <strsafe.h>
#include <intsafe.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <mutex>
#include "SubsetterBridge.h"
#define ttBoolean int
// The legacy algorithms use the Win32 mixed-integer min/max macro semantics.
// libc++ headers remove those macros, so restore them after standard headers.
#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif
namespace MS { namespace Internal { namespace TtfDelta {
#define NO_CRT_ASSERT
#include "ttfdelta.cpp"
#include "ttmem.cpp"
#include "ttfcntrl.cpp"
#include "ttfacc.cpp"
#include "ttftabl1.cpp"
#include "ttftable.cpp"
#include "modcmap.cpp"
#include "modglyf.cpp"
#include "modsbit.cpp"
#include "modtable.cpp"
#include "makeglst.cpp"
#include "mtxcalc.cpp"
#include "automap.cpp"
#include "util.cpp"
}}}
extern "C" short WpfCreateSubset(const unsigned char* source, unsigned long sourceSize,
    unsigned char** destination, unsigned long* destinationSize, unsigned long* written,
    const unsigned short* glyphs, unsigned short glyphCount, unsigned long directoryOffset) {
    using namespace MS::Internal::TtfDelta;
    if (!destination || !destinationSize || !written) return ERR_GENERIC;
    *destination = nullptr; *destinationSize = 0; *written = 0;
    if (!source || !glyphs || !glyphCount) return ERR_GENERIC;
    try {
        GlobalInit::Init();
        ControlTableInit::Init();
    } catch (...) {
        // A failed native once-only initialization must not unwind into CLR code.
        return ERR_GENERIC;
    }
    return CreateDeltaTTF(source, sourceSize, destination, destinationSize, written,
        TTFDELTA_SUBSET, TTFSUB_LANG_KEEP_ALL, 0, 0, TTFDELTA_GLYPHLIST, glyphs, glyphCount,
        Mem_ReAlloc, Mem_Free, directoryOffset, nullptr);
}
extern "C" void WpfFreeSubset(void* memory) {
    MS::Internal::TtfDelta::Mem_Free(memory);
}
