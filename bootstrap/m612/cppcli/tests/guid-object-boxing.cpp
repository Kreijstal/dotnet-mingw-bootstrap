// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
using namespace System::Globalization;
using namespace System::Collections::Generic;
namespace Sys = System;
namespace NativeCtor {
    class Guid { public: explicit Guid(const char*); };
}
namespace NativeFunction {
    int Guid(const char*);
}
static_assert(__is_same(decltype(NativeCtor::Guid("native")), NativeCtor::Guid), "native construction must remain native");
static_assert(__is_same(decltype(NativeFunction::Guid("native")), int), "native function must keep its literal type");
public value struct BoxPayload {
    int Value;
    BoxPayload(int value) : Value(value) {}
    Object^ Self() { return *this; }
    Object^ DirectSelf() { return this->operator Object*(); }
};
public value struct LiteralValue {
    String^ Text;
    LiteralValue(String^ text) : Text(text) {}
    LiteralValue(bool) : Text(__cli_literal("wrong overload")) {}
};
public ref class GuidObjectBoxes abstract sealed {
    static BoxPayload _value = BoxPayload(17);
public:
    static Guid GuidValue() { return System::Guid("b2d9f3ec-c9fe-4a11-a2ec-d86208f7c0a2"); }
    static Guid WideGuid() { return Sys::Guid(L"B2D9F3EC-C9FE-4A11-A2EC-D86208F7C0A2"); }
    static Guid InvalidGuid() { return Guid("invalid"); }
    static String^ SourceLiteral() { LiteralValue value = LiteralValue("λ value"); return value.Text; }
    static Object^ Box() { return _value; }
    static void Change() { _value.Value = 29; }
    static Object^ FromMethod() { return _value.Self(); }
    static Object^ FromDirectMethod() { return _value.DirectSelf(); }
    static Object^ GuidBox() { return GuidValue(); }
};
public ref class PairEnumerator {
    int _reads;
public:
    property KeyValuePair<CultureInfo^, String^> Current {
        KeyValuePair<CultureInfo^, String^> get() {
            ++_reads;
            return KeyValuePair<CultureInfo^, String^>(CultureInfo::InvariantCulture, __cli_literal("pair value"));
        }
    }
    property Object^ Current2 { Object^ get() { return Current; } }
    int Reads() { return _reads; }
};
