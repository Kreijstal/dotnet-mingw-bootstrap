// SPDX-License-Identifier: MIT
using namespace System;
extern "C" void* finalizer_open(int id);
extern "C" void finalizer_close(void* value);
public ref class FinalBase {
    void* _resource;
public:
    FinalBase():_resource(finalizer_open(1)) {}
    ~FinalBase() { this->!FinalBase(); }
    void CloseOwn() { this->!FinalBase(); }
protected:
    !FinalBase(void) { finalizer_close(_resource); _resource=nullptr; }
};
public ref class FinalDerived:FinalBase {
    void* _resource;
public:
    FinalDerived():_resource(finalizer_open(2)) {}
    ~FinalDerived(void) { this->!FinalDerived(); }
    void CloseDerived() { this->!FinalDerived(); }
    void CloseBaseExplicit() { this->FinalBase::!FinalBase(); }
protected:
    !FinalDerived(void);
};
FinalDerived::!FinalDerived(void) { finalizer_close(_resource); _resource=nullptr; }
namespace Example {
public ref class FinalOnly {
    void* _resource;
public:
    FinalOnly():_resource(finalizer_open(3)) {}
protected:
    !FinalOnly();
};
}
Example::FinalOnly::!FinalOnly() { finalizer_close(_resource); _resource=nullptr; }
public ref class FinalThrows:FinalBase {
public:
    FinalThrows() {}
protected:
    !FinalThrows() { throw gcnew InvalidOperationException(); }
};
public ref class FinalCalls abstract sealed {
public:
    static void Drop(FinalDerived^ value) { delete value; }
};
