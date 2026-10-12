// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
struct Point { int X; long long Y; };
extern "C" __declspec(dllexport) int NativePoint(Point* value) {
    if (value->X != 12 || value->Y != 99) return 0;
    value->Y = 1234567890123LL;
    return 42;
}
