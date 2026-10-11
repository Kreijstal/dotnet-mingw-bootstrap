using namespace System;
public ref class Box {
public:
    int value;
    Box(int initial) : value(initial) {}
};
public ref class Program abstract sealed {
public:
    static Box^ Get(Box^ box) { Console::WriteLine("receiver"); return box; }
    static void Run() {
        Box^ box = gcnew Box(1);
        Console::WriteLine(Get(box)->value += 2);
        Console::WriteLine(Get(box)->value++);
        Console::WriteLine(++Get(box)->value);
        Console::WriteLine(box->value);
    }
};
int main() { Program::Run(); return 0; }
