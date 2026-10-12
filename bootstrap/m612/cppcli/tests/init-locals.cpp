// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
enum NativeChoice { Nonzero = 3 };
public ref class InitializedLocals abstract sealed {
public:
    static long long Zero() {
        int number;
        double real;
        long long wide;
        int* pointer;
        NativeChoice choice;
        if (number != 0 || real != 0.0 || wide != 0 || pointer != nullptr || choice != 0) return -1;
        return wide;
    }
    static int Scope() {
        int total = 0;
        for (int i = 0; i < 3; ++i) { int retained; ++retained; total += retained; }
        return total;
    }
};
int main() {
    Console::WriteLine(InitializedLocals::Zero());
    Console::WriteLine(InitializedLocals::Scope());
    return 0;
}
