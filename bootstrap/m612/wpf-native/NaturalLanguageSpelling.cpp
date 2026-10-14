// SPDX-License-Identifier: MIT
// Original WPF NLG COM ABI, with ICU source offsets and Windows spell checking.
#include "NaturalLanguageNative.h"
#include <initguid.h>
#include <spellcheck.h>
#include <unicode/ubrk.h>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include <climits>
#include <cstring>

namespace {
template<class T> struct Release { void operator()(T* p) const { if(p)p->Release(); } };
template<class T> using Com = std::unique_ptr<T,Release<T>>;
template<class T> Com<T> retain(T* p) { if(p)p->AddRef();return Com<T>(p); }
template<class F> HRESULT protect(F f) noexcept {
    try { return f(); } catch(const std::bad_alloc&) { return E_OUTOFMEMORY; } catch(...) { return E_FAIL; }
}
template<class T,const GUID* Id> class Object : public T {
    std::atomic<ULONG> references{1};
protected:
    virtual ~Object() = default;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** output) override {
        if(!output)return E_POINTER;*output=nullptr;
        if(iid!=IID_IUnknown && iid!=*Id)return E_NOINTERFACE;
        *output=static_cast<T*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG left=--references;if(!left)delete this;return left; }
};
std::wstring lower(const std::wstring& word) {
    if(word.empty())return {};
    int size=LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,word.data(),int(word.size()),nullptr,0,nullptr,nullptr,0);
    if(!size)return word;
    std::wstring result(size,L'\0');
    LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,word.data(),int(word.size()),result.data(),size,nullptr,nullptr,0);
    return result;
}
HRESULT factory(Com<ISpellCheckerFactory>& value) {
    ISpellCheckerFactory* raw=nullptr;
    HRESULT result=CoCreateInstance(CLSID_SpellCheckerFactory,nullptr,CLSCTX_INPROC_SERVER,IID_ISpellCheckerFactory,reinterpret_cast<void**>(&raw));
    value.reset(raw);return result;
}
HRESULT checker(LONG locale,Com<ISpellChecker>& value,std::string& icu) {
    WCHAR name[LOCALE_NAME_MAX_LENGTH]{};
    if(!LCIDToLocaleName(locale?locale:GetUserDefaultLCID(),name,LOCALE_NAME_MAX_LENGTH,LOCALE_ALLOW_NEUTRAL_NAMES))return E_INVALIDARG;
    for(const WCHAR* p=name;*p;++p)icu.push_back(char(*p));
    Com<ISpellCheckerFactory> provider;HRESULT result=factory(provider);if(FAILED(result))return result;
    BOOL supported=FALSE;result=provider->IsSupported(name,&supported);if(FAILED(result))return result;if(!supported)return E_INVALIDARG;
    ISpellChecker* raw=nullptr;result=provider->CreateSpellChecker(name,&raw);value.reset(raw);return result;
}
struct Options : Object<NlIProcessingOptions,&NL_IID_IProcessingOptions> {
    bool spelling=false,verify=true,multiword=false;
    HRESULT STDMETHODCALLTYPE put_Item(VARIANT key,VARIANT value) override {return protect([&]() -> HRESULT {
        if(key.vt!=VT_BSTR||!key.bstrVal)return E_INVALIDARG;
        std::wstring name(key.bstrVal,SysStringLen(key.bstrVal));
        if(name==L"IsSpellChecking"||name==L"IsSpellVerifyOnly"||name==L"IsSpellSuggestingMWEs") {
            if(value.vt!=VT_BOOL)return E_INVALIDARG;
            bool enabled=value.boolVal!=VARIANT_FALSE;
            if(name==L"IsSpellChecking")spelling=enabled;
            else if(name==L"IsSpellVerifyOnly")verify=enabled;
            else multiword=enabled;
            return S_OK;
        }
        // The OS provider does not expose the legacy pre/post reform selector.
        return E_NOTIMPL;
    });}
};
struct Lexicon : Object<NlILexicon,&NL_IID_ILexicon> {
    std::set<std::wstring> words;LONG locale=0;
    HRESULT STDMETHODCALLTYPE ReadFrom(BSTR filename) override { return protect([&]() -> HRESULT {
        if(!filename)return E_POINTER;
        HANDLE file=CreateFileW(filename,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return HRESULT_FROM_WIN32(GetLastError());
        struct Close {HANDLE file;~Close(){CloseHandle(file);}} close{file};
        LARGE_INTEGER size{};if(!GetFileSizeEx(file,&size))return HRESULT_FROM_WIN32(GetLastError());
        if(size.QuadPart<0||size.QuadPart>INT_MAX)return E_INVALIDARG;
        std::string bytes(size_t(size.QuadPart),'\0');DWORD got=0;
        if(!ReadFile(file,bytes.data(),DWORD(bytes.size()),&got,nullptr))return HRESULT_FROM_WIN32(GetLastError());
        if(got!=bytes.size())return HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
        std::wstring text;
        if(bytes.size()>=2&&uint8_t(bytes[0])==0xff&&uint8_t(bytes[1])==0xfe) {
            if(bytes.size()%2)return E_INVALIDARG;
            text.resize((bytes.size()-2)/2);if(!text.empty())std::memcpy(text.data(),bytes.data()+2,bytes.size()-2);
        } else if(bytes.size()>=2&&uint8_t(bytes[0])==0xfe&&uint8_t(bytes[1])==0xff) {
            if(bytes.size()%2)return E_INVALIDARG;
            for(size_t i=2;i<bytes.size();i+=2)text.push_back(WCHAR((uint8_t(bytes[i])<<8)|uint8_t(bytes[i+1])));
        } else {
            size_t start=bytes.size()>=3&&bytes.substr(0,3)=="\xef\xbb\xbf"?3:0;
            if(bytes.size()>start){int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data()+start,int(bytes.size()-start),nullptr,0);
                if(!count)return E_INVALIDARG;text.resize(count);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data()+start,int(bytes.size()-start),text.data(),count);}
        }
        std::set<std::wstring> next;LONG nextLocale=0;
        for(size_t start=0;start<text.size();) {
            size_t end=text.find_first_of(L"\r\n",start);if(end==std::wstring::npos)end=text.size();
            auto word=text.substr(start,end-start);auto first=word.find_first_not_of(L" \t");auto last=word.find_last_not_of(L" \t");
            if(first!=std::wstring::npos){word=word.substr(first,last-first+1);
                if(word.rfind(L"#LID ",0)==0||word.rfind(L"#LID\t",0)==0){
                    WCHAR* tail=nullptr;unsigned long parsed=wcstoul(word.c_str()+5,&tail,10);
                    WCHAR tag[LOCALE_NAME_MAX_LENGTH]{};
                    if(!tail||*tail||parsed>LONG_MAX||!LCIDToLocaleName(LCID(parsed),tag,LOCALE_NAME_MAX_LENGTH,LOCALE_ALLOW_NEUTRAL_NAMES))return E_INVALIDARG;
                    nextLocale=LONG(parsed);
                }else if(word.front()!=L'#')next.insert(lower(word));}
            start=end+1;
        }
        words.swap(next);locale=nextLocale;return S_OK;
    }); }
};
struct Context : Object<NlITextContext,&NL_IID_ITextContext> {
    Com<Options> options{new Options};std::vector<Com<Lexicon>> lexicons;
    bool accepts(const std::wstring& word,LONG locale) const {auto key=lower(word);for(const auto& lex:lexicons)if((!lex->locale||lex->locale==locale)&&lex->words.count(key))return true;return false;}
    HRESULT STDMETHODCALLTYPE get_LexiconCount(LONG* output) override {if(!output)return E_POINTER;*output=LONG(lexicons.size());return S_OK;}
    HRESULT STDMETHODCALLTYPE get_Lexicon(LONG index,NlILexicon** output) override {
        if(!output)return E_POINTER;*output=nullptr;if(index<0||size_t(index)>=lexicons.size())return E_INVALIDARG;
        *output=lexicons[index].get();(*output)->AddRef();return S_OK;
    }
    HRESULT STDMETHODCALLTYPE AddLexicon(NlILexicon* value) override {return protect([&]() -> HRESULT {
        auto lexicon=dynamic_cast<Lexicon*>(value);if(!lexicon)return E_INVALIDARG;
        if(std::none_of(lexicons.begin(),lexicons.end(),[&](const auto& p){return p.get()==lexicon;}))lexicons.push_back(retain(lexicon));return S_OK;
    });}
    HRESULT STDMETHODCALLTYPE RemoveLexicon(NlILexicon* value) override {
        auto lexicon=dynamic_cast<Lexicon*>(value);if(!lexicon)return E_INVALIDARG;
        auto at=std::find_if(lexicons.begin(),lexicons.end(),[&](const auto& p){return p.get()==lexicon;});
        if(at==lexicons.end())return E_INVALIDARG;lexicons.erase(at);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Options(NlIProcessingOptions** output) override {
        if(!output)return E_POINTER;*output=options.get();(*output)->AddRef();return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Capabilities(LONG locale,NlIProcessingOptions** output) override {
        if(!output)return E_POINTER;*output=nullptr;Com<ISpellChecker> value;std::string name;
        HRESULT result=protect([&]{return checker(locale,value,name);});if(FAILED(result))return result;return get_Options(output);
    }
};
// NLG's original suggestions enumerator uses VT_UI2|VT_BYREF pointing at UTF-16
// strings owned by the enumerator, rather than an allocated VT_BSTR.
struct Enumeration : Object<IEnumVARIANT,&IID_IEnumVARIANT> {
    std::vector<Com<IUnknown>> objects;std::vector<std::wstring> strings;size_t position=0;
    HRESULT STDMETHODCALLTYPE Next(ULONG count,VARIANT* output,ULONG* fetched) override {
        if(fetched)*fetched=0;if(!output||(!fetched&&count!=1))return E_POINTER;
        size_t size=objects.empty()?strings.size():objects.size();ULONG read=0;
        while(read<count&&position<size) {
            VariantInit(output+read);
            if(!objects.empty()){output[read].vt=VT_UNKNOWN;output[read].punkVal=objects[position].get();output[read].punkVal->AddRef();}
            else {output[read].vt=VT_UI2|VT_BYREF;output[read].puiVal=reinterpret_cast<USHORT*>(strings[position].data());}
            ++read;++position;
        }
        if(fetched)*fetched=read;return read==count?S_OK:S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG count) override {size_t size=objects.empty()?strings.size():objects.size();size_t skip=std::min(size-position,size_t(count));position+=skip;return skip==count?S_OK:S_FALSE;}
    HRESULT STDMETHODCALLTYPE Reset() override {position=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE Clone(IEnumVARIANT** output) override {if(!output)return E_POINTER;*output=nullptr;return protect([&]() -> HRESULT {
        Com<Enumeration> copy{new Enumeration};for(const auto& value:objects)copy->objects.push_back(retain(value.get()));copy->strings=strings;copy->position=position;*output=copy.release();return S_OK;
    });}
};
struct Segment : Object<NlITextSegment,&NL_IID_ITextSegment> {
    NlTextRange range{};LONG role=NL_ecrrSimpleSegment;std::vector<std::wstring> suggestions;
    HRESULT STDMETHODCALLTYPE get_Range(NlTextRange* output) override {if(!output)return E_POINTER;*output=range;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_Count(LONG* output) override {if(!output)return E_POINTER;*output=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_Item(LONG,NlITextSegment** output) override {if(!output)return E_POINTER;*output=nullptr;return E_INVALIDARG;}
    HRESULT STDMETHODCALLTYPE get_Role(LONG* output) override {if(!output)return E_POINTER;*output=role;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_Suggestions(IEnumVARIANT** output) override {if(!output)return E_POINTER;*output=nullptr;return protect([&]() -> HRESULT {
        if(suggestions.empty())return S_OK;Com<Enumeration> value{new Enumeration};value->strings=suggestions;*output=value.release();return S_OK;
    });}
};
struct Sentence : Object<NlISentence,&NL_IID_ISentence> {
    std::vector<Com<Segment>> segments;
    HRESULT STDMETHODCALLTYPE get_Count(LONG* output) override {if(!output)return E_POINTER;*output=LONG(segments.size());return S_OK;}
    HRESULT STDMETHODCALLTYPE get_Item(LONG index,NlITextSegment** output) override {
        if(!output)return E_POINTER;*output=nullptr;if(index<0||size_t(index)>=segments.size())return E_INVALIDARG;
        *output=segments[index].get();(*output)->AddRef();return S_OK;
    }
};
HRESULT suggest_segment(ISpellChecker* provider,Segment& segment,const std::wstring& word) {
    IEnumString* list=nullptr;HRESULT result=provider->Suggest(word.c_str(),&list);Com<IEnumString> choices(list);if(FAILED(result))return result;
    if(choices)for(;;){LPOLESTR text=nullptr;ULONG fetched=0;result=choices->Next(1,&text,&fetched);
        std::unique_ptr<WCHAR,decltype(&CoTaskMemFree)> allocation(text,CoTaskMemFree);if(FAILED(result))return result;if(!fetched)break;if(text)segment.suggestions.emplace_back(text);}
    return S_OK;
}
HRESULT spell_segment(ISpellChecker* provider,Segment& segment,const std::wstring& word,bool suggestions) {
    IEnumSpellingError* raw=nullptr;HRESULT result=provider->Check(word.c_str(),&raw);Com<IEnumSpellingError> errors(raw);if(FAILED(result))return result;
    if(!errors)return S_OK;ISpellingError* first=nullptr;result=errors->Next(&first);Com<ISpellingError> error(first);if(FAILED(result))return result;if(!error)return S_OK;
    CORRECTIVE_ACTION action{};result=error->get_CorrectiveAction(&action);if(FAILED(result))return result;if(action==CORRECTIVE_ACTION_NONE)return S_OK;
    segment.role=NL_ecrrIncorrect;if(!suggestions)return S_OK;
    return suggest_segment(provider,segment,word);
}
HRESULT context_errors(ISpellChecker* provider,const Context& context,LONG locale,const std::wstring& input,bool suggestions,std::vector<Com<Segment>>& output) {
    // Windows accepts NUL-terminated text. Check each explicit-input span
    // separately so embedded NULs do not silently truncate the later source.
    for(size_t base=0;base<input.size();) {
        size_t limit=input.find(L'\0',base);if(limit==std::wstring::npos)limit=input.size();
        if(limit>base) {
            IEnumSpellingError* raw=nullptr;HRESULT result=provider->ComprehensiveCheck(input.c_str()+base,&raw);Com<IEnumSpellingError> errors(raw);if(FAILED(result))return result;
            if(errors)for(;;) {
                ISpellingError* item=nullptr;result=errors->Next(&item);Com<ISpellingError> error(item);if(FAILED(result))return result;if(!error)break;
                ULONG start=0,length=0;CORRECTIVE_ACTION action{};
                if(FAILED(result=error->get_StartIndex(&start))||FAILED(result=error->get_Length(&length))||FAILED(result=error->get_CorrectiveAction(&action)))return result;
                if(action==CORRECTIVE_ACTION_NONE)continue;
                if(action<CORRECTIVE_ACTION_GET_SUGGESTIONS||action>CORRECTIVE_ACTION_DELETE)return E_UNEXPECTED;
                if(!length||start>limit-base||length>limit-base-start)return E_UNEXPECTED;
                auto word=input.substr(base+start,length);
                // Lexicons suppress spelling errors, while contextual deletion
                // remains meaningful even for words accepted by a lexicon.
                if(action!=CORRECTIVE_ACTION_DELETE&&context.accepts(word,locale))continue;
                Com<Segment> segment{new Segment};segment->range={LONG(base+start),LONG(length)};segment->role=NL_ecrrIncorrect;
                if(suggestions) {
                    if(action==CORRECTIVE_ACTION_DELETE)segment->suggestions.emplace_back();
                    else if(action==CORRECTIVE_ACTION_REPLACE) {
                        LPWSTR raw_text=nullptr;result=error->get_Replacement(&raw_text);std::unique_ptr<WCHAR,decltype(&CoTaskMemFree)> text(raw_text,CoTaskMemFree);if(FAILED(result))return result;if(!text)return E_UNEXPECTED;
                        segment->suggestions.emplace_back(text.get());
                    } else if(action==CORRECTIVE_ACTION_GET_SUGGESTIONS) {result=suggest_segment(provider,*segment,word);if(FAILED(result))return result;}
                    else return E_UNEXPECTED;
                }
                output.push_back(std::move(segment));
            }
        }
        base=limit==input.size()?limit:limit+1;
    }
    std::sort(output.begin(),output.end(),[](const auto& a,const auto& b){return a->range.start<b->range.start;});
    LONG limit=0;for(const auto& error:output){if(error->range.start<limit)return E_UNEXPECTED;limit=error->range.start+error->range.length;}
    return S_OK;
}
struct Chunk : Object<NlITextChunk,&NL_IID_ITextChunk> {
    std::wstring input;Com<Context> context;LONG locale=0;bool reuse=false;
    HRESULT STDMETHODCALLTYPE SetInputArray(const WCHAR* text,LONG count) override {return protect([&]() -> HRESULT {
        if(count<0||(!text&&count))return E_INVALIDARG;input=count?std::wstring(text,count):std::wstring{};return S_OK;
    });}
    HRESULT analyze(std::vector<Com<Sentence>>& sentences) {
        if(!context)return E_UNEXPECTED;Com<ISpellChecker> provider;std::string name;
        if(context->options->spelling){HRESULT result=checker(locale,provider,name);if(FAILED(result))return result;}
        else {WCHAR tag[LOCALE_NAME_MAX_LENGTH]{};if(!LCIDToLocaleName(locale?locale:GetUserDefaultLCID(),tag,LOCALE_NAME_MAX_LENGTH,LOCALE_ALLOW_NEUTRAL_NAMES))return E_INVALIDARG;for(const WCHAR* p=tag;*p;++p)name.push_back(char(*p));}
        const bool multiword=provider&&context->options->multiword;
        std::vector<Com<Segment>> reported;
        if(multiword){HRESULT result=context_errors(provider.get(),*context,locale?locale:GetUserDefaultLCID(),input,!context->options->verify,reported);if(FAILED(result))return result;}
        UErrorCode error=U_ZERO_ERROR;
        using Break=std::unique_ptr<UBreakIterator,decltype(&ubrk_close)>;
        Break boundary(ubrk_open(UBRK_SENTENCE,name.c_str(),reinterpret_cast<const UChar*>(input.data()),int(input.size()),&error),ubrk_close);
        if(U_FAILURE(error)||!boundary)return E_FAIL;
        LONG start=ubrk_first(boundary.get()),end=0;
        while((end=ubrk_next(boundary.get()))!=UBRK_DONE) {
            Com<Sentence> sentence{new Sentence};error=U_ZERO_ERROR;
            Break words(ubrk_open(UBRK_WORD,name.c_str(),reinterpret_cast<const UChar*>(input.data()+start),end-start,&error),ubrk_close);if(U_FAILURE(error)||!words)return E_FAIL;
            LONG left=ubrk_first(words.get()),right=0;
            while((right=ubrk_next(words.get()))!=UBRK_DONE) {
                if(ubrk_getRuleStatus(words.get())>=UBRK_WORD_NUMBER) {
                    Com<Segment> segment{new Segment};segment->range={start+left,right-left};
                    std::wstring word=input.substr(start+left,right-left);
                    if(provider&&!multiword&&ubrk_getRuleStatus(words.get())>=UBRK_WORD_LETTER&&!context->accepts(word,locale?locale:GetUserDefaultLCID())) {
                        HRESULT result=spell_segment(provider.get(),*segment,word,!context->options->verify);if(FAILED(result))return result;
                    }
                    if(!multiword)sentence->segments.push_back(std::move(segment));
                    else {
                        LONG cursor=segment->range.start,limit=cursor+segment->range.length;
                        for(const auto& error:reported){LONG a=error->range.start,b=a+error->range.length;if(b<=cursor)continue;if(a>=limit)break;
                            if(a>cursor){Com<Segment> piece{new Segment};piece->range={cursor,a-cursor};sentence->segments.push_back(std::move(piece));}
                            cursor=std::min(limit,b);if(cursor==limit)break;
                        }
                        if(cursor<limit){Com<Segment> piece{new Segment};piece->range={cursor,limit-cursor};sentence->segments.push_back(std::move(piece));}
                    }
                }
                left=right;
            }
            if(multiword){for(const auto& error:reported)if(error->range.start>=start&&error->range.start<end)sentence->segments.push_back(retain(error.get()));
                std::sort(sentence->segments.begin(),sentence->segments.end(),[](const auto& a,const auto& b){return a->range.start<b->range.start;});}
            if(!sentence->segments.empty())sentences.push_back(std::move(sentence));start=end;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Count(LONG* output) override {if(!output)return E_POINTER;*output=0;return protect([&]() -> HRESULT {
        std::vector<Com<Sentence>> sentences;HRESULT result=analyze(sentences);if(SUCCEEDED(result))*output=LONG(sentences.size());return result;
    });}
    HRESULT STDMETHODCALLTYPE get_Item(LONG index,NlISentence** output) override {if(!output)return E_POINTER;*output=nullptr;return protect([&]() -> HRESULT {
        std::vector<Com<Sentence>> sentences;HRESULT result=analyze(sentences);if(FAILED(result))return result;if(index<0||size_t(index)>=sentences.size())return E_INVALIDARG;*output=sentences[index].release();return S_OK;
    });}
    HRESULT STDMETHODCALLTYPE get_Sentences(IEnumVARIANT** output) override {return GetEnumerator(output);}
    HRESULT STDMETHODCALLTYPE GetEnumerator(IEnumVARIANT** output) override {if(!output)return E_POINTER;*output=nullptr;return protect([&]() -> HRESULT {
        std::vector<Com<Sentence>> sentences;HRESULT result=analyze(sentences);if(FAILED(result))return result;
        Com<Enumeration> value{new Enumeration};for(auto& sentence:sentences)value->objects.emplace_back(sentence.release());*output=value.release();return S_OK;
    });}
    HRESULT STDMETHODCALLTYPE get_Context(NlITextContext** output) override {if(!output)return E_POINTER;*output=context.get();if(*output)(*output)->AddRef();return S_OK;}
    HRESULT STDMETHODCALLTYPE put_Context(NlITextContext* value) override {auto own=dynamic_cast<Context*>(value);if(value&&!own)return E_INVALIDARG;context=retain(own);return S_OK;}
    HRESULT STDMETHODCALLTYPE put_Locale(LONG value) override {locale=value;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_ReuseObjects(VARIANT_BOOL* output) override {if(!output)return E_POINTER;*output=reuse?VARIANT_TRUE:VARIANT_FALSE;return S_OK;}
    HRESULT STDMETHODCALLTYPE put_ReuseObjects(VARIANT_BOOL value) override {reuse=value!=VARIANT_FALSE;return S_OK;}
};
}
extern "C" __declspec(dllexport) HRESULT WINAPI NlLoad() {
    // COM apartment initialization belongs to WPF/the caller; no hidden balance
    // or process-wide mutable checker survives an NLG object's ownership.
    Com<ISpellCheckerFactory> provider;return factory(provider);
}
extern "C" __declspec(dllexport) void WINAPI NlUnload() {}
extern "C" __declspec(dllexport) HRESULT WINAPI NlGetClassObject(const GUID* clsid,const GUID* iid,void** output) {
    if(!output)return E_POINTER;*output=nullptr;if(!clsid||!iid)return E_POINTER;
    return protect([&]() -> HRESULT {
        Com<IUnknown> value;
        if(*clsid==NL_CLSID_ITextChunk)value.reset(new Chunk);
        else if(*clsid==NL_CLSID_ITextContext)value.reset(new Context);
        else if(*clsid==NL_CLSID_Lexicon)value.reset(new Lexicon);
        else return CLASS_E_CLASSNOTAVAILABLE;
        return value->QueryInterface(*iid,output);
    });
}
