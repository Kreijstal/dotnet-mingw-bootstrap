using namespace System;
using namespace System::Collections::Generic;
public ref class IntComparer : IComparer<int> {
public:
    virtual int Compare(int left, int right) { return left - right; }
};
public ref class PairCombiner : GenericFixture::ICombiner<String^, int> {
public:
    virtual int Combine(String^ key, int value) { return key->Length + value; }
};
public ref class GenericBridge abstract sealed {
public:
    static int ListSum(int value) {
        List<int>^ list = gcnew List<int>();
        list->Add(value);
        list->Add(2);
        return list->get_Item(0) + list->get_Item(1) + list->Count;
    }
    static int Enumerate(int value) {
        List<int>^ list = gcnew List<int>(); list->Add(value); list->Add(2);
        IEnumerable<int>^ sequence = list;
        IEnumerator<int>^ cursor = sequence->GetEnumerator();
        int sum = 0;
        try { while (cursor->MoveNext()) sum += cursor->Current; }
        finally { cursor->Dispose(); }
        return sum;
    }
    static Dictionary<String^, int>^ DictionaryValue(String^ key, int value) {
        Dictionary<String^, int>^ dictionary = gcnew Dictionary<String^, int>();
        dictionary->Add(key, value);
        return dictionary;
    }
    static bool Lookup(Dictionary<String^, int>^ dictionary, String^ key, int% value) {
        return dictionary->TryGetValue(key, value);
    }
    static KeyValuePair<String^, int> Pair(String^ key, int value) { return KeyValuePair<String^, int>(key, value); }
    static String^ Key(KeyValuePair<String^, int> pair) { return pair.Key; }
    static int Value(KeyValuePair<String^, int> pair) { return pair.Value; }
    static Nullable<int> Empty() { Nullable<int> value; return value; }
    static int NullableValue(int number) { Nullable<int> value(number); return value.Value; }
    static List<List<int>^>^ Nested(int value) {
        List<List<int>^>^ outer = gcnew List<List<int>^>();
        List<int>^ inner = gcnew List<int>(); inner->Add(value); outer->Add(inner);
        return outer;
    }
    static int Field(GenericFixture::Slot<int>^ slot) { slot->Value += 1; return slot->Value; }
    static int StaticField(int value) { GenericFixture::Slot<int>::Last = value; return GenericFixture::Slot<int>::Last; }
    static Int64 StaticField64(Int64 value) { GenericFixture::Slot<Int64>::Last = value; return GenericFixture::Slot<Int64>::Last; }
    static GenericFixture::Child<int>^ Child(int value) { return gcnew GenericFixture::Child<int>(value); }
};
