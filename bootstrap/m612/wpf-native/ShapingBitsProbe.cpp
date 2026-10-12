// SPDX-License-Identifier: MIT
#include "FontFileProbe.cpp"

class ScriptText final : public IDWriteTextAnalysisSource, public IDWriteTextAnalysisSink {
    LONG refs=1;
public:
    DWRITE_SCRIPT_ANALYSIS Script{};
    bool SawScript=false;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** output) override {
        if(!output) return E_POINTER;
        *output=nullptr;
        if(iid==__uuidof(IUnknown) || iid==__uuidof(IDWriteTextAnalysisSource)) *output=static_cast<IDWriteTextAnalysisSource*>(this);
        else if(iid==__uuidof(IDWriteTextAnalysisSink)) *output=static_cast<IDWriteTextAnalysisSink*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG count=InterlockedDecrement(&refs); if(!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE GetTextAtPosition(UINT32 position,const WCHAR** text,UINT32* length) override {
        *text=position<6 ? &L"office"[position] : nullptr; *length=position<6 ? 6-position : 0; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTextBeforePosition(UINT32 position,const WCHAR** text,UINT32* length) override {
        *text=position && position<=6 ? L"office" : nullptr; *length=position<=6 ? position : 0; return S_OK;
    }
    DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetParagraphReadingDirection() override { return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT; }
    HRESULT STDMETHODCALLTYPE GetLocaleName(UINT32 position,UINT32* length,const WCHAR** locale) override {
        *length=position<6 ? 6-position : 0; *locale=L"en-us"; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetNumberSubstitution(UINT32 position,UINT32* length,IDWriteNumberSubstitution** substitution) override {
        *length=position<6 ? 6-position : 0; *substitution=nullptr; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetScriptAnalysis(UINT32 position,UINT32 length,const DWRITE_SCRIPT_ANALYSIS* script) override {
        if(position!=0 || length!=6) return E_INVALIDARG;
        Script=*script; SawScript=true; return S_OK;
    }
    // This probe invokes AnalyzeScript only, so the other sink callbacks must
    // not be reached. No line-break/bidi/number-substitution result is supplied.
    HRESULT STDMETHODCALLTYPE SetLineBreakpoints(UINT32,UINT32,const DWRITE_LINE_BREAKPOINT*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetBidiLevel(UINT32,UINT32,UINT8,UINT8) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetNumberSubstitution(UINT32,UINT32,IDWriteNumberSubstitution*) override { return E_NOTIMPL; }
};

extern "C" __declspec(dllexport) int WpfNativeShapingFill(DWRITE_SHAPING_TEXT_PROPERTIES* properties,UINT32 count,UINT32* glyphCount) {
    if(count!=6) return E_INVALIDARG;
    IDWriteFactory* factory=nullptr;
    IDWriteTextAnalyzer* analyzer=nullptr;
    IDWriteFontFace* face=static_cast<IDWriteFontFace*>(WpfCreateFontFace());
    if(!face) return E_FAIL;
    ScriptText* source=new ScriptText();
    HRESULT hr=DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(&factory));
    if(SUCCEEDED(hr)) hr=factory->CreateTextAnalyzer(&analyzer);
    if(SUCCEEDED(hr)) hr=analyzer->AnalyzeScript(source,0,6,source);
    if(SUCCEEDED(hr) && !source->SawScript) hr=E_FAIL;
    UINT16 clusters[6]{},glyphs[32]{};
    DWRITE_SHAPING_GLYPH_PROPERTIES glyphProperties[32]{};
    if(SUCCEEDED(hr)) hr=analyzer->GetGlyphs(L"office",6,face,FALSE,FALSE,&source->Script,L"en-us",nullptr,
                          nullptr,nullptr,0,32,clusters,properties,glyphs,glyphProperties,glyphCount);
    source->Release();
    if(analyzer) analyzer->Release();
    if(factory) factory->Release();
    face->Release();
    return hr;
}
extern "C" __declspec(dllexport) int WpfNativeShapedAlone(const DWRITE_SHAPING_TEXT_PROPERTIES* properties,UINT32 index) {
    return properties[index].isShapedAlone ? 1 : 0;
}
