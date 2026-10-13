// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include <windows.h>
#include <cstring>
#include <cstddef>
extern "C" __declspec(dllexport) int ValuePinProbe(void* pointer,int kind) {
    Sleep(2);
    unsigned char* data=static_cast<unsigned char*>(pointer);
    int stride=kind ? 32 : static_cast<int>(sizeof(POINT));
    int yOffset=kind ? 20 : static_cast<int>(offsetof(POINT,y));
    for(int index=0;index<3;++index) {
        int x,y; std::memcpy(&x,data+index*stride,4); std::memcpy(&y,data+index*stride+yOffset,4);
        if(x!=10+index || y!=20+index) return 1;
    }
    int x=30,y=40;
    std::memcpy(data+stride,&x,4); std::memcpy(data+stride+yOffset,&y,4);
    return 0;
}
