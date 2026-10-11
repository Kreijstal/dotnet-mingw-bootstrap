using namespace System;
namespace Example {
public value struct Coordinate { int X; Coordinate(int x) : X(x) {} void Move() { ++X; } };
public ref class Tracking abstract sealed {
public:
    static void Change(int% n) { n += 3; }
    static void Move(Coordinate% p) { p.Move(); }
    static void Replace(Object^% o) { o = gcnew String("replaced"); }
};
}
int main() {
    int n = 5;
    int% alias = n;
    Example::Tracking::Change(alias);
    Console::WriteLine(n);
    alias = 10;
    Console::WriteLine(n);
    cli::array<int>^ a = gcnew cli::array<int>(2);
    a[0] = 20;
    int% element = a[0];
    System::GC::Collect();
    Example::Tracking::Change(element);
    Console::WriteLine(a[0]);
    Example::Coordinate p(4);
    Example::Tracking::Move(p);
    Console::WriteLine(p.X);
    int% field = p.X;
    ++field;
    Console::WriteLine(p.X);
    Object^ o = nullptr;
    Example::Tracking::Replace(o);
    Console::WriteLine(o != nullptr);
    Console::WriteLine(17 % 5);
    return 0;
}
