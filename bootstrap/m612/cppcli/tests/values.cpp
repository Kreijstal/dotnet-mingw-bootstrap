using namespace System;
namespace Example {
public value struct Point {
    int X;
    int Y;
    Point(int x, int y) : X(x), Y(y) {}
    int Sum() { return X + Y; }
    void Move(int dx) { X += dx; }
};
public ref class Holder {
public:
    Point Position;
    Holder() : Position(2, 3) {}
};
public ref class Values abstract sealed {
public:
    static Point Make(int x) { return Point(x, 4); }
    static Point Echo(Point p) { return p; }
    static int ByValue(Point p) { p.Move(10); return p.X; }
};
}
int main() {
    Example::Point p(3, 4);
    Console::WriteLine(p.Sum());
    Example::Point copy = p;
    copy.Move(2);
    Console::WriteLine(p.X);
    Console::WriteLine(copy.X);
    Console::WriteLine(Example::Values::ByValue(p));
    Console::WriteLine(p.X);
    p = Example::Values::Make(9);
    Console::WriteLine(p.Sum());
    Example::Holder^ h = gcnew Example::Holder();
    h->Position.Move(5);
    Console::WriteLine(h->Position.Sum());
    cli::array<Example::Point>^ a = gcnew cli::array<Example::Point>(2);
    a[0] = p;
    a[0].Move(3);
    Console::WriteLine(a[0].X);
    Console::WriteLine(p.X);
    Example::Point zero;
    Console::WriteLine(zero.Sum());
    Object^ boxed = safe_cast<Object^>(p);
    p.X = 20;
    Example::Point unboxed = safe_cast<Example::Point>(boxed);
    Console::WriteLine(unboxed.X);
    Object^ number = safe_cast<Object^>(42);
    Console::WriteLine(safe_cast<int>(number));
    return 0;
}
