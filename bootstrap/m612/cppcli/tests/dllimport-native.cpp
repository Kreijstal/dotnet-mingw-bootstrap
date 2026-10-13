// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include <windows.h>
#include <cwchar>
#include <cstring>
extern "C" __declspec(dllexport) int WINAPI ProbeWide(const wchar_t* text,int* first) {
    *first=text ? text[0] : -1;
    int count=text ? static_cast<int>(std::wcslen(text)) : -1;
    SetLastError(1234); return count;
}
extern "C" __declspec(dllexport) BOOL __cdecl ProbeBool(BOOL value) { return value ? FALSE : TRUE; }
extern "C" __declspec(dllexport) int WINAPI ProbeArray(int* values,int length) {
    int sum=0; for(int i=0;i<length;++i) { sum+=values[i]; values[i]+=10; } return sum;
}
extern "C" __declspec(dllexport) int WINAPI ProbeHandle(HANDLE value) {
    DWORD flags=0; return GetHandleInformation(value,&flags) ? 1 : 0;
}
extern "C" __declspec(dllexport) int WINAPI ProbeBuffer(wchar_t* value,int size) {
    if(size<4) return -1;
    value[0]=L'\u03bb'; value[1]=0xd83d; value[2]=0xde80; value[3]=0; return 3;
}
extern "C" __declspec(dllexport) int WINAPI ProbeAnsi(const char* value) { return value ? static_cast<int>(std::strlen(value)) : -1; }
