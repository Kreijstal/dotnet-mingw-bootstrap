// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
template<class T>
public ref class TemplateBox {
    T _value;
public:
    TemplateBox(T value) : _value(value) {}
    property T Value { T get(); void set(T value); }
    int Unused() { return 999; }
};
template<class T> T TemplateBox<T>::Value::get() { return _value; }
template<class T> void TemplateBox<T>::Value::set(T value) { _value = value; }
template<class T>
public ref class TemplateBase {
protected:
    int _state;
public:
    TemplateBase(int value) : _state(value) {}
    virtual int Read() { return _state; }
};
template<class T>
public ref class TemplateDerived : TemplateBase<T> {
public:
    TemplateDerived(int value) : TemplateBase<T>(value) {}
    virtual int Read() override { return this->_state + 1; }
};
public ref class TemplateFactory abstract sealed {
public:
    static Object^ Integer() {
        TemplateBox<int>^ box = gcnew TemplateBox<int>(17);
        box->Value = 29; if (box->Value != 29) return nullptr; return box;
    }
    static Object^ Wide() {
        TemplateBox<long long>^ box = gcnew TemplateBox<long long>(5000000002LL);
        box->Value = box->Value + 1; return box;
    }
    static Object^ Derived() {
        TemplateBase<int>^ value = gcnew TemplateDerived<int>(41);
        if (value->Read() != 42) return nullptr; return value;
    }
};
