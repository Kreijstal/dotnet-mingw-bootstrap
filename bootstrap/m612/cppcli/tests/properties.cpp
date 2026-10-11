using namespace System;
public ref class Box {
    int backing;
public:
    Box() : backing(0) {}
    property int Value {
        int get() { Console::WriteLine("get"); return backing; }
        void set(int value) { Console::WriteLine("set"); backing = value; }
    }
    property int Auto;
    property array<int>^ Samples {
        array<int>^ get() {
            try { return gcnew array<int>(2); }
            finally { Console::WriteLine("property finally"); }
        }
    }
};
public ref class Program abstract sealed {
public:
    static Box^ Get(Box^ box) { Console::WriteLine("receiver"); return box; }
    static void Run() {
        Box^ box = gcnew Box();
        box->Value = 4;
        Console::WriteLine(box->Value);
        Get(box)->Value += 2;
        Console::WriteLine(box->Value);
        box->Auto = 7;
        Console::WriteLine(box->Auto);
        Console::WriteLine(box->Samples->Length);
    }
};
int main() { Program::Run(); return 0; }
