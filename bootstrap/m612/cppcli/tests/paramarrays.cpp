// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#using "paramarray-fixture.dll"
using namespace System;
public ref class ParamArrayDerived : ParamArrayFixture {
public:
    ParamArrayDerived() : ParamArrayFixture() {}
    virtual int Virtual(array<int>^ values) override { return values->Length + 40; }
};
int main() {
    Console::WriteLine(ParamArrayFixture::Objects());
    Console::WriteLine(ParamArrayFixture::Objects(nullptr));
    Console::WriteLine(ParamArrayFixture::Numbers(17));
    array<int>^ values = gcnew array<int>(2); values[0] = 7; values[1] = 9;
    Console::WriteLine(ParamArrayFixture::Numbers(17, values));
    ParamArrayFixture^ fixture = gcnew ParamArrayFixture();
    Console::WriteLine(fixture->InitialCount);
    Console::WriteLine(fixture->Instance(29));
    fixture = gcnew ParamArrayFixture(values);
    Console::WriteLine(fixture->InitialCount);
    Console::WriteLine(ParamArrayFixture::Fresh());
    Console::WriteLine(ParamArrayFixture::Fresh());
    array<Object^>^ supplied = gcnew array<Object^>(0);
    ParamArrayFixture::Fresh(supplied);
    Console::WriteLine(ParamArrayFixture::IsPrevious(supplied));
    Console::WriteLine(ParamArrayFixture::Text("prefix"));
    ParamArrayFixture^ derived = gcnew ParamArrayDerived();
    Console::WriteLine(derived->Virtual());
    Console::WriteLine(ParamArrayFixture::Overload("text"));
    Console::WriteLine(ParamArrayFixture::Overload("text", supplied));
    Console::WriteLine(ParamArrayFixture::Better("text"));
    return 0;
}
