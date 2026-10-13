using namespace System;
using namespace System::Collections::Generic;
public ref class TokenMarker {};
public value struct TokenValue { int Number; };
public enum class TokenMode : unsigned short { One = 1 };
using IntArray = array<int>^;
using FloatPointer = float*;
public ref class TypeTokens abstract sealed {
public:
    static Type^ Source() { return TokenMarker::typeid; }
    static Type^ Value() { return TokenValue::typeid; }
    static Type^ Mode() { return TokenMode::typeid; }
    static Type^ Text() { return System::String::typeid; }
    static Type^ Integer() { return int::typeid; }
    static Type^ Wide() { return unsigned long long::typeid; }
    static Type^ Array() { return IntArray::typeid; }
    static Type^ ArrayRecord() { return array<Byte>::typeid; }
    static Type^ ValueArrayRecord() { return array<TokenValue>::typeid; }
    static Type^ JaggedArrayRecord() { return array<array<String^>^>::typeid; }
    static Type^ Pointer() { return FloatPointer::typeid; }
    static Type^ Generic() { return List<String^>::typeid; }
    static Type^ Nested() { return List<List<int>^>::typeid; }
};
