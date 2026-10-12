// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
struct NativePair { int Low; long long High; };
extern "C" int NativeArrayCheck(const void*, unsigned int);
int Count(int% calls) { ++calls; return 3; }
int Slot(int% calls) { ++calls; return 1; }
int main() {
    int calls=0;
    NativePair* pairs=new NativePair[Count(calls)]();
    if(calls!=1||pairs[1].Low!=0||pairs[1].High!=0) return 1;
    for(int i=0;i<3;++i) { pairs[i].Low=i+1; pairs[i].High=5000000002LL+i; }
    GC::Collect();
    if(NativeArrayCheck(pairs,3)!=0) return 2;
    long long* values=new long long[3]();
    values[1]=5; calls=0;
    if((values[Slot(calls)] |= 2)!=7||calls!=1) return 3;
    calls=0; if(values[Slot(calls)]++!=7||calls!=1||values[1]!=8) return 4;
    long long* address=&values[1]; *address=5000000002LL;
    if(values[1]!=5000000002LL) return 5;
    void** pointers=new void*[2]();
    if(pointers[0]!=nullptr||pointers[1]!=nullptr) return 6;
    pointers[1]=pairs; if(pointers[1]!=pairs) return 7;
    delete[] pointers; delete[] values; delete[] pairs;
    int* empty=new int[0]; delete[] empty;
    int invalidCount=-1;
    bool rejected=false;
    try { int* invalid=new int[invalidCount]; delete[] invalid; }
    catch(OverflowException^) { rejected=true; }
    if(!rejected) return 8;
    Console::WriteLine("native array allocation, zeroing, pointer/POD stride, address/store/updates, GC and delete[] OK");
    return 0;
}
