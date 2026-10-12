// SPDX-License-Identifier: MIT
#include <windows.h>
#include <dwrite.h>
#include <vcclr.h>
using namespace System;
extern "C" void* WpfDWriteCreate();
extern "C" int WpfDWriteFontCount(void*);
extern "C" int WpfDWriteFamilyName(void*, wchar_t*, int);
public ref class NativeComDirectWrite abstract sealed {
public:
    static int Verify() {
        IDWriteFactory* factory = static_cast<IDWriteFactory*>(WpfDWriteCreate());
        if (!factory) return 1;
        IDWriteFontCollection* fonts = nullptr;
        IDWriteFontFamily* family = nullptr;
        IDWriteLocalizedStrings* names = nullptr;
        try {
            if (factory->GetSystemFontCollection(&fonts, FALSE) != S_OK) return 2;
            unsigned int count = fonts->GetFontFamilyCount();
            if (count == 0 || count != static_cast<unsigned int>(WpfDWriteFontCount(factory))) return 3;
            if (fonts->GetFontFamily(0, &family) != S_OK || family->GetFamilyNames(&names) != S_OK) return 4;
            unsigned int index = 0, length = 0;
            BOOL exists = FALSE;
            String^ locale = gcnew String("en-us");
            pin_ptr<const wchar_t> pinnedLocale = PtrToStringChars(locale);
            if (names->FindLocaleName(pinnedLocale, &index, &exists) != S_OK) return 5;
            if (!exists) index = 0;
            if (names->GetStringLength(index, &length) != S_OK) return 6;
            array<Char>^ actual = gcnew array<Char>(length + 1);
            array<Char>^ expected = gcnew array<Char>(length + 1);
            pin_ptr<Char> actualBuffer = &actual[0];
            pin_ptr<Char> expectedBuffer = &expected[0];
            GC::Collect();
            if (names->GetString(index, actualBuffer, length + 1) != S_OK ||
                WpfDWriteFamilyName(factory, expectedBuffer, length + 1) != static_cast<int>(length + 1)) return 7;
            for (unsigned int i = 0; i <= length; ++i) if (actual[i] != expected[i]) return 8;
            Console::WriteLine(count);
        } finally {
            if (names) names->Release();
            if (family) family->Release();
            if (fonts) fonts->Release();
            factory->Release();
        }
        return 0;
    }
};
int main() {
    int result = NativeComDirectWrite::Verify();
    if (result) { Console::WriteLine(result); return 1; }
    Console::WriteLine("real DirectWrite COM dispatch, inherited slots, outputs and pinned Unicode names OK");
    return 0;
}
