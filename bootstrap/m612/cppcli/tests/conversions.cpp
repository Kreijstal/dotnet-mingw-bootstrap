using namespace System;
public ref class RefNumber {
public:
    int Value;
    RefNumber(int value):Value(value) {}
    static operator int(RefNumber^ value) { return value!=nullptr ? value->Value : -1; }
    static operator String^(RefNumber^ value) { return gcnew String("number"); }
    static explicit operator RefNumber^(int value) { return gcnew RefNumber(value); }
};
public value struct ValueNumber {
    int Value;
    ValueNumber(int value):Value(value) {}
    static operator ValueNumber(int value) { return ValueNumber(value); }
    static explicit operator int(ValueNumber value) { return value.Value; }
};
public ref class ExternalNumber {
public:
    int Value;
    ExternalNumber(int value):Value(value) {}
    static operator int(ExternalNumber^ value);
    static explicit operator ExternalNumber^(int value);
};
ExternalNumber::operator int(ExternalNumber^ other) { return other->Value; }
ExternalNumber::operator ExternalNumber^(int other) { return gcnew ExternalNumber(other); }
public ref class TrackingNumber {
public:
    int Value;
    TrackingNumber(int value):Value(value) {}
    static operator int(TrackingNumber% value) { return value.Value; }
};
