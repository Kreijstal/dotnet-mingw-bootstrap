// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "native-inline-arrays.hpp"
using namespace System;
InlineArrays* Receiver(InlineArrays* value,int% order) { order=order*10+2; return value; }
int Index(int% order) { order=order*10+3; return 1; }
unsigned int Rhs(int% order) { order=order*10+1; GC::Collect(); return 3; }
unsigned int* Range(InlineArrays* value) { return value->Range; }
int main() {
    InlineArrays* value=new InlineArrays();
    try {
        if(value->Range[0]!=0 || value->Wide[1]!=0 || value->Pointers[1]!=nullptr) return 1;
        value->Tag=7; value->Range[0]=10; value->Range[1]=20;
        int order=0;
        if((Receiver(value,order)->Range[Index(order)] += Rhs(order))!=23 || order!=123) return 2;
        value->Wide[1]=5000000002LL;
        value->Pairs[1].Low=42; value->Pairs[1].High=5000000004LL;
        value->Flags[1]=2;
        if(value->Flags[1]++!=2 || value->Flags[1]!=3) return 3;
        value->Pointers[0]=&value->Pairs[1];
        GC::Collect();
        if(Range(value)!=&value->Range[0] || Range(value)[1]!=23) return 4;
        if(NativeInlineArraysCheck(value)!=0) return 5;
        InlineArrays copy=*value;
        if(copy.Range[1]!=23 || copy.Pairs[1].High!=5000000004LL || NativeInlineArraysCheck(&copy)!=5) return 6;
        copy.Range[0]=11;
        if(value->Range[0]!=10 || copy.Range[0]!=11) return 7;
        if(sizeof(value->Range)!=8 || sizeof(value->Pairs)!=32) return 8;
    } finally { delete value; }
    Console::WriteLine("native inline arrays: packed offsets, scalar/pointer/POD strides, volatile updates, decay, copy, single evaluation and GC OK");
    return 0;
}
