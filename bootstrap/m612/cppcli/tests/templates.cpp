using namespace System;
template<class T> T Twice(T value) { return value + value; }
template<int N> int Offset(int value) { return value + N; }
public ref class Program abstract sealed {
public:
    static void Run() {
        Console::WriteLine(Twice(7));
        Console::WriteLine(Twice(2.5));
        Console::WriteLine(Offset<3>(4));
    }
};
int main() { Program::Run(); return 0; }
