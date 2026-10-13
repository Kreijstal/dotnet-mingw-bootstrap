// SPDX-License-Identifier: MIT
#include <windows.h>
#include <dwrite.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>
template<class T> struct Node { T value; UINT32 range[2]; Node* next; };
using MakeSink=void* (WINAPI *)();
using GetList=void* (WINAPI *)(void*);
using MakeSource=HRESULT (WINAPI *)(const WCHAR*,UINT32,const WCHAR*,void*,BOOL,const WCHAR*,BOOL,UINT32,void**);
static void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }

class Recorder:public IDWriteTextAnalysisSink {
    std::atomic<ULONG> refs{1};
    IDWriteTextAnalysisSink* target;
public:
    struct Script { UINT32 first,length; DWRITE_SCRIPT_ANALYSIS value; };
    struct Number { UINT32 first,length; IDWriteNumberSubstitution* value; };
    std::vector<Script> scripts;
    std::vector<Number> numbers;
    unsigned bidi=0,breaks=0;
    explicit Recorder(IDWriteTextAnalysisSink* input):target(input) { target->AddRef(); }
    ~Recorder() { target->Release(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** output) override {
        if(!output) return E_POINTER; *output=nullptr;
        if(iid==__uuidof(IUnknown)||iid==__uuidof(IDWriteTextAnalysisSink)) { *output=this; AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { auto n=--refs; if(!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE SetScriptAnalysis(UINT32 first,UINT32 length,const DWRITE_SCRIPT_ANALYSIS* value) override {
        scripts.push_back({first,length,*value}); return target->SetScriptAnalysis(first,length,value);
    }
    HRESULT STDMETHODCALLTYPE SetNumberSubstitution(UINT32 first,UINT32 length,IDWriteNumberSubstitution* value) override {
        numbers.push_back({first,length,value}); return target->SetNumberSubstitution(first,length,value);
    }
    HRESULT STDMETHODCALLTYPE SetBidiLevel(UINT32 first,UINT32 length,UINT8 a,UINT8 b) override {
        ++bidi; return target->SetBidiLevel(first,length,a,b);
    }
    HRESULT STDMETHODCALLTYPE SetLineBreakpoints(UINT32 first,UINT32 length,const DWRITE_LINE_BREAKPOINT* points) override {
        breaks+=length; return target->SetLineBreakpoints(first,length,points);
    }
};
class CountedNumber:public IDWriteNumberSubstitution {
public:
    ULONG refs=1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** output) override {
        if(!output) return E_POINTER; *output=nullptr;
        if(iid==__uuidof(IUnknown)||iid==__uuidof(IDWriteNumberSubstitution)) { *output=this; AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs; }
};
int main(int argc,char** argv) {
    try {
        require(argc==2,"usage: TextAnalysisNative DLL");
        auto dll=LoadLibraryA(argv[1]); require(dll!=nullptr,"native DLL load");
        auto make_sink=reinterpret_cast<MakeSink>(GetProcAddress(dll,"CreateTextAnalysisSink"));
        auto make_source=reinterpret_cast<MakeSource>(GetProcAddress(dll,"CreateTextAnalysisSource"));
        auto script_list=reinterpret_cast<GetList>(GetProcAddress(dll,"GetScriptAnalysisList"));
        auto number_list=reinterpret_cast<GetList>(GetProcAddress(dll,"GetNumberSubstitutionList"));
        require(make_sink&&make_source&&script_list&&number_list,"native analysis exports");
        IDWriteFactory* factory=nullptr;
        require(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(&factory))),"native DirectWrite factory");
        void* invalid=reinterpret_cast<void*>(1);
        require(make_source(nullptr,1,L"en-US",factory,FALSE,nullptr,FALSE,0,&invalid)==E_INVALIDARG&&!invalid,"null text rejected and output cleared");
        require(make_source(L"",0,L"en-US",factory,FALSE,nullptr,FALSE,0,nullptr)==E_POINTER,"null output rejected");
        require(make_source(L"",0,L"en-US",factory,FALSE,nullptr,FALSE,99,&invalid)==E_INVALIDARG&&!invalid,"invalid substitution method rejected");
        std::wstring text=L"Latin \u0627\u0644\u0639\u0631\u0628\u064A\u0629 123 \u05D0\u05D1 \u0915\u093F \U0001F600";
        WCHAR locale[]=L"en-US";
        IDWriteTextAnalysisSource* source=nullptr;
        require(make_source(text.data(),static_cast<UINT32>(text.size()),locale,factory,TRUE,L"ar-SA",TRUE,DWRITE_NUMBER_SUBSTITUTION_METHOD_NATIONAL,reinterpret_cast<void**>(&source))==S_OK,"analysis source creation");
        locale[0]=L'x'; // Locale copied; the text intentionally remains borrowed.
        const WCHAR* result=nullptr; UINT32 size=0;
        require(source->GetTextAtPosition(2,&result,&size)==S_OK&&result==text.data()+2&&size==text.size()-2,"borrowed text after position");
        require(source->GetTextBeforePosition(2,&result,&size)==S_OK&&result==text.data()&&size==2,"borrowed text before position");
        require(source->GetTextAtPosition(static_cast<UINT32>(text.size()),&result,&size)==S_OK&&!result&&!size,"end of text");
        require(source->GetLocaleName(0,&size,&result)==S_OK&&!wcscmp(result,L"en-US"),"owned locale lifetime");
        require(source->GetParagraphReadingDirection()==DWRITE_READING_DIRECTION_RIGHT_TO_LEFT,"paragraph reading direction");
        IUnknown* identity=nullptr;
        require(source->QueryInterface(__uuidof(IUnknown),reinterpret_cast<void**>(&identity))==S_OK&&identity,"source COM identity"); identity->Release();
        require(source->GetLocaleName(static_cast<UINT32>(text.size()+1),&size,&result)==E_INVALIDARG&&!size&&!result,"locale position bounds");
        IDWriteTextAnalysisSink* sink=static_cast<IDWriteTextAnalysisSink*>(make_sink()); require(sink!=nullptr,"analysis sink creation");
        auto recorder=new Recorder(sink);
        IDWriteTextAnalyzer* analyzer=nullptr; require(factory->CreateTextAnalyzer(&analyzer)==S_OK,"native text analyzer");
        UINT32 length=static_cast<UINT32>(text.size());
        require(analyzer->AnalyzeScript(source,0,length,recorder)==S_OK,"real script analysis");
        require(analyzer->AnalyzeNumberSubstitution(source,0,length,recorder)==S_OK,"real number analysis");
        require(analyzer->AnalyzeBidi(source,0,length,recorder)==S_OK&&recorder->bidi>0,"real bidi callback");
        require(analyzer->AnalyzeLineBreakpoints(source,0,length,recorder)==S_OK&&recorder->breaks==length,"real line-break callback");
        auto scripts=static_cast<Node<DWRITE_SCRIPT_ANALYSIS>*>(script_list(sink));
        UINT32 covered=0; unsigned count=0;
        for(auto* node=scripts;node;node=node->next) {
            require(node->range[0]==covered&&node->range[1]>covered&&node->range[1]<=length,"sorted script-node coverage");
            bool match=false;
            for(auto& entry:recorder->scripts) if(entry.first==node->range[0]&&entry.first+entry.length==node->range[1])
                match=node->value.script==entry.value.script&&node->value.shapes==entry.value.shapes;
            require(match,"script values against independent callback record"); covered=node->range[1]; ++count;
        }
        require(covered==length&&count>=4,"mixed-script coverage");
        unsigned substituted=0;
        for(auto* node=static_cast<Node<IDWriteNumberSubstitution*>*>(number_list(sink));node;node=node->next) {
            bool match=false;
            for(auto& entry:recorder->numbers) if(entry.first==node->range[0]&&entry.first+entry.length==node->range[1]) match=entry.value==node->value;
            require(match,"number values against independent callback record");
            if(node->value) ++substituted;
        }
        require(substituted>0,"actual number-substitution object retained");
        source->Release(); // Sink owns the substitution object independently.
        recorder->Release(); analyzer->Release(); sink->Release();

        // Overlapping ranges must preserve original values and COM ownership.
        sink=static_cast<IDWriteTextAnalysisSink*>(make_sink());
        require(!script_list(sink)&&!number_list(sink),"empty sink lists");
        CountedNumber a,b;
        require(sink->SetNumberSubstitution(0,10,&a)==S_OK&&a.refs==2,"number AddRef");
        require(sink->SetNumberSubstitution(3,4,&b)==S_OK&&a.refs==3&&b.refs==2,"overlap ownership");
        auto* head=static_cast<Node<IDWriteNumberSubstitution*>*>(number_list(sink));
        require(head&&head->range[0]==0&&head->range[1]==3&&head->value==&a,"overlap prefix");
        head=head->next; require(head&&head->range[0]==3&&head->range[1]==7&&head->value==&b,"overlap replacement");
        head=head->next; require(head&&head->range[0]==7&&head->range[1]==10&&head->value==&a&&!head->next,"overlap suffix");
        require(sink->SetNumberSubstitution(UINT32_MAX,2,&a)==E_INVALIDARG,"range overflow rejection");
        sink->Release(); require(a.refs==1&&b.refs==1,"number Release on sink destruction");
        factory->Release();
        std::printf("real DirectWrite source/sink: %u mixed-script ranges, %u substituted ranges; native ABI/callbacks/overlaps/COM lifetime PASS\n",count,substituted);
        return 0;
    } catch(const std::exception& error) { std::printf("FAIL: %s\n",error.what()); return 1; }
}
