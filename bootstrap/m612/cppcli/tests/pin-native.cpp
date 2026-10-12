// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include <vcclr.h>
using namespace System;
int ReadNative(const unsigned int* input, unsigned int* output) {
    const pin_ptr<const unsigned int> source = input;
    pin_ptr<unsigned int> target = output;
    GC::Collect();
    *target = *source + 2;
    return *target;
}
interior_ptr<unsigned int> Element(array<unsigned int>^ values, int% calls) {
    ++calls;
    return &values[0];
}
int main() {
    unsigned int* values = new unsigned int[2]();
    try {
        values[0] = 40;
        if (ReadNative(&values[0], &values[1]) != 42 || values[1] != 42) return 1;
        pin_ptr<unsigned int> pin = values;
        GC::Collect();
        if (*pin != 40) return 2;
        pin = &values[1];
        GC::Collect();
        if (*pin != 42) return 3;
        array<unsigned int>^ managed = gcnew array<unsigned int>(1);
        managed[0] = 100;
        pin = &managed[0];
        GC::Collect();
        *pin += 7;
        if (managed[0] != 107) return 4;
        int calls=0;
        if ((*Element(managed,calls))++ != 107 || calls != 1 || managed[0] != 108) return 6;
        calls=0;
        if ((*Element(managed,calls) += 2) != 110 || calls != 1 || managed[0] != 110) return 7;
        pin = values;
        GC::Collect();
        *pin = 17;
        if (values[0] != 17 || managed[0] != 110) return 5;
        pin = nullptr;
    } finally { delete[] values; }
    Console::WriteLine("native pointer pin initialization, const parameters, managed/native reassignment and GC OK");
    return 0;
}
