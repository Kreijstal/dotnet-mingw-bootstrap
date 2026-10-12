// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include <windows.h>
#include <malloc.h>
struct NativePair { int Low; long long High; };
static_assert(sizeof(NativePair) == 16);
extern "C" __declspec(dllexport) int NativeAllocationCheck(const void* pointer) {
    const auto* value = static_cast<const NativePair*>(pointer);
    return _msize(const_cast<void*>(pointer)) >= sizeof(NativePair) && value->Low == 7 && value->High == 5000000002LL ? 0 : 1;
}
extern "C" __declspec(dllexport) int NativeAllocationZeroGuid(const void* pointer) {
    if (_msize(const_cast<void*>(pointer)) < sizeof(GUID)) return 1;
    const auto* bytes = static_cast<const unsigned char*>(pointer);
    for (unsigned i = 0; i < sizeof(GUID); ++i) if (bytes[i]) return 2;
    return 0;
}
extern "C" __declspec(dllexport) void NativeAllocationFreeGuid(void* pointer) { delete static_cast<GUID*>(pointer); }
extern "C" __declspec(dllexport) void* NativeAllocationCreatePair() { return new NativePair{7,5000000002LL}; }
