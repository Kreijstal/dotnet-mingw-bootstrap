// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
namespace EnumScope {
public ref class Host {
public:
    [Flags] enum class Access : unsigned short { Read=1, Write=2, All=3 };
    enum struct Wide : unsigned long long { High=0x8000000000000000ULL, Max=0xffffffffffffffffULL };
    ref class Child {
    public:
        enum class Access : long long { Negative=-5000000002LL, Positive=5000000002LL };
        static Type^ Token() { return Access::typeid; }
    };
    static Access Round(Access value) { return value; }
    static Object^ Box(Access value) { return value; }
    static Access Unbox(Object^ value) { return *(Access^)value; }
    static Array^ Values() { return Enum::GetValues(Access::typeid); }
    static Access Parse(String^ name) { return *((Access^)Enum::Parse(Access::typeid,name,false)); }
    static Type^ Token() { return Access::typeid; }
protected:
    enum class Protected { Value=17 };
private:
    enum class Hidden { Value=19 };
internal:
    enum class Assembly { Value=23 };
public:
    static Type^ ProtectedToken() { return Protected::typeid; }
    static Type^ HiddenToken() { return Hidden::typeid; }
    static Type^ AssemblyToken() { return Assembly::typeid; }
};
private ref class InternalHost {
public:
    enum class PrintPropertyTypeInterop { StringPrintType=1, Int32PrintType=2, DataTimePrintType=5, ByteBufferPrintType=10 };
    static Array^ Values() { return Enum::GetValues(PrintPropertyTypeInterop::typeid); }
};
public ref class Expose abstract sealed {
public:
    static Array^ PrintingValues() { return InternalHost::Values(); }
};
}
