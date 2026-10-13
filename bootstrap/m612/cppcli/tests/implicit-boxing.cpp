using namespace System;
public enum class BoxFlags : unsigned int { High=0x80000000, Low=1 };
public ref class ObjectSlot {
public:
    Object^ Field;
    property Object^ Value;
    void Set(Object^ value) { Field=value; }
    Object^ Overloaded(Object^ value) { return nullptr; }
    int Overloaded(int value) { return value+1; }
};
public ref class ImplicitBoxes abstract sealed {
public:
    static int Receivers;
    static int Operands;
    static array<String^>^ Owned={gcnew String("owned"),"tail"};
    static ObjectSlot^ Receiver(ObjectSlot^ value) { ++Receivers; return value; }
    static int Operand(int value) { ++Operands; return value; }
    static Object^ Int32(ObjectSlot^ target,int value) { Receiver(target)->Value=Operand(value); return target->Value; }
    static Object^ Flag(ObjectSlot^ target,BoxFlags value) { target->Value=value; return target->Value; }
    static Object^ Boolean(ObjectSlot^ target,bool value) { target->Set(value); return target->Field; }
    static Object^ Unsigned(ObjectSlot^ target,unsigned long long value) { target->Field=value; return target->Field; }
    static Object^ Float(ObjectSlot^ target,float value) { target->Set(value); return target->Field; }
    static Object^ Double(ObjectSlot^ target,double value) { target->Value=value; return target->Value; }
    static Object^ Character(ObjectSlot^ target,wchar_t value) { target->Value=value; return target->Value; }
    static Object^ Array(array<Object^>^ values,int index,int value) { values[index]=value; return values[index]; }
    static Object^ Expression(ObjectSlot^ target,int value) { return target->Field=value; }
    static int Typed(ObjectSlot^ target,int value) { return target->Overloaded(value); }
    static String^ EnumName(BoxFlags value) { return value.ToString(); }
    static int EnumHash(BoxFlags value) { return value.GetHashCode(); }
    static bool EnumEquals(BoxFlags left,BoxFlags right) { return left.Equals(right); }
    static array<String^>^ Filter(String^ name) { array<String^>^ values={name,"R" "AW",gcnew String("owned"),"tail",nullptr}; return values; }
    static array<int>^ Numbers() { array<int>^ values={Operand(1),Operand(2),Operand(3)}; return values; }
    static array<int>^ Empty() { array<int>^ values={}; return values; }
    static Object^ Dictionary(int value) {
        Collections::Hashtable^ values=gcnew Collections::Hashtable();
        values->Add("key",value);
        return values->get_Item("key");
    }
};
