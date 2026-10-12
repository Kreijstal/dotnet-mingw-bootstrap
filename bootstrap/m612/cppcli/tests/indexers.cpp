using namespace System;
using namespace System::Collections::Generic;
public ref class Indexed {
    array<int>^ _values;
public:
    int Reads;
    int Writes;
    Indexed() : _values(gcnew array<int>(3)) {}
    property int default[int] {
        int get(int index) { Reads += 1; return _values[index]; }
        void set(int index, int value) { Writes += 1; _values[index] = value; }
    }
    property int Offset[int] { int get(int index) { return _values[index] + 100; } }
};
public ref class OverloadedIndex {
    int _number;
    int _named;
public:
    property int default[int] { int get(int index); void set(int index, int value); }
    property int default[String^] { int get(String^ key); void set(String^ key, int value); }
internal:
    property int Hidden[int] { int get(int index) { return index + 1; } }
};
int OverloadedIndex::default::get(int index) { return _number + index; }
void OverloadedIndex::default::set(int index, int value) { _number = value - index; }
int OverloadedIndex::default::get(String^ key) { return _named + key->Length; }
void OverloadedIndex::default::set(String^ key, int value) { _named = value - key->Length; }
public ref class IndexerBridge abstract sealed {
    static int _receivers;
    static int _indices;
    static Indexed^ Receiver(Indexed^ value) { _receivers += 1; return value; }
    static int Index() { _indices += 1; return 1; }
public:
    static void Update(Indexed^ value) { Receiver(value)[Index()] += 5; }
    static int Receivers() { return _receivers; }
    static int Indices() { return _indices; }
    static int Read(Indexed^ value) { return value[1]; }
    static int Named(Indexed^ value) { return value->Offset[1]; }
    static int DictionaryItem(Dictionary<String^, int>^ value, String^ key) { value[key] += 2; return value[key]; }
    static Char First(String^ text) { return text[0]; }
    static int Overloaded(OverloadedIndex^ value) { value[1] = 40; value[__cli_literal("name")] = 2; return value[1] + value[__cli_literal("name")]; }
};
