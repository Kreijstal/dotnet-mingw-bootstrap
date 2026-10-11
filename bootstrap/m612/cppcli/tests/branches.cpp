using namespace System;
public ref class Box {
public:
    int count;
    Box() : count(0) {}
};
public ref class Program abstract sealed {
public:
    static bool Touch(Box^ box) { ++box->count; return true; }
    static void Run() {
        Box^ box = gcnew Box();
        bool a = false && Touch(box);
        bool b = true || Touch(box);
        bool c = false || Touch(box);
        Console::WriteLine(a);
        Console::WriteLine(b);
        Console::WriteLine(c);
        Console::WriteLine(box->count);
        Console::WriteLine(10 + (b ? 2 : 3));
        Console::WriteLine(20 + (a ? 4 : 5));
    }
};
int main() { Program::Run(); return 0; }
