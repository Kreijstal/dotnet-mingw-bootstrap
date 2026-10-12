// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
namespace Example {
public value struct BoxPoint {
    int X;
    BoxPoint(int value) { X = value; }
    void Move(int delta) { X += delta; }
};
public ref class Boxes abstract sealed {
public:
    static BoxPoint^ Make(int value) { return gcnew BoxPoint(value); }
    static int Read(BoxPoint^ value) { return value->X; }
    static void Move(BoxPoint^ value) { value->Move(2); }
    static long long FileTime(long long value) {
        DateTime^ date = DateTime::FromFileTimeUtc(value);
        return date->Ticks;
    }
};
}
int main() {
    Example::BoxPoint original(4);
    Example::BoxPoint^ box = original;
    original.X = 9;
    Console::WriteLine(box->X);
    box->Move(3);
    Console::WriteLine(box->X);
    Console::WriteLine(original.X);
    Example::BoxPoint^ alias = box;
    alias->X = 12;
    Console::WriteLine(box->X);
    GC::Collect();
    Console::WriteLine(box->X);
    Example::BoxPoint^ zero = gcnew Example::BoxPoint;
    Console::WriteLine(zero->X);
    Console::WriteLine(Example::Boxes::Read(Example::Boxes::Make(5)));
    Example::BoxPoint^ empty = nullptr;
    try { int unused = empty->X; }
    catch (NullReferenceException^ error) { Console::WriteLine("null box OK"); }
    Console::WriteLine(Example::Boxes::FileTime(0));
    return 0;
}
