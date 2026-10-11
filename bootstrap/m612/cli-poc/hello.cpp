// C++/CLI input: the managed declarations are not ordinary C++.
using namespace System;

public ref class Greeter abstract sealed
{
public:
    static void Hello()
    {
        String^ message = gcnew String(L"Hello from C++/CLI through a GCC plugin!");
        Console::WriteLine(message);
    }
};

int main()
{
    Greeter::Hello();
    return 0;
}
