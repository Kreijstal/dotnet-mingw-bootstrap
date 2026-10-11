namespace Example {
public ref class Counter {
public:
    int Value;
    Counter(int initial) : Value(initial) {}
    int Add(int step) { Value += step; return Value; }
};
}
