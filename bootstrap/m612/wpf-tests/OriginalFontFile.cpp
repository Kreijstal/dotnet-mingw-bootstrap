// SPDX-License-Identifier: MIT
// Full original FontFile.cpp/header, with focused dependency context.
#using WINDOWS_BASE_DLL as_friend
#include "NativePointerWrapper.cpp"
#include "FontFile.cpp"
using namespace System;
using namespace MS::Internal::Text::TextInterface;
extern "C" void* WpfCreateFontFile();
extern "C" int WpfFontFilePath(void*,wchar_t*,UINT32);
extern "C" int WpfAnalyzeFontFile(void*);
public ref class OriginalFontFileCheck abstract sealed {
public:
    static String^ Verify() {
        IDWriteFontFile* pointer=static_cast<IDWriteFontFile*>(WpfCreateFontFile());
        if(!pointer) throw gcnew InvalidOperationException("real font file unavailable");
        FontFile^ file=gcnew FontFile(pointer);
        try {
            if(file->DWriteFontFileNoAddRef!=pointer) throw gcnew InvalidOperationException("original native pointer property disagrees");
            DWRITE_FONT_FILE_TYPE fileType;
            DWRITE_FONT_FACE_TYPE faceType;
            unsigned int faces=0;
            HRESULT hr=S_OK;
            if(!file->Analyze(fileType,faceType,faces,hr) || hr!=S_OK || faces==0 || WpfAnalyzeFontFile(pointer)!=static_cast<int>(faces))
                throw gcnew InvalidOperationException("original font Analyze disagrees with native oracle");
            String^ path=file->GetUriPath();
            if(path==nullptr || path->Length==0) throw gcnew InvalidOperationException("original font path missing");
            array<Char>^ expected=gcnew array<Char>(path->Length+1);
            pin_ptr<Char> buffer=&expected[0];
            GC::Collect();
            if(WpfFontFilePath(pointer,buffer,expected->Length)!=path->Length) throw gcnew InvalidOperationException("native font path length disagrees");
            for(int i=0;i<path->Length;++i) if(path->get_Chars(i)!=expected[i]) throw gcnew InvalidOperationException("native Unicode font path disagrees");
            return path;
        } finally { delete file; delete file; }
    }
};
