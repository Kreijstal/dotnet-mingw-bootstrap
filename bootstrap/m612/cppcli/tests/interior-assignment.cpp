// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
public ref class InteriorAssignment abstract sealed {
public:
    static void Rebind(interior_ptr<int> pointer,array<int>^ other) {
        pointer=&other[1]; GC::Collect(); *pointer=41;
    }
    static int Alias(array<int>^ first,array<int>^ second) {
        interior_ptr<int> pointer=&first[0];
        interior_ptr<int> alias=nullptr;
        alias=pointer; pointer=&second[1];
        GC::Collect(); *alias=40; *pointer=42;
        Rebind(pointer,first);
        return *pointer;
    }
    static unsigned short Chars(array<wchar_t>^ values) {
        interior_ptr<unsigned short> pointer=nullptr;
        pointer=(interior_ptr<unsigned short>)&values[0];
        GC::Collect(); pointer[1]=0x03bb;
        return pointer[1];
    }
};
