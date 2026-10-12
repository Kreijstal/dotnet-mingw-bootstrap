using namespace System;
namespace Example {
public interface class IRead { int Read(); };
public interface class IWrite { void Write(int value); };
public ref class Base abstract {
    int factor;
public:
    Base(int scale) : factor(scale) {}
    virtual int Read() abstract;
    virtual int Scale() { return factor; }
    virtual int Fixed() sealed { return 17; }
};
public ref class Counter : Base, IRead, IWrite {
    int value;
public:
    Counter(int initial) : Base(3), value(initial) {}
    virtual int Read() override { return value; }
    virtual void Write(int next) { value = next; }
    virtual int Scale() override { return Base::Scale() * 2; }
};
public ref class PropertyBase {
protected:
    int value;
public:
    PropertyBase() : value(7) {}
    virtual property int Value {
        int get() { return value; }
        void set(int next) { value = next; }
    }
};
public ref class PropertyDerived : PropertyBase {
public:
    virtual property int Value {
        int get() override { return value + 10; }
        void set(int next) override { value = next + 1; }
    }
};
public ref class VirtualProperties abstract sealed {
public:
    static int Read(PropertyBase^ value) { return value->Value; }
    static void Write(PropertyBase^ value, int next) { value->Value = next; }
};
}
int main() {
    Example::Counter^ counter = gcnew Example::Counter(7);
    Example::Base^ base = counter;
    Example::IRead^ reader = counter;
    Example::IWrite^ writer = counter;
    Console::WriteLine(base->Read());
    Console::WriteLine(reader->Read());
    writer->Write(9);
    Console::WriteLine(reader->Read());
    Console::WriteLine(base->Scale());
    Console::WriteLine(base->Fixed());
    Console::WriteLine(dynamic_cast<Example::IWrite^>(base) != nullptr);
    return 0;
}
