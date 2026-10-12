// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include <windows.h>
using namespace System;
struct NativePair { int Low; long long High; };
extern "C" int NativeAllocationCheck(const void*);
extern "C" int NativeAllocationZeroGuid(const void*);
extern "C" void NativeAllocationFreeGuid(void*);
extern "C" void* NativeAllocationCreatePair();
int FailInitialization() { throw gcnew InvalidOperationException(); }
int main() {
    NativePair* value = new NativePair{7, 5000000002LL};
    GC::Collect();
    if (value->Low != 7 || value->High != 5000000002LL || NativeAllocationCheck(value) != 0) return 1;
    delete value;
    _GUID* guid = new _GUID();
    if (NativeAllocationZeroGuid(guid) != 0) return 2;
    NativeAllocationFreeGuid(guid); // Native C++ delete consumes the CLR/UCRT allocation.
    NativePair* native = static_cast<NativePair*>(NativeAllocationCreatePair());
    if (native->Low != 7 || native->High != 5000000002LL) return 3;
    delete native; // CLR/UCRT free consumes native C++ new.
    void* erased = NativeAllocationCreatePair();
    delete erased; // Matches WPF's erased trivial-record cleanup.
    void* missing = nullptr;
    delete missing;
    double* real = new double();
    if (*real != 0.0) return 4;
    delete real;
    int* number = new int(42);
    if (*number != 42) return 5;
    delete number;
    bool caught = false;
    try { NativePair* failed = new NativePair{FailInitialization(), 0}; delete failed; }
    catch (InvalidOperationException^) { caught = true; }
    if (!caught) return 6;
    Console::WriteLine("native scalar/POD allocation, UCRT ownership transfer, erased/null deletion, GC and initializer exceptions OK");
    return 0;
}
