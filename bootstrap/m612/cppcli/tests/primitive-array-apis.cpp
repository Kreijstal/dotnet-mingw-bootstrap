// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
public enum class ParsedStatus : int { Ready=1, Error=-1 };
public value struct BoxData { int Value; BoxData(int value):Value(value) {} };
using CountAlias=System::Int32;
namespace Shadow {
    using Int32=double;
    public ref class Record { public: int CompareTo(int value) { return value+3; } };
}
public ref class OverloadSlots {
public:
    Object^ Stored;
    int SetValue(String^ name,unsigned int index,Object^ value) { Stored=value; return 1; }
    int SetValue(String^ name,unsigned int index,Type^ value) { return 2; }
    int Typed(Object^ value) { return 1; }
    int Typed(int value) { return 2; }
};
public ref class PrimitiveArrayApis abstract sealed {
public:
    static int Receivers;
    static int Operand(int value) { ++Receivers; return value; }
    static String^ Format(int value,Globalization::CultureInfo^ culture) { return Operand(value).ToString(culture); }
    static String^ FormatDouble(double value,String^ format,Globalization::CultureInfo^ culture) { return value.ToString(format,culture); }
    static String^ FormatLong(long long value,Globalization::CultureInfo^ culture) { return value.ToString(culture); }
    static String^ FormatAlias(CountAlias value,Globalization::CultureInfo^ culture) { return value.ToString(culture); }
    static String^ FormatShadow(Shadow::Int32 value,Globalization::CultureInfo^ culture) { return value.ToString(culture); }
    static int RecordShadow(int value) { Shadow::Record^ record=gcnew Shadow::Record(); return record->CompareTo(value); }
    static String^ Boolean(bool value) { return value.ToString(); }
    static String^ Byte(unsigned char value,Globalization::CultureInfo^ culture) { return value.ToString(culture); }
    static String^ SignedByte(signed char value,Globalization::CultureInfo^ culture) { return value.ToString(culture); }
    static String^ Short(short value,Globalization::CultureInfo^ culture) { return value.ToString(culture); }
    static String^ UnsignedShort(unsigned short value,Globalization::CultureInfo^ culture) { return value.ToString(culture); }
    static String^ Unsigned(unsigned int value,Globalization::CultureInfo^ culture) { return value.ToString(culture); }
    static String^ UnsignedLong(unsigned long long value,Globalization::CultureInfo^ culture) { return value.ToString(culture); }
    static String^ Char(wchar_t value) { return value.ToString(); }
    static String^ Float(float value,Globalization::CultureInfo^ culture) { return value.ToString(culture); }
    static bool Equal(int left,int right) { return left.Equals(right); }
    static bool EqualObject(int left,Object^ right) { return left.Equals(right); }
    static int Compare(int left,int right) { return left.CompareTo(right); }
    static int Hash(int value) { return value.GetHashCode(); }
    static String^ TypeName(int value) { return value.GetType()->FullName; }
    static ParsedStatus Status(int value,Globalization::CultureInfo^ culture) {
        ParsedStatus status=*(ParsedStatus^)Enum::Parse(ParsedStatus::typeid,value.ToString(culture));
        return status;
    }
    static Collections::IEnumerator^ Enumerate(array<String^>^ values) { return values->GetEnumerator(); }
    static void WriteInt(array<int>^ values,int index,int value) { values->SetValue(value,index); }
    static void WriteString(array<String^>^ values,int index,String^ value) { values->SetValue(value,index); }
    static void WriteObject(array<String^>^ values,int index,Object^ value) { values->SetValue(value,index); }
    static Object^ Read(array<int>^ values,int index) { return values->GetValue(index); }
    static int Length(array<int>^ values,int dimension) { return values->GetLength(dimension); }
    static int Rank(array<int>^ values) { return values->Rank; }
    static int Lower(array<int>^ values,int dimension) { return values->GetLowerBound(dimension); }
    static int Upper(array<int>^ values,int dimension) { return values->GetUpperBound(dimension); }
    static Object^ Clone(array<int>^ values) { return values->Clone(); }
    static int PrintingOverload(OverloadSlots^ slots,int value) { return slots->SetValue("EscapeCode",0,Int32(value)); }
    static int TypedOverload(OverloadSlots^ slots,int value) { return slots->Typed(value); }
    static int NullOverload(OverloadSlots^ slots) { return slots->SetValue("EscapeCode",0,static_cast<Type^>(nullptr)); }
    static BoxData^ Percent(int value) { BoxData original(value); BoxData^ box=%original; original.Value=99; return box; }
    static Object^ PercentObject(int value) { BoxData original(value); return (Object^)(%original); }
    static String^ LocalLiteral() { String^ value="Attributes"; return value; }
    static Object^ LocalNumber(int value) { Object^ box=value; return box; }
    static Object^ LocalZero() { Object^ value=0; return value; }
    static int NativeRemainder(int value) { const char* text="native"; return value%5+text[0]; }
};
