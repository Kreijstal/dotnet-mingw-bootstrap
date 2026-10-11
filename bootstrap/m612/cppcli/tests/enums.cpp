using namespace System;
namespace Example {
public enum class Shade : unsigned char { Dark = 1, Bright = 2 };
public enum class Signed : long long { Negative = -5, Positive = 5000000002LL };
public ref class Enums abstract sealed {
public:
    static Shade Echo(Shade value) { return value; }
    static int Number(Shade value) { return static_cast<int>(value); }
    static DayOfWeek Day() { return DayOfWeek::Friday; }
};
}
int main() {
    Console::WriteLine(Example::Enums::Number(Example::Enums::Echo(Example::Shade::Bright)));
    Console::WriteLine(static_cast<long long>(Example::Signed::Negative));
    Console::WriteLine(static_cast<long long>(Example::Signed::Positive));
    Console::WriteLine(static_cast<int>(Example::Enums::Day()));
    array<Example::Shade>^ values = gcnew array<Example::Shade>(2);
    values[0] = Example::Shade::Dark;
    Console::WriteLine(static_cast<int>(values[0]));
    return 0;
}
