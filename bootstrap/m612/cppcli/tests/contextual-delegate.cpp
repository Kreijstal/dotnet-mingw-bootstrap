// SPDX-License-Identifier: MIT
using namespace System;
public delegate int Mapper(int value);
public delegate System::String^ TextMapper(System::String^ text);
int PlusThree(int value) { return value+3; }
String^ Echo(String^ text) { return text; }
public ref class Holder {
public:
    Mapper^ delegate;
    Holder(Mapper^ value):delegate(value) {}
    int Call(int value) { return delegate(value); }
};
int Pass(Mapper^ delegate,int value) {
    Mapper^ saved=delegate;
    delegate=saved;
    return delegate(value);
}
bool IsPresent(MulticastDelegate^ delegate) { return delegate!=nullptr; }
int delegate(int value) { return value+1; }
int main() {
    Mapper^ value=gcnew Mapper(PlusThree);
    Mapper^ delegate=value;
    Holder^ holder=gcnew Holder(delegate);
    TextMapper^ text=gcnew TextMapper(Echo);
    String^ input=String::Empty;
    if(Pass(delegate,39)!=42 || holder->Call(39)!=42 || holder->delegate(39)!=42 || !IsPresent(delegate) || ::delegate(41)!=42 || text(input)!=input) return 1;
    Console::WriteLine("contextual delegate declarations, WPF-style parameter/field identifiers and delegate invocation PASS");
    return 0;
}
