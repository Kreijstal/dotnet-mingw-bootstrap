using namespace System;
public delegate int Scalar(int value);
public delegate void Update(int% value);
public delegate String^ TextCallback(String^ value);
public delegate void Notify(int value);
public delegate void* PointerCallback(void* value);
delegate int Hidden(int value);
public ref class BaseTarget {
public:
    virtual int Apply(int value) { return value + 1; }
};
public ref class DerivedTarget : BaseTarget {
public:
    virtual int Apply(int value) override { return value + 10; }
};
public ref class CounterTarget {
    int _offset;
public:
    CounterTarget(int offset) : _offset(offset) {}
    int Add(int value) { return _offset + value; }
};
int GlobalValue(int value) { return value + 2; }
public ref class DelegateBridge abstract sealed {
    static int Twice(int value) { return value * 2; }
    static void Set(int% value) { value = 42; }
    static String^ Echo(String^ value) { return value; }
    static void NoOp(int value) {}
    static void* Pointer(void* value) { return value; }
    static int Throw(int value) { throw gcnew InvalidOperationException(); }
public:
    static Scalar^ Static() { return gcnew Scalar(&DelegateBridge::Twice); }
    static Scalar^ Global() { return gcnew Scalar(GlobalValue); }
    static Scalar^ Instance(CounterTarget^ target) { return gcnew Scalar(target, &CounterTarget::Add); }
    static Scalar^ Virtual(BaseTarget^ target) { return gcnew Scalar(target, &BaseTarget::Apply); }
    static Scalar^ Throws() { return gcnew Scalar(&DelegateBridge::Throw); }
    static Update^ Updater() { return gcnew Update(&DelegateBridge::Set); }
    static TextCallback^ Text() { return gcnew TextCallback(&DelegateBridge::Echo); }
    static Notify^ Notification() { return gcnew Notify(&DelegateBridge::NoOp); }
    static PointerCallback^ Pointers() { return gcnew PointerCallback(&DelegateBridge::Pointer); }
    static int Call(Scalar^ value, int argument) { return value(argument); }
    static int Invoke(Scalar^ value, int argument) { return value->Invoke(argument); }
    static void Change(Update^ update, int% value) { update(value); }
};
