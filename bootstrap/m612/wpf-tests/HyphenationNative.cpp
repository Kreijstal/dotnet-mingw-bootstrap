// SPDX-License-Identifier: MIT
#include <windows.h>
#include <cstdio>
#include <stdexcept>
#include <cstring>
static void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(int argc,char** argv){try{
    require(argc==2,"usage: DLL");auto dll=LoadLibraryA(argv[1]);require(dll!=nullptr,"DLL load");
    using Create=HRESULT(WINAPI*)(void**);using Destroy=void(WINAPI*)(void**);using Analyze=HRESULT(WINAPI*)(void*,const WCHAR*,int,int,unsigned char*,int);
    auto create=reinterpret_cast<Create>(GetProcAddress(dll,"NlCreateHyphenator"));auto destroy=reinterpret_cast<Destroy>(GetProcAddress(dll,"NlDestroyHyphenator"));auto analyze=reinterpret_cast<Analyze>(GetProcAddress(dll,"NlHyphenate"));require(create&&destroy&&analyze,"original native exports");
    void* handle=nullptr;require(create(nullptr)==E_POINTER&&create(&handle)==S_OK&&handle,"HRESULT/out-retval resource ABI");unsigned char bits[4]{0xff,0xff,0xff,0xff};
    require(analyze(handle,L"hyphenation",11,0x409,bits,4)==S_OK&&bits[0]==0x44&&!bits[1]&&!bits[2]&&!bits[3],"exact English source boundaries and full output clearing");
    require(analyze(handle,L"HYPHENATION",11,0x409,bits,4)==S_OK&&bits[0]==0x44&&!bits[1],"case normalization retains original UTF-16 positions");
    require(analyze(handle,L"\xd83d\xde00 hyphenation",14,0x409,bits,4)==S_OK&&bits[0]==0x20&&bits[1]==2&&!bits[2]&&!bits[3],"surrogate pair and word offset preserve source bit positions");
    require(analyze(handle,L"hyphenation",11,0x7fffffff,bits,4)==S_OK&&!bits[0]&&!bits[1]&&!bits[2]&&!bits[3],"unavailable locale has no invented breaks");
    bits[0]=0x7f;require(analyze(handle,L"hyphenation",11,0x409,bits,1)==E_INVALIDARG&&bits[0]==0x7f,"short output buffer rejects without an overrun");
    void* stale=handle;destroy(&handle);require(!handle,"ref resource disposal clears original handle");bits[0]=0xff;require(analyze(stale,L"word",4,0x409,bits,4)==E_HANDLE&&!bits[0]&&!bits[1]&&!bits[2]&&!bits[3],"disposed resources reject with cleared output");destroy(&handle);destroy(nullptr);
    std::puts("native hyphenator: HRESULT/out-retval, exact/uppercase/surrogate offsets, unavailable locales, short arrays, stale resources and idempotent cleanup PASS");return 0;
}catch(const std::exception& error){std::printf("FAIL: %s\n",error.what());return 1;}}
