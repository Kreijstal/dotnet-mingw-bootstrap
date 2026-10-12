// SPDX-License-Identifier: MIT
// Surrounding fixture only; FontFileZeroOriginal.inc is extracted unchanged.
#include "NativePointerWrapper.cpp"
#include "OpenTypeTableTag.h"
using namespace System;
using namespace System::Runtime::InteropServices;
extern "C" void* WpfCreateFontFace();
extern "C" int WpfAnalyzeFontFile(void* pointer);
extern "C" int WpfFontEmbeddingRights(void* pointer, unsigned short* rights);
extern "C" int WpfFontGlyphs(void* pointer, const UINT32* codes, UINT32 count, UINT16* glyphs);
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
    bool TryGetFontTable(OpenTypeTableTag tag, [Out] array<byte>^% data);
    void GetArrayOfGlyphIndices(const UINT32* codes, UINT32 count, UINT16* glyphs);
public:
    FontFace(IDWriteFontFace* pointer) { _fontFace = gcnew NativeIUnknownWrapper<IDWriteFontFace>(pointer); }
    ~FontFace() { delete _fontFace; }
    FontFile^ GetFileZero();
    bool ReadFontEmbeddingRights([Out] unsigned short% fsType);
    int VerifyTablesAndGlyphs() {
        unsigned short actualRights=0, expectedRights=0;
        bool actualFound=ReadFontEmbeddingRights(actualRights);
        int expectedFound=WpfFontEmbeddingRights(_fontFace->Value,&expectedRights);
        if(expectedFound<0 || actualFound!=(expectedFound!=0) || actualRights!=expectedRights) return 1;
        array<byte>^ table;
        bool found=TryGetFontTable(OpenTypeTableTag::OS_2,table);
        if(!actualFound || !found || table==nullptr || table->Length<10) return 2;
        unsigned short tableRights=static_cast<unsigned short>((static_cast<unsigned int>(table[8])<<8)|table[9]);
        if(tableRights!=actualRights) return 3;
        if(TryGetFontTable(static_cast<OpenTypeTableTag>(DWRITE_MAKE_OPENTYPE_TAG('!','!','!','!')),table) || table!=nullptr) return 4;
        array<UInt32>^ codes=gcnew array<UInt32>(3);
        array<UInt16>^ actual=gcnew array<UInt16>(3);
        array<UInt16>^ expected=gcnew array<UInt16>(3);
        codes[0]=65; codes[1]=0x03a9; codes[2]=0x1f600;
        pin_ptr<UInt32> codeBuffer=&codes[0];
        pin_ptr<UInt16> actualBuffer=&actual[0];
        pin_ptr<UInt16> expectedBuffer=&expected[0];
        GC::Collect();
        GetArrayOfGlyphIndices(codeBuffer,3,actualBuffer);
        if(WpfFontGlyphs(_fontFace->Value,codeBuffer,3,expectedBuffer)!=S_OK) return 5;
        for(int i=0;i<3;++i) if(actual[i]!=expected[i]) return 6;
        GC::KeepAlive(_fontFace);
        return 0;
    }
    CriticalHandle^ Handle() { return _fontFace; }
    static FontFace^ Create() {
        IDWriteFontFace* face = static_cast<IDWriteFontFace*>(WpfCreateFontFace());
        if (!face) throw gcnew InvalidOperationException("real DirectWrite font face unavailable");
        return gcnew FontFace(face);
    }
};
#include "FontFileZeroOriginal.inc"
#include "FontFaceTablesOriginal.inc"
}}}}
