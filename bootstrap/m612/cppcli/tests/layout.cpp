using namespace System::Runtime::InteropServices;
constexpr int HeaderSize = 4;
[StructLayout(LayoutKind::Explicit, Size = HeaderSize * 4)]
public value struct LayoutUnion {
    [FieldOffset(0)] unsigned int Bits;
    [FieldOffset(0)] float Number;
    [FieldOffset(HeaderSize * 2)] unsigned short Tail;
};
[StructLayoutAttribute(LayoutKind::Sequential, Pack = 1, Size = 12, CharSet = CharSet::Unicode)]
public value struct PackedLayout {
    unsigned char Tag;
    unsigned int Value;
};
[StructLayout(LayoutKind::Explicit)]
public ref struct LayoutObject sealed {
    [FieldOffset(0)] int Value;
    [FieldOffset(8)] short Tail;
    property int Doubled { int get() { return Value * 2; } }
internal:
    [FieldOffset(16)] int Secret;
};
public ref class LayoutChecks abstract sealed {
public:
    static float UnionValue(unsigned int bits) { LayoutUnion value; value.Bits = bits; return value.Number; }
};
