using namespace System;
public ref class Base {
public:
    int value;
    Base(int initial) : value(initial) {}
    int Add(int step) { value += step; return value; }
};
public ref class Derived : Base {
public:
    Derived(int initial) : Base(initial) {}
    int Twice() { return Add(value); }
};
public ref class Program abstract sealed {
public:
    static int Choose(bool flag) { if (flag) return 2; else return 3; }
    static int Choose(String^ text) { Console::WriteLine(text); return 1; }
    static void Run() {
        Derived^ derived = gcnew Derived(5);
        Base^ base = derived;
        Console::WriteLine(base->Add(3));
        Console::WriteLine(derived->Twice());
        Base^ alias = base;
        Console::WriteLine(Object::ReferenceEquals(alias, base));
        Derived^ cast = safe_cast<Derived^>(base);
        Console::WriteLine(Object::ReferenceEquals(cast, derived));
        Derived^ explicitCast = (Derived^)base;
        Console::WriteLine(Object::ReferenceEquals(explicitCast, derived));
        Base^ unrelated = gcnew Base(0);
        Derived^ absent = dynamic_cast<Derived^>(unrelated);
        Console::WriteLine(Object::ReferenceEquals(absent, nullptr));
        alias = nullptr;
        Console::WriteLine(Object::ReferenceEquals(alias, base));
        Console::WriteLine(Choose("typed literal"));
        Console::WriteLine(Choose(true));
    }
};
int main() { Program::Run(); return 0; }
