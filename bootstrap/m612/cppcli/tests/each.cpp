using namespace System;
using namespace System::Collections::Generic;
using namespace EachFixtures;
public ref class EachBridge abstract sealed {
    static int _receivers;
    static ProbeSequence^ Receiver(ProbeSequence^ value) { _receivers += 1; return value; }
public:
    static int Receivers() { return _receivers; }
    static int Once(ProbeSequence^ source) {
        int sum = 0;
        for each (int value in Receiver(source)) { sum += value; }
        return sum;
    }
    static int Explicit(ExplicitSequence^ source) {
        int sum = 0;
        for each (int value in source) { sum += value; }
        return sum;
    }
    static int Sum(IEnumerable<int>^ source, int mode) {
        int sum = 0;
        for each (int value in source) {
            if (mode == 1 && value == 2) break;
            if (mode == 2 && value == 2) continue;
            if (mode == 3) return value;
            if (mode == 4 && value == 2) throw gcnew InvalidOperationException();
            sum += value;
        }
        return sum;
    }
    static int Nested(IEnumerable<int>^ source) {
        int sum = 0;
        for each (int outer in source) {
            for each (int inner in source) { sum += outer * inner; if (inner == 2) break; }
        }
        return sum;
    }
    static int PairSum(IEnumerable<KeyValuePair<String^, int>>^ source) {
        int sum = 0;
        for each (KeyValuePair<String^, int> pair in source) { sum += pair.Value; }
        return sum;
    }
    static int Untyped(System::Collections::IEnumerable^ source) {
        int sum = 0;
        for each (int value in source) { sum += value; }
        return sum;
    }
    static String^ Last(System::Collections::IEnumerable^ source) {
        String^ last = nullptr;
        for each (String^ value in source) { last = value; }
        return last;
    }
    static int Characters(String^ text) {
        int sum = 0;
        for each (Char value in text) { sum += value; }
        return sum;
    }
    static double ArraySum(array<int>^ source) {
        double sum = 0;
        for each (double value in source) { if (value == 2) continue; sum += value; }
        return sum;
    }
    static int Flags(array<Int64>^ source) {
        int count = 0;
        for each (bool value in source) { if (value) ++count; }
        return count;
    }
    static double Unsigned(array<UInt64>^ source) {
        double sum = 0;
        for each (double value in source) { sum += value; }
        return sum;
    }
    static void CopyPairs(IEnumerable<KeyValuePair<String^, int>>^ source, array<KeyValuePair<String^, int>>^ output) {
        int index = 0;
        for each (KeyValuePair<String^, int> pair in source) { output[index] = pair; ++index; }
    }
};
public ref class EachProperty {
    IEnumerable<int>^ _source;
public:
    EachProperty(IEnumerable<int>^ source) : _source(source) {}
    property int First {
        int get() { for each (int value in _source) { return value; } return 0; }
    }
};
