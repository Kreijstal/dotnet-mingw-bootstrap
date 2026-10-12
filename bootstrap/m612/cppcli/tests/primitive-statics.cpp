// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
namespace Sys = System;
namespace NativeShadow {
    enum class Int32 { MaxValue = 17 };
    int Read() { return (int)Int32::MaxValue; }
}
namespace NativeClassShadow {
    class Int32 { public: enum { MaxValue = 18 }; };
    int ReadClass() { return Int32::MaxValue; }
}
public ref class PrimitiveStatics abstract sealed {
public:
    static double Minimum() { return System::Double::MinValue; }
    static double Maximum() { return Double::MaxValue; }
    static double Epsilon() { return Sys::Double::Epsilon; }
    static double Nan() { return Double::NaN; }
    static double PositiveInfinity() { return Double::PositiveInfinity; }
    static double NegativeInfinity() { return Double::NegativeInfinity; }
    static float FloatMinimum() { return Single::MinValue; }
    static float FloatEpsilon() { return Single::Epsilon; }
    static float FloatNan() { return Single::NaN; }
    static int IntMinimum() { return Int32::MinValue; }
    static int IntMaximum() { return Sys::Int32::MaxValue; }
    static unsigned int UIntMaximum() { return UInt32::MaxValue; }
    static long long LongMinimum() { return Int64::MinValue; }
    static unsigned long long ULongMaximum() { return UInt64::MaxValue; }
    static int SmallMinimum() { return SByte::MinValue + Int16::MinValue; }
    static int SmallMaximum() { return Byte::MaxValue + UInt16::MaxValue; }
    static int NativeLookup() { return NativeShadow::Read(); }
    static int NativeClassLookup() { return NativeClassShadow::ReadClass(); }
    static wchar_t CharacterMaximum() { return Char::MaxValue; }
    static bool Parse(String^ value, double% result) {
        return System::Double::TryParse(value, System::Globalization::NumberStyles::Float,
            System::Globalization::CultureInfo::InvariantCulture, result);
    }
    static bool Finite(double value) { return Double::IsFinite(value); }
    static bool ParseInteger(String^ value, int% result) { return Int32::TryParse(value, result); }
};
