// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
public enum class SmallFlags : unsigned char { None=0, Low=1, High=128 };
public ref class FlagOwner {
public: int Flags;
};
// Global storage is intentionally avoided by passing observable state byref.
int Rhs(int% order) { order = order*10+1; return 2; }
FlagOwner^ Receiver(FlagOwner^ owner, int% order) { order = order*10+2; return owner; }
int Index(int% order) { order = order*10+2; return 0; }
int* Pointer(int* value, int% calls) { ++calls; return value; }
int main() {
    FlagOwner^ owner=gcnew FlagOwner();
    owner->Flags=1;
    int order=0;
    int changed=(Receiver(owner,order)->Flags |= Rhs(order));
    if(order!=12||changed!=3||owner->Flags!=3) return 1;
    array<Byte>^ bytes=gcnew array<Byte>(1);
    bytes[0]=255; order=0;
    int narrowed=(bytes[Index(order)] += Rhs(order));
    if(order!=12||narrowed!=1||bytes[0]!=1) return 2;
    SmallFlags flags=SmallFlags::Low;
    flags |= SmallFlags::High;
    flags ^= SmallFlags::Low;
    flags &= SmallFlags::High;
    if(static_cast<int>(flags)!=128) return 3;
    unsigned int unsignedValue=0x80000000U;
    unsignedValue >>= 31;
    if(unsignedValue!=1) return 4;
    unsignedValue=0xffffffffU; unsignedValue /= 2;
    if(unsignedValue!=0x7fffffffU) return 5;
    unsignedValue %= 3; if(unsignedValue!=1) return 6;
    int signedValue=-16; signedValue >>= 2; signedValue /= 2; signedValue %= 3;
    if(signedValue!=-2) return 7;
    unsigned char low=255; low += 2; low <<= 7;
    if(low!=128) return 8;
    float real=1.5f; real += 2.25; real *= 2; real /= 3;
    if(real!=2.5f) return 9;
    bool truth=false; truth |= 2; truth ^= true;
    if(truth) return 10;
    int value=1,calls=0;
    int indirect=(*Pointer(&value,calls) |= 2);
    if(value!=3||indirect!=3||calls!=1) return 11;
    unsigned int high=0xffffffffU;
    long long total=1; total+=high;
    if(total!=4294967296LL) return 12;
    long long divisor=2;
    high/=divisor;
    if(high!=0x7fffffffU) return 13;
    unsigned long long unsignedDivisor=2;
    int negative=-16; negative/=unsignedDivisor;
    if(negative!=-8) return 14;
    unsigned int floating=0x80000000U; floating/=2.0;
    if(floating!=0x40000000U) return 15;
    double floatingTotal=0; high=0x80000000U; floatingTotal+=high;
    if(floatingTotal!=2147483648.0) return 16;
    unsigned long long shifted=0x10000000000ULL; long long shift=40;
    shifted>>=shift; if(shifted!=1) return 17;
    int subtract=7; subtract-=2; if(subtract!=5) return 18;
    if(safe_cast<long long>(high)!=2147483648LL || safe_cast<double>(high)!=2147483648.0) return 19;
    int minusOne=-1;
    if(static_cast<unsigned long long>(minusOne)!=0xffffffffffffffffULL ||
       safe_cast<unsigned long long>(minusOne)!=0xffffffffffffffffULL) return 20;
    Console::WriteLine("compound flags, promotion/narrowing, signedness, floats and single RHS/receiver/index/pointer evaluation OK");
    return 0;
}
