// SPDX-License-Identifier: MIT
// Surrounding fixture only; FontFileZeroOriginal.inc is extracted unchanged.
#include "NativePointerWrapper.cpp"
using namespace System;
using namespace System::Runtime::InteropServices;
extern "C" void* WpfCreateFontFace();
extern "C" int WpfAnalyzeFontFile(void* pointer);
namespace MS { namespace Internal { namespace Text { namespace TextInterface {
using namespace Generics;
void ThrowHresult(HRESULT hr) {
    if (hr < 0) Marshal::ThrowExceptionForHR(hr);
}
#define ConvertHresultToException(hr, msg) ThrowHresult(hr)
public ref class FontFile {
    NativeIUnknownWrapper<IDWriteFontFile>^ _fontFile;
public:
    FontFile(IDWriteFontFile* pointer) { _fontFile = gcnew NativeIUnknownWrapper<IDWriteFontFile>(pointer); }
    ~FontFile() { delete _fontFile; }
    CriticalHandle^ Handle() { return _fontFile; }
    int Analyze() {
        BOOL supported = FALSE;
        DWRITE_FONT_FILE_TYPE fileType;
        DWRITE_FONT_FACE_TYPE faceType;
        UINT32 faces = 0;
        HRESULT hr = _fontFile->Value->Analyze(&supported, &fileType, &faceType, &faces);
        ConvertHresultToException(hr, "FontFile probe Analyze");
        if (!supported || faces == 0 || WpfAnalyzeFontFile(_fontFile->Value) != static_cast<int>(faces)) return -1;
        GC::KeepAlive(_fontFile);
        return static_cast<int>(faces);
    }
};
public ref class FontFace {
    NativeIUnknownWrapper<IDWriteFontFace>^ _fontFace;
public:
    FontFace(IDWriteFontFace* pointer) { _fontFace = gcnew NativeIUnknownWrapper<IDWriteFontFace>(pointer); }
    ~FontFace() { delete _fontFace; }
    FontFile^ GetFileZero();
    CriticalHandle^ Handle() { return _fontFace; }
    static FontFace^ Create() {
        IDWriteFontFace* face = static_cast<IDWriteFontFace*>(WpfCreateFontFace());
        if (!face) throw gcnew InvalidOperationException("real DirectWrite font face unavailable");
        return gcnew FontFace(face);
    }
};
#include "FontFileZeroOriginal.inc"
}}}}
