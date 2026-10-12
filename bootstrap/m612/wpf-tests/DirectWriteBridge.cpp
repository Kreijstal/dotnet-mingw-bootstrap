// SPDX-License-Identifier: MIT
#include <vcclr.h>
using namespace System;
extern "C" void* WpfDWriteCreate();
extern "C" void WpfDWriteRelease(void*);
extern "C" int WpfDWriteFontCount(void*);
extern "C" int WpfDWriteFamilyName(void*, wchar_t*, int);
public ref class SourceDirectWriteBridge abstract sealed {
public:
    static int FontCount() {
        void* factory = WpfDWriteCreate();
        try { return WpfDWriteFontCount(factory); }
        finally { WpfDWriteRelease(factory); }
    }
    static int FamilyName(array<Char>^ buffer) {
        pin_ptr<Char> name = &buffer[0];
        void* factory = WpfDWriteCreate();
        try { return WpfDWriteFamilyName(factory, name, buffer->Length); }
        finally { WpfDWriteRelease(factory); }
    }
};
