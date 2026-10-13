// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
namespace Example {
private ref class InternalState abstract sealed {
    static int _value = 40;
internal:
    static int Read() { return _value; }
    property static int Value { int get(); void set(int value); }
};
int InternalState::Value::get() { return _value; }
void InternalState::Value::set(int value) { _value = value; }
public ref class StaticProperties abstract sealed {
public:
    property static int Automatic;
    static property int Value {
        int get() { return InternalState::Value; }
        void set(int value) { InternalState::Value = value; }
    }
    static property String^ Text {
        String^ get() { return gcnew String("static text"); }
    }
    static int ReadInternal() { return InternalState::Read(); }
};
}
int main() {
    if(Example::StaticProperties::Automatic!=0) return 1;
    Example::StaticProperties::Automatic=42;
    if(Example::StaticProperties::Automatic!=42) return 2;
    Console::WriteLine(Example::StaticProperties::Value);
    Example::StaticProperties::Value = 42;
    Console::WriteLine(Example::StaticProperties::Value);
    Console::WriteLine(Example::StaticProperties::Text->Length);
    return 0;
}
