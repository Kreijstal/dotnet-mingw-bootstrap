// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
using Mixed = double (*)(float,double,unsigned long long,int,unsigned short);
using Reference = int (*)(int&);
using Output = void (*)(int*);
extern "C" void* NativeFunction(int);
Mixed SelectFunction(Mixed pointer, int% calls) { ++calls; return pointer; }
public ref class FunctionHolder {
    Mixed _pointer;
public:
    FunctionHolder(Mixed pointer) { _pointer = pointer; }
    double Invoke() { return _pointer(1.5f,2.25,0xf123456789abcdefULL,4,6); }
    static Mixed Echo(Mixed pointer) { return pointer; }
};
int main() {
    Mixed pointer = reinterpret_cast<Mixed>(NativeFunction(0));
    int calls = 0;
    if ((*SelectFunction(pointer,calls))(1.5f,2.25,0xf123456789abcdefULL,4,6) != 13.75 || calls != 1) return 1;
    FunctionHolder^ holder = gcnew FunctionHolder(FunctionHolder::Echo(pointer));
    GC::Collect();
    if (holder->Invoke() != 13.75) return 2;
    Reference modify = reinterpret_cast<Reference>(NativeFunction(1));
    int value = 5;
    if (modify(value) != 12 || value != 12) return 3;
    Output write = reinterpret_cast<Output>(NativeFunction(2));
    write(&value);
    if (value != 42) return 4;
    Console::WriteLine("native function pointer storage, returns, single callee evaluation, mixed ABI, references and outputs OK");
    return 0;
}
