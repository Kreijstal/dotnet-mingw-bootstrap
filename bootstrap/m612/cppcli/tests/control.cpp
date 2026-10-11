#include "settings.hpp"
using namespace System;
public ref class Program abstract sealed {
public:
    static int Sum(int limit) {
        int total = 0;
        for (int i = 0; i < limit; ++i) {
            if (i == 2) continue;
            total += i;
        }
        return total;
    }
    static void Run() {
        Console::WriteLine(Sum(LIMIT));
        int value = 0x2 ^ 3;
        while (value < 4) { ++value; }
        if (value == 4) Console::WriteLine("control OK");
        else Console::WriteLine("wrong branch");
        String^ message = gcnew String(L"Unicode: λ 😀");
        Console::WriteLine(message);
    }
};
int main() { Program::Run(); return 0; }
