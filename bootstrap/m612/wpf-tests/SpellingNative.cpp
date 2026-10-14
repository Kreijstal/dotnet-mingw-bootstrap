// SPDX-License-Identifier: MIT
#include "NaturalLanguageNative.h"
#include <cstdio>
#include <stdexcept>
#include <memory>
template<class T> struct Release {void operator()(T* p)const{if(p)p->Release();}};
template<class T> using Com=std::unique_ptr<T,Release<T>>;
static void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(int argc,char** argv){try{
 require(argc==2,"usage: DLL");auto library=LoadLibraryA(argv[1]);require(library!=nullptr,"DLL load");
 auto load=reinterpret_cast<HRESULT(WINAPI*)()>(GetProcAddress(library,"NlLoad"));auto unload=reinterpret_cast<void(WINAPI*)()>(GetProcAddress(library,"NlUnload"));auto create=reinterpret_cast<HRESULT(WINAPI*)(const GUID*,const GUID*,void**)>(GetProcAddress(library,"NlGetClassObject"));require(load&&unload&&create,"legacy exports");
 require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"STA initialization");require(load()==S_OK,"native spelling factory");
 void* output=reinterpret_cast<void*>(1);GUID absent{};
 require(create(nullptr,&IID_IUnknown,&output)==E_POINTER&&!output,"null class/output clearing");output=reinterpret_cast<void*>(1);
 require(create(&absent,&IID_IUnknown,&output)==CLASS_E_CLASSNOTAVAILABLE&&!output,"unknown class");output=reinterpret_cast<void*>(1);
 require(create(&NL_CLSID_ITextChunk,&absent,&output)==E_NOINTERFACE&&!output,"unknown IID");require(create(&NL_CLSID_ITextChunk,&NL_IID_ITextChunk,nullptr)==E_POINTER,"null result");
 NlITextChunk* raw=nullptr;require(create(&NL_CLSID_ITextChunk,&NL_IID_ITextChunk,reinterpret_cast<void**>(&raw))==S_OK&&raw,"original chunk identity");Com<NlITextChunk> chunk(raw);
 require(chunk->stub_get_InputText()==E_NOTIMPL,"upstream unused placeholder stays explicitly unsupported");
 VARIANT_BOOL reuse=VARIANT_FALSE;require(chunk->put_ReuseObjects(VARIANT_TRUE)==S_OK&&chunk->get_ReuseObjects(&reuse)==S_OK&&reuse==VARIANT_TRUE,"COM boolean ABI");
 NlITextContext* contextRaw=nullptr;require(create(&NL_CLSID_ITextContext,&NL_IID_ITextContext,reinterpret_cast<void**>(&contextRaw))==S_OK,"original context identity");Com<NlITextContext> context(contextRaw);
 require(chunk->put_Context(context.get())==S_OK&&chunk->put_Locale(0x409)==S_OK,"chunk context ownership/locale");
 NlIProcessingOptions* optionsRaw=nullptr;require(context->get_Options(&optionsRaw)==S_OK,"processing options");Com<NlIProcessingOptions> options(optionsRaw);
 NlIProcessingOptions* unsupported=reinterpret_cast<NlIProcessingOptions*>(1);require(context->get_Capabilities(0x7fffffff,&unsupported)==E_INVALIDARG&&!unsupported,"unavailable culture rejects with cleared capability output");
 VARIANT key{},value{};key.vt=VT_BSTR;key.bstrVal=SysAllocString(L"IsSpellChecking");value.vt=VT_BOOL;value.boolVal=VARIANT_TRUE;HRESULT result=options->put_Item(key,value);VariantClear(&key);require(result==S_OK,"VARIANT-by-value option ABI");
 key.vt=VT_BSTR;key.bstrVal=SysAllocString(L"IsSpellSuggestingMWEs");value.boolVal=VARIANT_TRUE;result=options->put_Item(key,value);VariantClear(&key);require(result==E_NOTIMPL,"unsupported compound spelling is explicit");
 require(chunk->SetInputArray(nullptr,1)==E_INVALIDARG&&chunk->SetInputArray(L"hello speling",-1)==E_INVALIDARG,"invalid input lengths");
 require(chunk->SetInputArray(L"hello speling",13)==S_OK,"source input");IEnumVARIANT* enumRaw=nullptr;require(chunk->GetEnumerator(&enumRaw)==S_OK&&enumRaw,"sentences");Com<IEnumVARIANT> enumeration(enumRaw);
 IEnumVARIANT* cloneRaw=nullptr;require(enumeration->Clone(&cloneRaw)==S_OK&&cloneRaw,"enumerator clone");Com<IEnumVARIANT> clone(cloneRaw);
 // Enumeration must own a snapshot after the chunk and its context disappear.
 chunk.reset();context.reset();options.reset();unload();
 VARIANT item{};ULONG fetched=0;require(enumeration->Next(1,&item,&fetched)==S_OK&&fetched==1&&item.vt==VT_UNKNOWN,"snapshot after owner release");
 NlISentence* sentenceRaw=nullptr;require(item.punkVal->QueryInterface(NL_IID_ISentence,reinterpret_cast<void**>(&sentenceRaw))==S_OK,"original sentence IID");Com<NlISentence> sentence(sentenceRaw);VariantClear(&item);LONG count=0;require(sentence->get_Count(&count)==S_OK&&count==2,"snapshot word count");
 NlITextSegment* segmentRaw=nullptr;require(sentence->get_Item(1,&segmentRaw)==S_OK,"segment");Com<NlITextSegment> segment(segmentRaw);NlTextRange range{};LONG role=0;require(segment->get_Range(&range)==S_OK&&range.start==6&&range.length==7&&segment->get_Role(&role)==S_OK&&role==2,"original offset and role ABI");
 require(enumeration->Next(1,&item,&fetched)==S_FALSE&&!fetched,"end of snapshot");require(enumeration->Reset()==S_OK&&enumeration->Skip(1)==S_OK&&enumeration->Skip(1)==S_FALSE,"enumerator reset/skip");require(clone->Next(1,&item,&fetched)==S_OK&&fetched==1,"independent cloned position");VariantClear(&item);
 require(load()==S_OK,"reload while surviving references exist");unload();segment.reset();sentence.reset();clone.reset();enumeration.reset();CoUninitialize();FreeLibrary(library);
 std::puts("native legacy spelling: HRESULT/class/IID, boolean/VARIANT ABI, invalid inputs, snapshot/clone lifetime, original word offsets/roles and reload PASS");return 0;
}catch(const std::exception& error){std::printf("FAIL: %s\n",error.what());return 1;}}
