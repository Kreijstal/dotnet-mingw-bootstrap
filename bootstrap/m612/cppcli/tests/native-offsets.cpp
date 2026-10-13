// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include <windows.h>
#include <winspool.h>
#include <stddef.h>
struct OffsetInner { char Tag; long long Value; };
struct OffsetNested { char Tag; OffsetInner Items[3]; };
#pragma pack(push,1)
struct OffsetPacked { char Tag; void* Pointer; int Value; };
#pragma pack(pop)
extern "C" int NativeOffsetCheck(int index,unsigned long long value);
public ref class NativeOffsets abstract sealed {
public:
    static bool Check() {
        return NativeOffsetCheck(0,offsetof(PRINTER_INFO_2W,pPrinterName)) &&
            NativeOffsetCheck(1,offsetof(PRINTER_INFO_2W,pSecurityDescriptor)) &&
            NativeOffsetCheck(2,offsetof(PRINTER_INFO_2W,Status)) &&
            NativeOffsetCheck(3,offsetof(OffsetNested,Items[2].Value)) &&
            NativeOffsetCheck(4,offsetof(OffsetPacked,Pointer)) &&
            NativeOffsetCheck(5,offsetof(OffsetPacked,Value));
    }
};
