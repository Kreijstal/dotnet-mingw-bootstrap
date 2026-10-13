// SPDX-License-Identifier: MIT
// Compile as native C++ and C++/CLI against the same trusted SDK declarations.
#include <windows.h>
#include <winspool.h>
#ifdef SPOOLER_NATIVE_ORACLE
#include <cstdio>
#else
using namespace System;
#endif
int main() {
    DWORD needed=0,count=0;
    int result=EnumPrintersW(PRINTER_ENUM_LOCAL,nullptr,1,nullptr,0,&needed,&count);
#ifdef SPOOLER_NATIVE_ORACLE
    std::printf("%d\n%lu\n%lu\n",result,static_cast<unsigned long>(needed),static_cast<unsigned long>(count));
#else
    Console::WriteLine(result);
    Console::WriteLine(static_cast<unsigned int>(needed));
    Console::WriteLine(static_cast<unsigned int>(count));
#endif
    return 0;
}
