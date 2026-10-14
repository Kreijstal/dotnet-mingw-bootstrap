// SPDX-License-Identifier: MIT
// Original WPF HRESULT/out-retval hyphenation ABI backed by source libhyphen.
#include <windows.h>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <unicode/ubrk.h>
#include <unicode/ustring.h>
#include <unicode/uchar.h>
#include <unicode/utf16.h>
#include "hyphen/engine/hyphen.h"
#include "HyphenPatterns.h"
namespace {
struct Resource {};
struct Dictionary { HyphenDict* value=nullptr;~Dictionary(){if(value)hnj_hyphen_free(value);} };
std::mutex gate;
std::unordered_map<void*,std::shared_ptr<Resource>> resources;
std::unordered_map<std::string,std::shared_ptr<Dictionary>> dictionaries;
std::string locale_name(int locale) {
    WCHAR name[LOCALE_NAME_MAX_LENGTH]{};
    if(!LCIDToLocaleName(locale?LCID(locale):GetUserDefaultLCID(),name,LOCALE_NAME_MAX_LENGTH,LOCALE_ALLOW_NEUTRAL_NAMES))return {};
    std::string result;for(WCHAR value:name){if(!value)break;result.push_back(char(value));}return result;
}
std::shared_ptr<Dictionary> get_dictionary(const std::string& locale) {
    auto language=locale.substr(0,locale.find('-'));std::string selected=language;
    if(language=="en")selected="en-US";
    if(language=="de")selected=locale=="de-CH"?"de-CH":"de-1996";
    if(language=="sr")selected="sh";
    const HyphenPatterns* patterns=nullptr;for(const auto& item:builtin_hyphen_patterns)if(selected==item.locale){patterns=&item;break;}
    if(!patterns)return {};
    auto old=dictionaries.find(selected);if(old!=dictionaries.end())return old->second;
    auto result=std::make_shared<Dictionary>();result->value=hnj_hyphen_load_data(patterns->text,patterns->size);if(!result->value)return {};
    dictionaries.emplace(selected,result);return result;
}
struct Replacements {
    char** text=nullptr;int* positions=nullptr;int* cuts=nullptr;size_t size=0;
    ~Replacements(){if(text){for(size_t i=0;i<size;++i)std::free(text[i]);std::free(text);}std::free(positions);std::free(cuts);}
};
}
extern "C" __declspec(dllexport) HRESULT WINAPI NlCreateHyphenator(void** output) {
    if(!output)return E_POINTER;*output=nullptr;
    try{auto value=std::make_shared<Resource>();std::lock_guard<std::mutex> lock(gate);resources.emplace(value.get(),value);*output=value.get();return S_OK;}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
}
extern "C" __declspec(dllexport) void WINAPI NlDestroyHyphenator(void** handle) {
    if(!handle)return;std::lock_guard<std::mutex> lock(gate);resources.erase(*handle);*handle=nullptr;
}
extern "C" __declspec(dllexport) HRESULT WINAPI NlHyphenate(void* handle,const WCHAR* input,int length,int locale,uint8_t* bits,int capacity) {
    if(length<0||capacity<0||length>INT_MAX-7||capacity<(length+7)/8||(!input&&length)||(!bits&&capacity))return E_INVALIDARG;
    if(capacity)std::memset(bits,0,size_t(capacity));
    try {
        std::lock_guard<std::mutex> lock(gate);if(resources.find(handle)==resources.end())return E_HANDLE;
        auto name=locale_name(locale);auto dictionary=get_dictionary(name);if(!dictionary||!length)return S_OK;
        UErrorCode error=U_ZERO_ERROR;std::unique_ptr<UBreakIterator,decltype(&ubrk_close)> words(ubrk_open(UBRK_WORD,name.c_str(),reinterpret_cast<const UChar*>(input),length,&error),ubrk_close);
        if(U_FAILURE(error)||!words)return E_FAIL;
        error=U_ZERO_ERROR;std::unique_ptr<UBreakIterator,decltype(&ubrk_close)> clusters(ubrk_open(UBRK_CHARACTER,name.c_str(),reinterpret_cast<const UChar*>(input),length,&error),ubrk_close);
        if(U_FAILURE(error)||!clusters)return E_FAIL;
        int start=ubrk_first(words.get()),end=0;
        while((end=ubrk_next(words.get()))!=UBRK_DONE) {
            if(ubrk_getRuleStatus(words.get())>=UBRK_WORD_LETTER&&ubrk_getRuleStatus(words.get())<UBRK_WORD_KANA) {
                std::vector<UChar> lower;std::vector<int> source{start};
                for(int at=start;at<end;){UChar32 value=0;U16_NEXT(reinterpret_cast<const UChar*>(input),at,end,value);
                    value=name.rfind("tr",0)==0&&value=='I'?0x131:u_tolower(value);
                    if(value<=0xffff)lower.push_back(UChar(value));else{lower.push_back(U16_LEAD(value));lower.push_back(U16_TRAIL(value));}source.push_back(at);}
                error=U_ZERO_ERROR;int bytes=0;u_strToUTF8(nullptr,0,&bytes,lower.data(),int(lower.size()),&error);
                if(error!=U_BUFFER_OVERFLOW_ERROR&&U_FAILURE(error))return E_FAIL;
                std::string word(size_t(bytes),'\0');error=U_ZERO_ERROR;u_strToUTF8(word.data(),bytes,nullptr,lower.data(),int(lower.size()),&error);if(U_FAILURE(error))return E_FAIL;
                std::vector<char> hyphens(size_t(bytes)+5);Replacements replacements;replacements.size=size_t(bytes);
                if(hnj_hyphen_hyphenate2(dictionary->value,word.data(),bytes,hyphens.data(),nullptr,&replacements.text,&replacements.positions,&replacements.cuts))return E_FAIL;
                for(size_t at=1;at+1<source.size();++at)if((hyphens[at-1]&1)&&(!replacements.text||!replacements.text[at-1])&&ubrk_isBoundary(clusters.get(),source[at]))bits[source[at]/8]|=uint8_t(1u<<(source[at]%8));
                // WPF's bitmask ABI cannot carry spelling replacements; expose only
                // opportunities that leave all source characters unchanged.
            }
            start=end;
        }
        return S_OK;
    }catch(const std::bad_alloc&){if(capacity)std::memset(bits,0,size_t(capacity));return E_OUTOFMEMORY;}
}
