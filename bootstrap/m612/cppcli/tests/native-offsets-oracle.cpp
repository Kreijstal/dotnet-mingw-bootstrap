// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include <windows.h>
#include <winspool.h>
#include <stddef.h>
struct OffsetInner { char Tag; long long Value; };
struct OffsetNested { char Tag; OffsetInner Items[3]; };
#pragma pack(push,1)
struct OffsetPacked { char Tag; void* Pointer; int Value; };
#pragma pack(pop)
extern "C" __declspec(dllexport) int NativeOffsetCheck(int index,unsigned long long value) {
    PRINTER_INFO_2W printer{}; OffsetNested nested{}; OffsetPacked packed{};
    const unsigned long long offsets[]={
        static_cast<unsigned long long>(reinterpret_cast<char*>(&printer.pPrinterName)-reinterpret_cast<char*>(&printer)),
        static_cast<unsigned long long>(reinterpret_cast<char*>(&printer.pSecurityDescriptor)-reinterpret_cast<char*>(&printer)),
        static_cast<unsigned long long>(reinterpret_cast<char*>(&printer.Status)-reinterpret_cast<char*>(&printer)),
        static_cast<unsigned long long>(reinterpret_cast<char*>(&nested.Items[2].Value)-reinterpret_cast<char*>(&nested)),
        static_cast<unsigned long long>(reinterpret_cast<char*>(&packed.Pointer)-reinterpret_cast<char*>(&packed)),
        static_cast<unsigned long long>(reinterpret_cast<char*>(&packed.Value)-reinterpret_cast<char*>(&packed))};
    return index>=0 && index<6 && value==offsets[index];
}
