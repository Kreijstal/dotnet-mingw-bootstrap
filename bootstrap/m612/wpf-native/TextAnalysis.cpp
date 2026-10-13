// SPDX-License-Identifier: MIT
// Real DirectWrite analysis source/sink for WPF's original TextAnalyzer.Itemize.
#include <windows.h>
#include <dwrite.h>
#include <atomic>
#include <cstdint>
#include <map>
#include <new>
#include <string>
#include <vector>
#include <algorithm>

template<class T> struct AnalysisNode {
    // Keep the original TextItemizer.h order, including native padding.
    T Value;
    UINT32 Range[2];
    AnalysisNode* Next;
};
static_assert(offsetof(AnalysisNode<DWRITE_SCRIPT_ANALYSIS>,Range)==8,"WPF script-node ABI");
static_assert(offsetof(AnalysisNode<IDWriteNumberSubstitution*>,Range)==8,"WPF substitution-node ABI");
static_assert(offsetof(AnalysisNode<DWRITE_SCRIPT_ANALYSIS>,Next)==16,"WPF script-next ABI");
static_assert(offsetof(AnalysisNode<IDWriteNumberSubstitution*>,Next)==16,"WPF substitution-next ABI");

template<class T> void retain(T) {}
template<class T> void release(T) {}
template<> void retain(IDWriteNumberSubstitution* value) { if(value) value->AddRef(); }
template<> void release(IDWriteNumberSubstitution* value) { if(value) value->Release(); }

template<class T> class Ranges {
    using Map=std::map<UINT32,AnalysisNode<T>>;
    Map nodes;
    static void clear(Map& values) { for(auto& item:values) release(item.second.Value); values.clear(); }
    static void put(Map& values,UINT32 first,UINT32 last,T value) {
        auto item=values.emplace(first,AnalysisNode<T>{value,{first,last},nullptr});
        if(item.second) retain(value);
    }
public:
    ~Ranges() { clear(nodes); }
    HRESULT set(UINT32 first,UINT32 length,T value) {
        if(length>UINT32_MAX-first) return E_INVALIDARG;
        if(!length) return S_OK;
        UINT32 last=first+length;
        Map replacement;
        try {
            // Overlay callback ranges, preserving any uncovered prefix/suffix.
            for(const auto& item:nodes) {
                const auto& node=item.second;
                if(node.Range[1]<=first || node.Range[0]>=last)
                    put(replacement,node.Range[0],node.Range[1],node.Value);
                else {
                    if(node.Range[0]<first) put(replacement,node.Range[0],first,node.Value);
                    if(node.Range[1]>last) put(replacement,last,node.Range[1],node.Value);
                }
            }
            put(replacement,first,last,value);
            nodes.swap(replacement);
            clear(replacement);
            return S_OK;
        } catch(const std::bad_alloc&) { clear(replacement); return E_OUTOFMEMORY; }
    }
    AnalysisNode<T>* head() {
        AnalysisNode<T>* previous=nullptr;
        for(auto& item:nodes) {
            auto* current=&item.second;
            if(previous) previous->Next=current;
            current->Next=nullptr; previous=current;
        }
        return nodes.empty()?nullptr:&nodes.begin()->second;
    }
};

class AnalysisSink final:public IDWriteTextAnalysisSink {
    std::atomic<ULONG> refs{1};
    Ranges<DWRITE_SCRIPT_ANALYSIS> scripts;
    Ranges<IDWriteNumberSubstitution*> numbers;
    struct Bidi { UINT8 explicit_level,resolved_level; };
    Ranges<Bidi> bidi;
    std::map<UINT32,DWRITE_LINE_BREAKPOINT> breaks;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** output) override {
        if(!output) return E_POINTER; *output=nullptr;
        if(iid==__uuidof(IUnknown) || iid==__uuidof(IDWriteTextAnalysisSink)) {
            *output=static_cast<IDWriteTextAnalysisSink*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { auto count=--refs; if(!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE SetScriptAnalysis(UINT32 first,UINT32 length,const DWRITE_SCRIPT_ANALYSIS* value) override {
        if(!value) return E_POINTER; return scripts.set(first,length,*value);
    }
    HRESULT STDMETHODCALLTYPE SetLineBreakpoints(UINT32 first,UINT32 length,const DWRITE_LINE_BREAKPOINT* values) override {
        if(length && !values) return E_POINTER;
        if(length>UINT32_MAX-first) return E_INVALIDARG;
        try { for(UINT32 i=0;i<length;++i) breaks[first+i]=values[i]; return S_OK; }
        catch(const std::bad_alloc&) { return E_OUTOFMEMORY; }
    }
    HRESULT STDMETHODCALLTYPE SetBidiLevel(UINT32 first,UINT32 length,UINT8 explicit_level,UINT8 resolved_level) override {
        return bidi.set(first,length,{explicit_level,resolved_level});
    }
    HRESULT STDMETHODCALLTYPE SetNumberSubstitution(UINT32 first,UINT32 length,IDWriteNumberSubstitution* value) override {
        return numbers.set(first,length,value);
    }
    void* script_list() { return scripts.head(); }
    void* number_list() { return numbers.head(); }
};

class AnalysisSource final:public IDWriteTextAnalysisSource {
    std::atomic<ULONG> refs{1};
    const WCHAR* text;
    UINT32 length;
    std::wstring locale;
    DWRITE_READING_DIRECTION direction;
    IDWriteNumberSubstitution* substitution;
public:
    AnalysisSource(const WCHAR* input,UINT32 size,const WCHAR* culture,bool rtl,IDWriteNumberSubstitution* number):
        text(input),length(size),locale(culture),direction(rtl?DWRITE_READING_DIRECTION_RIGHT_TO_LEFT:DWRITE_READING_DIRECTION_LEFT_TO_RIGHT),substitution(number) {}
    ~AnalysisSource() { if(substitution) substitution->Release(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** output) override {
        if(!output) return E_POINTER; *output=nullptr;
        if(iid==__uuidof(IUnknown) || iid==__uuidof(IDWriteTextAnalysisSource)) {
            *output=static_cast<IDWriteTextAnalysisSource*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { auto count=--refs; if(!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE GetTextAtPosition(UINT32 position,const WCHAR** output,UINT32* count) override {
        if(!output || !count) return E_POINTER;
        *output=position<length?text+position:nullptr; *count=position<length?length-position:0; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTextBeforePosition(UINT32 position,const WCHAR** output,UINT32* count) override {
        if(!output || !count) return E_POINTER;
        *output=position && position<=length?text:nullptr; *count=position<=length?position:0; return S_OK;
    }
    DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetParagraphReadingDirection() override { return direction; }
    HRESULT STDMETHODCALLTYPE GetLocaleName(UINT32 position,UINT32* count,const WCHAR** output) override {
        if(!output || !count) return E_POINTER; *output=nullptr; *count=0;
        if(position>length) return E_INVALIDARG;
        *output=locale.c_str(); *count=length-position; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetNumberSubstitution(UINT32 position,UINT32* count,IDWriteNumberSubstitution** output) override {
        if(!output || !count) return E_POINTER; *output=nullptr; *count=0;
        if(position>length) return E_INVALIDARG;
        *count=length-position; *output=substitution; if(substitution) substitution->AddRef(); return S_OK;
    }
};

extern "C" __declspec(dllexport) void* WINAPI CreateTextAnalysisSink() {
    return new(std::nothrow) AnalysisSink;
}
extern "C" __declspec(dllexport) void* WINAPI GetScriptAnalysisList(void* input) {
    return input?static_cast<AnalysisSink*>(input)->script_list():nullptr;
}
extern "C" __declspec(dllexport) void* WINAPI GetNumberSubstitutionList(void* input) {
    return input?static_cast<AnalysisSink*>(input)->number_list():nullptr;
}
extern "C" __declspec(dllexport) HRESULT WINAPI CreateTextAnalysisSource(
    const WCHAR* text,UINT32 length,const WCHAR* culture,void* factory,BOOL rtl,
    const WCHAR* number_culture,BOOL ignore_override,UINT32 method,void** output) {
    if(!output) return E_POINTER; *output=nullptr;
    if((length && !text) || !culture || !factory || method>DWRITE_NUMBER_SUBSTITUTION_METHOD_TRADITIONAL) return E_INVALIDARG;
    IDWriteNumberSubstitution* number=nullptr;
    if(number_culture) {
        HRESULT hr=static_cast<IDWriteFactory*>(factory)->CreateNumberSubstitution(
            static_cast<DWRITE_NUMBER_SUBSTITUTION_METHOD>(method),number_culture,ignore_override,&number);
        if(FAILED(hr)) return hr;
    }
    try {
        *output=static_cast<IDWriteTextAnalysisSource*>(new AnalysisSource(text,length,culture,rtl!=FALSE,number));
        return S_OK;
    } catch(const std::bad_alloc&) { if(number) number->Release(); return E_OUTOFMEMORY; }
}
