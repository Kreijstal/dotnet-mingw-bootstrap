// SPDX-License-Identifier: MIT
#include <windows.h>
static LONG live=0,closed=0,trace=0;
struct Resource { int id; };
extern "C" __declspec(dllexport) void* finalizer_open(int id) {
    auto* value=new Resource{id}; InterlockedIncrement(&live); return value;
}
extern "C" __declspec(dllexport) void finalizer_close(void* pointer) {
    if(!pointer) return;
    auto* value=static_cast<Resource*>(pointer);
    trace=trace*10+value->id;
    delete value; InterlockedIncrement(&closed); InterlockedDecrement(&live);
}
extern "C" __declspec(dllexport) int finalizer_live() { return live; }
extern "C" __declspec(dllexport) int finalizer_closed() { return closed; }
extern "C" __declspec(dllexport) int finalizer_trace() { return trace; }
extern "C" __declspec(dllexport) void finalizer_reset() { trace=0; closed=0; }
