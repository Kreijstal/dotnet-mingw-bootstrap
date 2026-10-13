// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
public ref class NestedDelegates {
public:
    delegate int Transform(int value);
    delegate void Update(int% value);
    public ref class Inner {
    public:
        delegate int Transform(int value);
        static int Add(int value) { return value+7; }
        static Transform^ Create() { return gcnew Transform(&Add); }
    };
private:
    delegate int Hidden(int value);
    static int Double(int value) { return value*2; }
    static void Set(int% value) { value=42; GC::Collect(); }
    int _offset;
protected:
    delegate int Protected(int value);
public:
    NestedDelegates(int offset):_offset(offset) {}
    int Add(int value) { return value+_offset; }
    event EventHandler^ Changed;
    void Fire() { Changed(this,EventArgs::Empty); }
    static Transform^ Static() { return gcnew Transform(&Double); }
    Transform^ Instance() { return gcnew Transform(this,&NestedDelegates::Add); }
    static Update^ Change() { return gcnew Update(&Set); }
    static int Call(Transform^ callback,int value) { return callback(value); }
    static int Private(int value) { Hidden^ callback=gcnew Hidden(&Double); return callback(value); }
};
