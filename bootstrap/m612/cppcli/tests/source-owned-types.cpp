#using "SourcePeer.dll"
#include <mmintrin.h>
[assembly:System::Reflection::AssemblyVersion("1.0.0.0")];
namespace SourceContract {
public ref class Value {
public:
    int Data;
    Value(int value):Data(value) {}
    virtual int Read() { return Data+1; }
};
public ref class Bridge abstract sealed {
public:
    static int Call(int number) {
        Value^ value=gcnew Value(number);
        Peer::Holder^ holder=gcnew Peer::Holder(value);
        return holder->Read();
    }
    static Value^ Return(Value^ value) { Peer::Holder^ holder=gcnew Peer::Holder(value); return holder->Current; }
    static array<Value^>^ Array(array<Value^>^ values) { return Peer::Holder::EchoArray(values); }
    static void Replace(Value^% value,Value^ replacement) { Peer::Holder::Replace(value,replacement); }
};
}
