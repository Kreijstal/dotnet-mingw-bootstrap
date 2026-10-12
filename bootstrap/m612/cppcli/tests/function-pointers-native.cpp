// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include <cstdint>
static double Mixed(float a, double b, uint64_t c, int d, unsigned short e) { return c == UINT64_C(0xf123456789abcdef) ? a+b+d+e : -1.0; }
static int Reference(int& value) { value += 7; return value; }
static void Output(int* value) { *value = 42; }
extern "C" __declspec(dllexport) void* NativeFunction(int index) {
    if (index == 0) return reinterpret_cast<void*>(&Mixed);
    if (index == 1) return reinterpret_cast<void*>(&Reference);
    return reinterpret_cast<void*>(&Output);
}
