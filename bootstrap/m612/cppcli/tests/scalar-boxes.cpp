// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
public enum class ScalarFlags : unsigned int { High=0x80000000, Low=1 };
using ScalarAlias=Int32;
typedef ScalarFlags FlagAlias;
typedef Int32^ BoxAlias;
namespace Qualified { using Count=Int32; }
static_assert(sizeof(Int32^)==sizeof(void*), "managed handle size");
static_assert(alignof(Int32^)==alignof(void*), "managed handle alignment");
public ref class ScalarBoxSlot {
public:
    Int32^ Value;
    property Int32^ Current;
    ScalarBoxSlot(Int32^ value) { Value=value; Current=value; }
};
public ref class ScalarBoxes abstract sealed {
public:
    static int Identity(int value) {
        Int32^ box=value;
        Int32^ alias=box;
        *alias=42;
        GC::Collect();
        return *box;
    }
    static Object^ Make(int value) { Int32^ box=value; return box; }
    static int Read(Object^ value) { Int32^ box=static_cast<Int32^>(value); return *box; }
    static Object^ Enum(ScalarFlags value) { ScalarFlags^ box=value; return box; }
    static ScalarFlags ReadEnum(Object^ value) { ScalarFlags^ box=(ScalarFlags^)value; return *box; }
    static bool Null() { Int32^ box=nullptr; return box==nullptr; }
    static bool Truth(int value) { Int32^ box=value; return box ? true : false; }
    static int Allocate(int value) { Int32^ box=gcnew Int32(value); return *box; }
    static int AllocateZero() { Int32^ box=gcnew Int32; return *box; }
    static bool Dynamic(Object^ value) { Int32^ box=dynamic_cast<Int32^>(value); return box!=nullptr; }
    static Int32^ Echo(Int32^ value) { return value; }
    static Object^ SafeObject(Int32^ value) { return safe_cast<Object^>(value); }
    static int SafeValue(Int32^ value) { return safe_cast<int>(value); }
    static bool Same(Int32^ left, Int32^ right) { return left==right; }
    static ScalarFlags^ EchoEnum(ScalarFlags^ value) { return value; }
    static Object^ Bool(bool value) { Boolean^ box=value; return box; }
    static Object^ Byte(unsigned char value) { System::Byte^ box=value; return box; }
    static Object^ SignedByte(signed char value) { SByte^ box=value; return box; }
    static Object^ Short(short value) { Int16^ box=value; return box; }
    static Object^ UnsignedShort(unsigned short value) { UInt16^ box=value; return box; }
    static Object^ Unsigned(unsigned int value) { UInt32^ box=value; return box; }
    static Object^ Long(long long value) { Int64^ box=value; return box; }
    static Object^ UnsignedLong(unsigned long long value) { UInt64^ box=value; return box; }
    static Object^ Char(wchar_t value) { System::Char^ box=value; return box; }
    static Object^ Float(float value) { Single^ box=value; return box; }
    static Object^ Double(double value) { System::Double^ box=value; return box; }
    static String^ Name(int value) { Int32^ box=value; return box->ToString(); }
    static int Receivers;
    static int HandleSize() { return sizeof(Int32^); }
    static Int32^ Next(Int32^ value) { ++Receivers; return value; }
    static int Delta() { GC::Collect(); return 2; }
    static int Updates() {
        Int32^ box=5;
        *Next(box)+=Delta();
        int old=(*Next(box))++;
        int now=++(*Next(box));
        return old*100+now*10+*box;
    }
    static int Aliases() {
        ScalarAlias^ first=5;
        Qualified::Count^ second=first;
        FlagAlias^ flag=ScalarFlags::High;
        Int32% tracked=*second;
        GC::Collect();
        tracked=23;
        first=second;
        BoxAlias preserved=first;
        if (*preserved!=23) return -1;
        *preserved+=2;
        if (*second!=25) return -1;
        *first=23;
        return *first + static_cast<int>(*flag==ScalarFlags::High);
    }
    static int Mixed() { int^ box=4, scalar=5; int* pointer=&scalar; *pointer=6; return *box+scalar; }
    static int Xor(int right) { int Int32=9; return Int32 ^ right; }
    static Object^ Slot(int value) { ScalarBoxSlot^ slot=gcnew ScalarBoxSlot(value); return slot->Current; }
    static int Native(int value) { int* pointer=&value; *pointer=17; return value; }
};
int main() {
    Console::WriteLine(ScalarBoxes::Identity(5));
    Console::WriteLine(ScalarBoxes::Read(ScalarBoxes::Make(7)));
    Console::WriteLine(static_cast<unsigned int>(ScalarBoxes::ReadEnum(ScalarBoxes::Enum(ScalarFlags::High))));
    Console::WriteLine(ScalarBoxes::Null());
    Console::WriteLine(ScalarBoxes::Truth(0));
    Console::WriteLine(ScalarBoxes::Native(0));
    Console::WriteLine(ScalarBoxes::Allocate(19));
    Console::WriteLine(ScalarBoxes::AllocateZero());
    Console::WriteLine(ScalarBoxes::Dynamic(ScalarBoxes::Make(0)));
    Console::WriteLine(ScalarBoxes::Dynamic(gcnew Object()));
    Console::WriteLine(ScalarBoxes::Aliases());
    Console::WriteLine(ScalarBoxes::Mixed());
    Console::WriteLine(ScalarBoxes::Xor(3));
    try { ScalarBoxes::Read(nullptr); }
    catch(NullReferenceException^ error) { Console::WriteLine("null box OK"); }
    try { ScalarBoxes::Read(gcnew Object()); }
    catch(InvalidCastException^ error) { Console::WriteLine("wrong box OK"); }
    return 0;
}
