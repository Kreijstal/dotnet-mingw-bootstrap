// SPDX-License-Identifier: MIT
#include <windows.h>
#include <dwrite.h>
#include <vector>

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

extern "C" __declspec(dllexport) int WpfFontEmbeddingRights(void* pointer, unsigned short* rights) {
    auto* face=static_cast<IDWriteFontFace*>(pointer);
    const void* table=nullptr;
    void* context=nullptr;
    UINT32 size=0;
    BOOL exists=FALSE;
    HRESULT hr=face->TryGetFontTable(DWRITE_MAKE_OPENTYPE_TAG('O','S','/','2'), &table, &size, &context, &exists);
    if(FAILED(hr)) return -1;
    *rights=0;
    if(!exists) return 0;
    bool valid=size>=10;
    if(valid) {
        auto* bytes=static_cast<const unsigned char*>(table);
        *rights=static_cast<unsigned short>((bytes[8]<<8)|bytes[9]);
    }
    face->ReleaseFontTable(context);
    return valid ? 1 : 0;
}

extern "C" __declspec(dllexport) int WpfFontGlyphs(void* pointer, const UINT32* codes, UINT32 count, UINT16* glyphs) {
    return static_cast<IDWriteFontFace*>(pointer)->GetGlyphIndices(codes,count,glyphs);
}

extern "C" __declspec(dllexport) void* WpfCreateFontFile() {
    auto* face=static_cast<IDWriteFontFace*>(WpfCreateFontFace());
    if(!face) return nullptr;
    UINT32 count=0;
    HRESULT hr=face->GetFiles(&count,nullptr);
    std::vector<IDWriteFontFile*> files(count,nullptr);
    if(SUCCEEDED(hr) && count) hr=face->GetFiles(&count,files.data());
    IDWriteFontFile* first=SUCCEEDED(hr) && count ? files[0] : nullptr;
    for(size_t i=first ? 1 : 0;i<files.size();++i) if(files[i]) files[i]->Release();
    face->Release();
    return first;
}

extern "C" __declspec(dllexport) int WpfFontFilePath(void* pointer, wchar_t* buffer, UINT32 capacity) {
    auto* file=static_cast<IDWriteFontFile*>(pointer);
    IDWriteFontFileLoader* loader=nullptr;
    IDWriteLocalFontFileLoader* local=nullptr;
    const void* key=nullptr;
    UINT32 keySize=0, length=0;
    HRESULT hr=file->GetLoader(&loader);
    if(SUCCEEDED(hr)) hr=loader->QueryInterface(__uuidof(IDWriteLocalFontFileLoader),reinterpret_cast<void**>(&local));
    if(SUCCEEDED(hr)) hr=file->GetReferenceKey(&key,&keySize);
    if(SUCCEEDED(hr)) hr=local->GetFilePathLengthFromKey(key,keySize,&length);
    if(SUCCEEDED(hr)) hr=local->GetFilePathFromKey(key,keySize,buffer,capacity);
    if(local) local->Release();
    if(loader) loader->Release();
    return SUCCEEDED(hr) ? static_cast<int>(length) : -1;
}
