// SPDX-License-Identifier: MIT
// Source-built split bridge for checking the frontend against real DirectWrite.
// This is an incremental probe, not WPF's DirectWriteForwarder replacement.
#include <windows.h>
#include <dwrite.h>

extern "C" __declspec(dllexport) void* WpfDWriteCreate() {
    IDWriteFactory* factory = nullptr;
    HRESULT result = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                        reinterpret_cast<IUnknown**>(&factory));
    return SUCCEEDED(result) ? factory : nullptr;
}
extern "C" __declspec(dllexport) void WpfDWriteRelease(void* handle) {
    if (handle) static_cast<IDWriteFactory*>(handle)->Release();
}
extern "C" __declspec(dllexport) int WpfDWriteFontCount(void* handle) {
    if (!handle) return -1;
    IDWriteFontCollection* fonts = nullptr;
    HRESULT result = static_cast<IDWriteFactory*>(handle)->GetSystemFontCollection(&fonts);
    if (FAILED(result)) return -1;
    int count = static_cast<int>(fonts->GetFontFamilyCount());
    fonts->Release();
    return count;
}
extern "C" __declspec(dllexport) int WpfDWriteFamilyName(void* handle, wchar_t* buffer, int capacity) {
    if (!handle || !buffer || capacity <= 0) return -1;
    IDWriteFontCollection* fonts = nullptr;
    IDWriteFontFamily* family = nullptr;
    IDWriteLocalizedStrings* names = nullptr;
    HRESULT result = static_cast<IDWriteFactory*>(handle)->GetSystemFontCollection(&fonts);
    if (SUCCEEDED(result)) result = fonts->GetFontFamily(0, &family);
    if (SUCCEEDED(result)) result = family->GetFamilyNames(&names);
    UINT32 index = 0, length = 0;
    BOOL exists = FALSE;
    if (SUCCEEDED(result)) result = names->FindLocaleName(L"en-us", &index, &exists);
    if (!exists) index = 0;
    if (SUCCEEDED(result)) result = names->GetStringLength(index, &length);
    int answer = -1;
    if (SUCCEEDED(result)) {
        answer = static_cast<int>(length + 1);
        if (capacity >= answer && FAILED(names->GetString(index, buffer, static_cast<UINT32>(capacity)))) answer = -1;
    }
    if (names) names->Release();
    if (family) family->Release();
    if (fonts) fonts->Release();
    return answer; // Required capacity, including the terminator; -1 on failure.
}
