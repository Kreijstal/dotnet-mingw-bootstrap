// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
using namespace System::Collections::Generic;
public ref class NestedIndexers abstract sealed {
    static int _receiverCalls;
    static int _outerCalls;
    static int _innerCalls;
    static List<array<unsigned int>^>^ Source(List<array<unsigned int>^>^ value) { ++_receiverCalls; return value; }
    static int Outer() { ++_outerCalls; return 0; }
    static int Inner() { ++_innerCalls; return 1; }
public:
    static unsigned int Check() {
        List<array<unsigned int>^>^ ranges = gcnew List<array<unsigned int>^>();
        array<unsigned int>^ row = gcnew array<unsigned int>(2); row[1] = 17; ranges->Add(row);
        Source(ranges)[Outer()][Inner()] += 25;
        unsigned int result = ranges[0][1];
        if (_receiverCalls != 1 || _outerCalls != 1 || _innerCalls != 1) return 0;
        return result;
    }
};
int main() { Console::WriteLine(NestedIndexers::Check()); return 0; }
