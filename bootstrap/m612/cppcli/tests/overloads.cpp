using namespace System;
public ref class Program abstract sealed {
public:
    static int Choose(int value) { return value + 1; }
    static int Choose(bool value) { if (value) return 10; else return 20; }
    static void Run() {
        Console::WriteLine(Choose(4));
        Console::WriteLine(Choose(true));
        Console::WriteLine(false);
        Console::WriteLine("text");
    }
};
int main() { Program::Run(); return 0; }
