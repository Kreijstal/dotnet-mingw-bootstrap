// SPDX-License-Identifier: MIT
#include <windows.h>
#include <winspool.h>
#ifdef WPF_MISSING_PRINT_PROPERTIES
#include "PrintingSdk.h"
#endif
#include <cstddef>
#include <cstdio>
static_assert(sizeof(EPrintPropertyType)==4);
static_assert(sizeof(PrintPropertyValue)==24 && alignof(PrintPropertyValue)==8);
static_assert(offsetof(PrintPropertyValue,value)==8);
static_assert(sizeof(PrintNamedProperty)==32 && offsetof(PrintNamedProperty,propertyValue)==8);
static_assert(sizeof(PrintPropertiesCollection)==16 && offsetof(PrintPropertiesCollection,propertiesCollection)==8);
int main() {
    static_assert(kPropertyTypeString==1 && kPropertyTypeInt32==2 && kPropertyTypeTime==5 && kPropertyTypeBuffer==10);
    PrintPropertyValue property{};
    property.ePropertyType=kPropertyTypeBuffer;
    BYTE bytes[3]={1,2,3};
    property.value.propertyBlob={3,bytes};
    PrintNamedProperty named{const_cast<WCHAR*>(L"source"),property};
    PrintPropertiesCollection collection{1,&named};
    if(collection.propertiesCollection[0].propertyValue.value.propertyBlob.cbBuf!=3 ||
       static_cast<BYTE*>(collection.propertiesCollection[0].propertyValue.value.propertyBlob.pBuf)[2]!=3) return 1;
    HMODULE spooler=LoadLibraryW(L"winspool.drv");
    if(!spooler) return 2;
    using Get=int (WINAPI*)(HANDLE,DWORD,PCWSTR,PrintPropertyValue*);
    auto get=reinterpret_cast<Get>(GetProcAddress(spooler,"GetJobNamedPropertyValue"));
    if(!get) return 3;
    PrintPropertyValue output{};
    // Read-only failure path; no printer/job is opened or created.
    int error=get(nullptr,0,L"source-bootstrap",&output);
    FreeLibrary(spooler);
    if(error!=ERROR_INVALID_HANDLE) return 4;
    std::puts("print SDK x64 ABI: property 24/value offset 8, named 32, collection 16; typed blob access and real Winspool named-property invalid-handle path PASS");
}
