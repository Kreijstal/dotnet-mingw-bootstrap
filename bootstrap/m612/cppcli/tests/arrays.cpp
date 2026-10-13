using namespace System;
public ref class Box {
public:
    int Value;
    Box(int value) : Value(value) {}
};
public ref class Arrays abstract sealed {
public:
    static array<int>^ Get(array<int>^ values) { Console::WriteLine("receiver"); return values; }
    static int Index() { Console::WriteLine("index"); return 1; }
    static array<int>^ Make(int size) { return gcnew array<int>(size); }
    static int Read(array<int>^ values, int index) { return values[index]; }
    static void Write(array<int>^ values, int index, int value) { values[index] = value; }
};
int main() {
    array<int>^ values = Arrays::Make(3);
    for (int i = 0; i < values->Length; ++i) values[i] = i * 2;
    Console::WriteLine(values->Length);
    Console::WriteLine(values[0]);
    Console::WriteLine(values[2]);
    Arrays::Get(values)[Arrays::Index()] += 3;
    Console::WriteLine(values[1]);
    Console::WriteLine(Arrays::Get(values)[Arrays::Index()]++);
    Console::WriteLine(values[1]);
    array<Box^>^ boxes = gcnew array<Box^>(2);
    Console::WriteLine(boxes[0] == nullptr);
    boxes[1] = gcnew Box(9);
    Console::WriteLine(boxes[1]->Value);
    String^ text = gcnew String("cli");
    array<System::Char>^ chars = text->ToCharArray();
    Console::WriteLine(chars->Length);
    Console::WriteLine(chars[1]);
    Object^ object = values;
    array<int>^ casted = safe_cast<array<int>^>(object);
    Console::WriteLine(casted[1]);
    array<int>^ nil = 0;
    Console::WriteLine(nil ? true : false);
    Console::WriteLine(values ? true : false);
    return 0;
}
