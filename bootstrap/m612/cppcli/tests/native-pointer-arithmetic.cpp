// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
struct Pair { int Low; long long High; };
#pragma pack(push,1)
struct PackedSlot { unsigned char Tag; Pair* Pointer; };
#pragma pack(pop)
extern "C" int NativePointerCheck(const void*, const void*, const void*, long long);
public ref class PointerOwner { public: Pair* Pointer; };
long long Offset(int% order) { order=order*10+1; GC::Collect(); return 2; }
PointerOwner^ Receiver(PointerOwner^ owner, int% order) { order=order*10+2; GC::Collect(); return owner; }
int Index(int% order) { order=order*10+2; return 0; }
PackedSlot* Location(PackedSlot* slot, int% calls) { ++calls; return slot; }
Pair** Indirect(Pair** slot, int% calls) { ++calls; return slot; }
void Advance(Pair*& pointer) { pointer+=1; }
int ReadLow(const Pair& value) { return value.Low; }
int main() {
    Pair* values=new Pair[5]();
    Pair** slots=new Pair*[1]();
    PackedSlot* packed=new PackedSlot();
    try {
        Pair* middle=values+3;
        long long backwards=-2;
        Pair* earlier=middle+backwards;
        if(middle-values!=3 || values-middle!=-3 || earlier!=values+1 || 3+values!=middle) return 1;
        if(!(values<middle) || !(middle>values) || !(values<=values) || !(middle>=middle) || values>middle || middle<values) return 2;
        if(NativePointerCheck(values,middle,earlier,middle-values)!=0) return 3;
        PointerOwner^ owner=gcnew PointerOwner(); owner->Pointer=values;
        int order=0;
        if((Receiver(owner,order)->Pointer+=Offset(order))!=values+2 || order!=12 || owner->Pointer!=values+2) return 4;
        order=0;
        if((Receiver(owner,order)->Pointer-=Offset(order))!=values || order!=12 || owner->Pointer!=values) return 5;
        slots[0]=values; order=0;
        if((slots[Index(order)]+=Offset(order))!=values+2 || order!=12 || slots[0]!=values+2) return 6;
        packed->Pointer=values; int calls=0;
        if(Location(packed,calls)->Pointer++!=values || calls!=1 || packed->Pointer!=values+1) return 7;
        calls=0;
        if(++Location(packed,calls)->Pointer!=values+2 || calls!=1 || packed->Pointer!=values+2) return 8;
        calls=0;
        if(--*Indirect(slots,calls)!=values+1 || calls!=1 || slots[0]!=values+1) return 9;
        Pair* current=values;
        if(current++!=values || current!=values+1 || --current!=values || current!=values) return 10;
        current=values+3;
        if(current--!=values+3 || current!=values+2) return 11;
        Advance(current); if(current!=values+3) return 12;
        earlier->Low=42;
        if(values[1].Low!=42 || middle[-2].Low!=42 || ReadLow((const Pair&)*earlier)!=42) return 13;
        unsigned int positive=2;
        if(values+positive!=values+2 || (values+4)-positive!=values+2) return 14;
    } finally { delete packed; delete[] slots; delete[] values; }
    Console::WriteLine("native pointer strides, signed offsets/differences, ordering, updates and single evaluation OK");
    return 0;
}
