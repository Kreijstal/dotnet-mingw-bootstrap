// SPDX-License-Identifier: MIT
#include <windows.h>
#include <dwrite.h>
using FactoryFunction = HRESULT (WINAPI*)(DWRITE_FACTORY_TYPE, REFIID, IUnknown**);
extern "C" HMODULE WpfLoadDwrite(void**);
extern "C" BOOL WpfUnloadDwrite(HMODULE);
int main() {
    void* address = nullptr;
    HMODULE module = WpfLoadDwrite(&address);
    if (!module || !address) return 1;
    IUnknown* unknown = nullptr;
    IDWriteFontCollection* fonts = nullptr;
    try {
        FactoryFunction factoryFunction = reinterpret_cast<FactoryFunction>(address);
        HRESULT result = (*factoryFunction)(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &unknown);
        if (result != S_OK || !unknown) return 2;
        IDWriteFactory* factory = static_cast<IDWriteFactory*>(unknown);
        if (factory->GetSystemFontCollection(&fonts, FALSE) != S_OK) return 3;
        if (fonts->GetFontFamilyCount() == 0) return 4;
        System::GC::Collect();
        // The pointer type spelling resolves to the same immutable SDK GUID.
        IUnknown* queried = nullptr;
        if (factory->QueryInterface(__uuidof(IDWriteFactory*), reinterpret_cast<void**>(&queried)) != S_OK) return 5;
        queried->Release();
        System::Console::WriteLine("real loaded DWriteCreateFactory pointer, immutable SDK UUID references and COM calls OK");
    } finally {
        if (fonts) fonts->Release();
        if (unknown) unknown->Release();
        WpfUnloadDwrite(module);
    }
    return 0;
}
