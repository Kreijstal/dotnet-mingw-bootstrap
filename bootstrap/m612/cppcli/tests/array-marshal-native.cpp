// SPDX-License-Identifier: MIT
#include <windows.h>
#include <unknwn.h>
#include <cstdint>
struct IArrayService:IUnknown {
    virtual int STDMETHODCALLTYPE Change(uint8_t*,int)=0;
    virtual int STDMETHODCALLTYPE Fixed(int*)=0;
    virtual int STDMETHODCALLTYPE Combined(int*,int)=0;
    virtual int STDMETHODCALLTYPE FirstIndex(int,uint8_t*)=0;
};
extern "C" __declspec(dllexport) int call_array_service(void* pointer) {
    auto* service=static_cast<IArrayService*>(pointer);
    uint8_t values[5]={1,2,3,99,100};
    if(service->Change(values,3)!=3) return 1;
    if(values[0]!=2||values[1]!=3||values[2]!=4||values[3]!=99||values[4]!=100) return 2;
    if(service->Change(values,0)!=0 || values[0]!=2 || values[4]!=100) return 6;
    int integers[8]={10,20,30,40,50,999,1000,1001};
    if(service->Fixed(integers)!=60) return 3;
    if(service->Combined(integers,2)!=150) return 4;
    if(service->Combined(integers,0)!=60) return 7;
    if(service->FirstIndex(3,values)!=6) return 5;
    return 0;
}
