// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#define UNICODE 1
#include <windows.h>
#include <string.h>
static volatile LONG stage;
extern "C" __declspec(dllexport) int NativeRecordStage() { return InterlockedCompareExchange(&stage,0,0); }
extern "C" __declspec(dllexport) int NativePinnedRecord(const ENUMLOGFONTEXDV* input,ENUMLOGFONTEXDV* output) {
    const auto& font=input->elfEnumLogfontEx.elfLogFont;
    if(font.lfHeight!=23 || font.lfFaceName[0]!=0x03bb || font.lfFaceName[1]!=0x6771) return -1;
    InterlockedExchange(&stage,1);
    Sleep(150);
    if(font.lfHeight!=23 || font.lfFaceName[0]!=0x03bb || font.lfFaceName[1]!=0x6771) { InterlockedExchange(&stage,2); return -2; }
    memcpy(output,input,sizeof(*input));
    output->elfEnumLogfontEx.elfLogFont.lfHeight=42;
    InterlockedExchange(&stage,2);
    return 42;
}
