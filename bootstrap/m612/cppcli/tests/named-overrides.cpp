using namespace System;
using namespace System::Collections;
#define interface struct
#include "interface-macro.hpp"
public interface /* keep the CLI keyword despite the Windows macro */ class IProperty {
    property int Number { int get(); }
    int Read();
};
public ref class SourceProperties : IProperty, IHeaderProperty {
public:
    virtual property int Number { int get() { return 42; } }
    virtual property int HeaderNumber { int get() { return 13; } }
    virtual int Read() { return 42; }
};
public ref class NamedEnumerator : IEnumerator {
    bool _moved;
public:
    NamedEnumerator() : _moved(false) {}
    virtual bool MoveNext() { if (_moved) return false; _moved = true; return true; }
    virtual void Reset() { _moved = false; }
    property Object^ Current2 {
        virtual Object^ get() sealed = IEnumerator::Current::get { return safe_cast<Object^>(42); }
    }
};
public ref class NamedSequence : IEnumerable {
    virtual IEnumerator^ GetEnumerator2() sealed = IEnumerable::GetEnumerator { return gcnew NamedEnumerator(); }
};
