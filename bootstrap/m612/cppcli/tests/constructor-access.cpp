using namespace System;
public ref class PrivateFactory {
    int _value;
    PrivateFactory(int value) : _value(value) {}
public:
    static PrivateFactory^ Create(int value) { return gcnew PrivateFactory(value); }
    property int Value { int get() { return _value; } }
};
public ref class ProtectedFactory {
    int _value;
protected:
    ProtectedFactory(int value) : _value(value) {}
public:
    static ProtectedFactory^ Create(int value) { return gcnew ProtectedFactory(value); }
    int Read() { return _value; }
};
public ref class NestedFactory {
    PrivateFactory^ _child;
    NestedFactory(PrivateFactory^ child) : _child(child) {}
public:
    static NestedFactory^ Create() { return gcnew NestedFactory(PrivateFactory::Create(41)); }
    int Read() { return _child->Value; }
};
public ref class ConvertedFactory {
    long long _value;
public:
    ConvertedFactory(long long value = 7) : _value(value) {}
    long long Read() { return _value; }
};
public ref class StaticFactory {
public:
    static ConvertedFactory^ Item = gcnew ConvertedFactory(23);
    property ConvertedFactory^ Other { ConvertedFactory^ get() { return gcnew ConvertedFactory(7); } }
};
public ref class LiteralFactory {
    String^ _text;
    LiteralFactory(String^ text) : _text(text) {}
    LiteralFactory(bool) : _text(__cli_literal("wrong overload")) {}
public:
    static LiteralFactory^ Create() { return gcnew LiteralFactory("λ factory"); }
    static LiteralFactory^ Default = gcnew LiteralFactory("static literal");
    property LiteralFactory^ Next { LiteralFactory^ get() { return gcnew LiteralFactory("accessor literal"); } }
    String^ Read() { return _text; }
};
public interface class IFactoryValue { int Read(); };
public interface class IFactoryChild : IFactoryValue {};
public ref class InterfaceFactory : IFactoryChild, IDisposable {
    InterfaceFactory() {}
public:
    static InterfaceFactory^ Create() { return gcnew InterfaceFactory(); }
    virtual int Read() { return 42; }
    virtual void Dispose() {}
};
public ref class ImportedFactory : System::Runtime::InteropServices::CriticalHandle {
    ImportedFactory() : CriticalHandle(IntPtr::Zero) {}
protected:
    virtual bool ReleaseHandle() override { return true; }
public:
    static ImportedFactory^ Create() { return gcnew ImportedFactory(); }
    virtual property bool IsInvalid { bool get() override { return true; } }
};
public ref class ConstructorProbes {
public:
    static ArgumentOutOfRangeException^ Range() { return gcnew ArgumentOutOfRangeException("faceIndex"); }
    static Object^ Upcast() { return InterfaceFactory::Create(); }
    static Object^ Handle() { return ImportedFactory::Create(); }
};
int main() {
    Console::WriteLine(PrivateFactory::Create(17)->Value);
    Console::WriteLine(ProtectedFactory::Create(29)->Read());
    Console::WriteLine(NestedFactory::Create()->Read());
    ConvertedFactory^ converted = gcnew ConvertedFactory((gcnew ConvertedFactory(63))->Read());
    Console::WriteLine(converted->Read());
    Console::WriteLine(StaticFactory::Item->Read());
    ConvertedFactory^ defaults = gcnew ConvertedFactory;
    StaticFactory^ accessor = gcnew StaticFactory();
    if (accessor->Other->Read() != defaults->Read()) return 1;
    Console::WriteLine(defaults->Read());
    InterfaceFactory^ object = InterfaceFactory::Create();
    GC::KeepAlive(object);
    Object^ base = object;
    ImportedFactory^ handle = ImportedFactory::Create();
    GC::KeepAlive(handle);
    return 0;
}
