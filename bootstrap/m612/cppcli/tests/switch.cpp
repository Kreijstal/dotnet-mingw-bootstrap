using namespace System;
namespace Example {
[Flags] public enum class Bits : unsigned char { None = 0, A = 1, B = 2, Both = A | B };
public enum class Wide : unsigned long long { High = 0x8000000000000000ULL, Max = 0xffffffffffffffffULL };
public enum class Negative : long long { Low = -5000000002LL };
public ref class Switches abstract sealed {
    static int _evaluations = 0;
    static int Condition(int value) { _evaluations += 1; return value; }
public:
    static Bits Combine(Bits a, Bits b) { return a | b; }
    static Bits Mask(Bits a, Bits b) { return a & b; }
    static Bits Toggle(Bits a, Bits b) { return a ^ b; }
    static Bits Invert(Bits a) { return ~a; }
    static Wide InvertWide(Wide a) { return ~a; }
    static int Convert(Bits value) {
        char bits = (char)value;
        switch (bits) {
            case Bits::None: return 0;
            case Bits::A: return 10;
            case Bits::B: return 20;
            case (Bits::A | Bits::B): return 30;
            default: throw gcnew InvalidOperationException();
        }
    }
    static int FallThrough(int value) {
        int sum = 0;
        switch (Condition(value)) {
            case 1: sum += 10;
            case 2: sum += 20; break;
            default: sum += 90; break;
            case 4: case 5: sum += 40;
        }
        return sum;
    }
    static int Evaluations() { return _evaluations; }
    static int NoDefault(int value) { int result = 7; switch(value) { case 5: result = 9; } return result; }
    static int WideCases(Wide value) { switch(value) { case Wide::High: return 1; case Wide::Max: return 2; default: return 3; } }
    static int NegativeCase(Negative value) { switch(value) { case Negative::Low: return 4; default: return 5; } }
    static int Nested(int value) {
        int sum = 0;
        for (int i = 0; i < 4; ++i) {
            switch(i) {
                case 0: continue;
                case 1: sum += 10; break;
                case 2:
                    for (int j = 0; j < 3; ++j) {
                        switch(j) { case 1: continue; default: sum += j; break; }
                    }
                    break;
                default:
                    switch(value) { case 9: sum += 90; break; default: sum += 30; }
                    break;
            }
        }
        return sum;
    }
    static int WithFinally(int value, int% cleanup) {
        switch(value) {
            case 1: try { return 42; } finally { cleanup += 1; }
            default: try { break; } finally { cleanup += 2; }
        }
        return 7;
    }
};
}
