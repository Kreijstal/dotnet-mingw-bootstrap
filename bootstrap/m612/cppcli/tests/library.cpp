namespace Example {
public ref class Defaults { public: int Value = 23; };
public ref class RequiredBase { public: RequiredBase(int value) {} };
public ref class NoDefault : RequiredBase {};
public ref class Counter {
    property int Secret;
public:
    int Value;
    Counter(int initial) : Value(initial) {}
    static Counter^ Create(int initial) { return gcnew Counter(initial); }
    int Add(int step) { Value += step; return Value; }
    property int Current {
        int get() { return Value; }
        void set(int value) { Value = value; }
    }
};
}
