// SPDX-License-Identifier: MIT
#include <windows.h>
#include <dwrite.h>

extern "C" __declspec(dllexport) void* WpfCreateFontFace() {
    IDWriteFactory* factory = nullptr;
    IDWriteFontCollection* collection = nullptr;
    IDWriteFontFamily* family = nullptr;
    IDWriteFont* font = nullptr;
    IDWriteFontFace* face = nullptr;
    HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                    reinterpret_cast<IUnknown**>(&factory));
    if (SUCCEEDED(hr)) hr = factory->GetSystemFontCollection(&collection);
    if (SUCCEEDED(hr)) hr = collection->GetFontFamily(0, &family);
    if (SUCCEEDED(hr)) hr = family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL,
                            DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font);
    if (SUCCEEDED(hr)) hr = font->CreateFontFace(&face);
    if (font) font->Release();
    if (family) family->Release();
    if (collection) collection->Release();
    if (factory) factory->Release();
    return SUCCEEDED(hr) ? face : nullptr;
}

extern "C" __declspec(dllexport) int WpfAnalyzeFontFile(void* pointer) {
    BOOL supported = FALSE;
    DWRITE_FONT_FILE_TYPE fileType;
    DWRITE_FONT_FACE_TYPE faceType;
    UINT32 faces = 0;
    HRESULT hr = static_cast<IDWriteFontFile*>(pointer)->Analyze(&supported, &fileType, &faceType, &faces);
    return SUCCEEDED(hr) && supported && faces > 0 ? static_cast<int>(faces) : -1;
}
