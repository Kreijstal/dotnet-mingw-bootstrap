// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#using "AccessorContract.dll"
#include <mmintrin.h>
using namespace System;
extern "C" int NativeLength(const char*);
public ref class LiteralBase {
public:
    String^ Name;
protected:
    LiteralBase(String^ name):Name(name) {}
    LiteralBase(bool value):Name("incorrect bool overload") {}
};
public ref class InlineLiteral : LiteralBase {
public:
    String^ Label;
    Object^ Number;
    InlineLiteral():LiteralBase(("R" "AW")),Label(L"東京 😀"),Number(42) {}
};
public ref class ExternalLiteral : LiteralBase { public: ExternalLiteral(); };
ExternalLiteral::ExternalLiteral():LiteralBase("out of line") {}
using BaseAlias=LiteralBase;
public ref class AliasLiteral : BaseAlias { public: AliasLiteral():BaseAlias("alias") {} };
public ref class NativeLiteralBase {
public:
    int Length;
protected:
    NativeLiteralBase(const char* value):Length(NativeLength(value)) {}
};
public ref class NativeLiteral : NativeLiteralBase { public: NativeLiteral():NativeLiteralBase("native") {} };
public ref class ResourceLiteral : Resources::ResourceManager {
public:
    ResourceLiteral():ResourceManager("SourceBootstrapResource",Reflection::Assembly::GetExecutingAssembly()) {}
};
public ref class QualifiedTable : Collections::Hashtable {
public:
    Object^ ReadBase(String^ name) { return Hashtable::default::get(name); }
    void WriteBase(String^ name,int value) { Hashtable::default::set(name,value); }
};
public ref class QualifiedCustom : AccessorContract::BaseTable {
public:
    int ReadBase(int index) { return AccessorContract::BaseTable::default::get(index); }
    void WriteBase(int index,int value) { AccessorContract::BaseTable::default::set(index,value); }
    String^ TitleBase() { return AccessorContract::BaseTable::Title::get(); }
};
public ref class ObjectConstructor {
public:
    Object^ Value;
    ObjectConstructor(Object^ value):Value(value) {}
};
public ref class TypedConstructor {
public:
    int Value;
    TypedConstructor(Object^ value):Value(-1) {}
    TypedConstructor(int value):Value(value) {}
};
public ref class ConstructorCalls abstract sealed {
public:
    static Object^ Box(int value) { return (gcnew ObjectConstructor(value))->Value; }
    static int Typed(int value) { return (gcnew TypedConstructor(value))->Value; }
    static ArgumentOutOfRangeException^ Range(int value) { return gcnew ArgumentOutOfRangeException("value",value,"message"); }
};
public delegate void NameWriter(String^ value);
public ref class PropertyRoot {
    String^ _name;
public:
    property String^ Name {
        String^ get() { return _name; }
    protected:
        void set(String^ value) { _name=value; }
    }
};
public ref class PropertyMid : PropertyRoot {};
public ref class PropertyLeaf : PropertyMid {
public:
    NameWriter^ Bind() { return gcnew NameWriter(this,&PropertyLeaf::Name::set); }
    String^ ReadBase() { return PropertyLeaf::Name::get(); }
};
