namespace Example {
public ref class Counter {
public:
    int Value;
    Counter(int initial) : Value(initial) {}
    int Add(int step) { Value += step; return Value; }
    property int Current {
        int get() { return Value; }
        void set(int value) { Value = value; }
    }
};
}
