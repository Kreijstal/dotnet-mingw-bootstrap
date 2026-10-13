// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
public ref class InteriorSnapshot {
public:
    array<Byte>^ Bytes;
    InteriorSnapshot(const interior_ptr<Byte> data,int size) {
        Bytes=gcnew array<Byte>(size);
        GC::Collect();
        for(int index=0;index<size;++index) Bytes[index]=data[index];
    }
};
public ref class InteriorIndexing abstract sealed {
public:
    static int Indices;
    static long long Index(long long value) { ++Indices; GC::Collect(); return value; }
    static int Operand(int value) { GC::Collect(); return value; }
    static int Read(const interior_ptr<int> data,long long index) { GC::Collect(); return data[index]; }
    static void Write(interior_ptr<int> data,long long index,int value) { data[Index(index)]=Operand(value); }
    static int Update(interior_ptr<int> data,long long index) {
        data[Index(index)]+=Operand(2);
        int old=data[Index(index)]++;
        int now=++data[Index(index)];
        return old*100+now*10+data[index];
    }
    static long long Wide(interior_ptr<long long> data,long long index) { return data[Index(index)]; }
    static double Floating(interior_ptr<double> data,long long index) { return data[Index(index)]; }
    static int Alias(array<int>^ values) {
        interior_ptr<int> data=&values[0];
        int% alias=data[1];
        GC::Collect();
        alias=Operand(31);
        return data[1];
    }
    static int Pinned(array<int>^ values) { pin_ptr<int> data=&values[0]; data[Index(1)]+=2; return data[1]; }
    static int ByteWrap(interior_ptr<Byte> data) { Byte old=data[0]++; return old*1000+data[0]; }
};
