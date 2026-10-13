// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
namespace RefLocalScope {
private enum class Mask { One=1, Two=2 };
private ref struct Data {
    Mask mask;
    bool single;
    Data(Mask value,bool flag):mask(value),single(flag) {}
};
}
public ref class RefLocalNode {
public:
    int Value;
    String^ Text;
    RefLocalNode(int value,String^ text):Value(value),Text(text) { GC::Collect(); }
    int Read() { return Value; }
    property String^ Label { String^ get() { return Text; } void set(String^ text) { Text=text; } }
};
public ref class RefLocalChecks abstract sealed {
public:
    static System::Collections::Hashtable^ Store() {
        RefLocalScope::Data item(RefLocalScope::Mask::One,false);
        auto table=gcnew System::Collections::Hashtable();
        table->Add("entry",(Object^)(%item));
        item.mask=RefLocalScope::Mask::Two; GC::Collect();
        return table;
    }
    static RefLocalNode^ Escape(String^ text) {
        RefLocalNode value(7,text);
        RefLocalNode^ alias=%value;
        value.Value+=2; value.Label=text;
        GC::Collect();
        if(alias->Read()!=9) throw gcnew InvalidOperationException("identity");
        return alias;
    }
    static void Change(RefLocalNode% value) { value.Value+=3; }
    static RefLocalNode^ Bind(String^ text) { RefLocalNode value(10,text); Change(value); return %value; }
    static Object^ Default() { String^ text="default"; RefLocalNode value(11,text); return (Object^)(%value); }
};
