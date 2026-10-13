// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#define UNICODE 1
#include <windows.h>
#include <stddef.h>
#include <wchar.h>
#include <string.h>
static volatile LONG stage;
extern "C" __declspec(dllexport) int FieldMarshalStage() { return InterlockedCompareExchange(&stage,0,0); }
extern "C" __declspec(dllexport) int FieldMarshalDefaults(PRINTER_DEFAULTSW* value) {
    if(sizeof(*value)!=24 || offsetof(PRINTER_DEFAULTSW,pDevMode)!=8 || offsetof(PRINTER_DEFAULTSW,DesiredAccess)!=16 ||
        wcscmp(value->pDatatype,L"東京 λ 😀") || reinterpret_cast<ULONG_PTR>(value->pDevMode)!=0x123456789abcdef0ULL ||
        value->DesiredAccess!=0x80000000u) return -1;
    InterlockedExchange(&stage,1); Sleep(150);
    if(wcscmp(value->pDatatype,L"東京 λ 😀") || value->DesiredAccess!=0x80000000u) { InterlockedExchange(&stage,2); return -2; }
    value->DesiredAccess=0x80000042u;
    InterlockedExchange(&stage,2); return 42;
}
extern "C" __declspec(dllexport) int FieldMarshalDoc(const DOCINFOW* value) {
    return sizeof(*value)==40 && value->cbSize==sizeof(*value) && !wcscmp(value->lpszDocName,L"λ document") &&
        value->lpszOutput==nullptr && !wcscmp(value->lpszDatatype,L"RAW") && value->fwType==0x80000000u ? 40 : -1;
}
