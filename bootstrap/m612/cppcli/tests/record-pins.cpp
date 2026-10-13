// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#define UNICODE 1
#include <windows.h>
#include <string.h>
using namespace System;
extern "C" int NativePinnedRecord(const ENUMLOGFONTEXDV*,ENUMLOGFONTEXDV*);
extern "C" int NativeRecordStage();
public ref class RecordPins abstract sealed {
public:
    static int Size() { return sizeof(ENUMLOGFONTEXDV); }
    static int Stage() { return NativeRecordStage(); }
    static int Read(array<Byte>^ bytes,array<Byte>^ copied) {
        interior_ptr<ENUMLOGFONTEXDV> record=(interior_ptr<ENUMLOGFONTEXDV>)&bytes[0];
        interior_ptr<ENUMLOGFONTEXDV> output=(interior_ptr<ENUMLOGFONTEXDV>)&copied[0];
        record->elfEnumLogfontEx.elfLogFont.lfHeight=23;
        record->elfEnumLogfontEx.elfLogFont.lfFaceName[0]=0x03bb;
        record->elfEnumLogfontEx.elfLogFont.lfFaceName[1]=0x6771;
        pin_ptr<ENUMLOGFONTEXDV> nativeRecord=record;
        pin_ptr<ENUMLOGFONTEXDV> nativeOutput=output;
        int result=NativePinnedRecord(nativeRecord,nativeOutput);
        if(record->elfEnumLogfontEx.elfLogFont.lfHeight!=23 || output->elfEnumLogfontEx.elfLogFont.lfHeight!=42) return -3;
        return result;
    }
    static void Copy(array<Byte>^ input,array<Byte>^ output) {
        interior_ptr<ENUMLOGFONTEXDV> source=(interior_ptr<ENUMLOGFONTEXDV>)&input[0];
        interior_ptr<ENUMLOGFONTEXDV> target=(interior_ptr<ENUMLOGFONTEXDV>)&output[0];
        pin_ptr<ENUMLOGFONTEXDV> nativeSource=source;
        pin_ptr<ENUMLOGFONTEXDV> nativeTarget=target;
        ::memcpy(nativeTarget,nativeSource,sizeof(ENUMLOGFONTEXDV));
    }
    static int Font(array<Byte>^ bytes) {
        interior_ptr<ENUMLOGFONTEXDV> record=(interior_ptr<ENUMLOGFONTEXDV>)&bytes[0];
        pin_ptr<ENUMLOGFONTEXDV> nativeRecord=record;
        HFONT font=::CreateFontIndirectExW(nativeRecord);
        if(font==nullptr) return -1;
        int height=0;
        int deleted=0;
        try {
            LOGFONTW description;
            if(::GetObjectW(font,sizeof(LOGFONTW),&description)==sizeof(LOGFONTW)) height=description.lfHeight;
        } finally { deleted=::DeleteObject(font); }
        return deleted ? height : -2;
    }
};
