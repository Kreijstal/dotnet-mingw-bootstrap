#include "native.hpp"
using namespace System;
public ref class NativeBridge abstract sealed {
public:
    static int Sum(int left, int right) { return NativeSum(left, right); }
    static long long Scale(long long value) { return NativeScale(value); }
    static double Half(double value) { return NativeHalf(value); }
};
int main() {
    Console::WriteLine(NativeBridge::Sum(7, 6));
    Console::WriteLine(NativeBridge::Scale(5000000002LL));
    Console::WriteLine(NativeBridge::Half(7.0) == 3.5);
    return 0;
}
