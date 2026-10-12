// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
public ref class CleanupTrace abstract sealed {
    static int _value;
public:
    static void Reset() { _value = 0; }
    static void Add(int value) { _value = _value * 10 + value; }
    static int Read() { return _value; }
};
public ref class CleanupBase {
public:
    CleanupBase() {}
    ~CleanupBase() { CleanupTrace::Add(1); }
};
public ref class CleanupDerived : CleanupBase {
public:
    CleanupDerived() {}
    ~CleanupDerived() { CleanupTrace::Add(2); }
};
public ref class CleanupThrows : CleanupBase {
public:
    CleanupThrows() {}
    ~CleanupThrows() noexcept(false) { CleanupTrace::Add(3); throw gcnew InvalidOperationException(); }
};
public ref class CleanupStream : System::IO::MemoryStream {
public:
    CleanupStream() {}
    ~CleanupStream() { CleanupTrace::Add(4); }
};
public ref class CleanupUnrelatedBase {
public:
    CleanupUnrelatedBase() {}
    virtual void Dispose(bool closing) { CleanupTrace::Add(5); }
};
public ref class CleanupUnrelatedDerived : CleanupUnrelatedBase {
public:
    CleanupUnrelatedDerived() {}
    ~CleanupUnrelatedDerived() { CleanupTrace::Add(6); }
};
public ref class CleanupSimpleBase : IDisposable {
public:
    CleanupSimpleBase() {}
    virtual void Dispose() { CleanupTrace::Add(7); }
};
public ref class CleanupSimpleDerived : CleanupSimpleBase {
public:
    CleanupSimpleDerived() {}
    ~CleanupSimpleDerived() { CleanupTrace::Add(8); }
};
public ref class CleanupOwner {
    int _value;
    ref class Leaf {
        CleanupOwner^ _owner;
    public:
        Leaf(CleanupOwner^ owner) { _owner = owner; }
        int Read() { return _owner->_value; }
        ~Leaf() { _owner->_value += 100; }
    };
public:
    value struct Snapshot {
        int Number;
        String^ Text;
        Snapshot(int number, String^ text) { Number = number; Text = text; }
    };
    static Snapshot SnapshotValue(int number, String^ text) { return Snapshot(number, text); }
    CleanupOwner() { _value = 17; }
    Object^ Open() { return gcnew Leaf(this); }
    int Value() { return _value; }
    int ReadLeaf() { Leaf^ leaf = gcnew Leaf(this); int value = leaf->Read(); delete leaf; return value; }
};
public ref class CleanupCalls abstract sealed {
    static int _receivers;
    static CleanupBase^ Receiver(CleanupBase^ value) { ++_receivers; return value; }
public:
    static void Drop(CleanupBase^ value) { delete Receiver(value); }
    static void Explicit(CleanupBase^ value) { value->~CleanupBase(); }
    static int Receivers() { return _receivers; }
};
