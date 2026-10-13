// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
using namespace System::Runtime::InteropServices;
public ref class NativeInts abstract sealed {
public:
    static IntPtr Make(long long bits) { return IntPtr(bits); }
    static long long Bits(IntPtr value) { return value.ToInt64(); }
    static IntPtr Zero() { return IntPtr::Zero; }
    static UIntPtr Unsigned(unsigned long long bits) { return UIntPtr(bits); }
    static unsigned long long UnsignedBits(UIntPtr value) { return value.ToUInt64(); }
    static int Receivers;
    static IntPtr Receiver(IntPtr value) { ++Receivers; return value; }
    static unsigned long long Raw(IntPtr value) { return (unsigned long long)(void*)Receiver(value); }
    static unsigned long long RawUnsigned(UIntPtr value) { return (unsigned long long)static_cast<const unsigned char*>(value); }
    static int TypedCast() {
        IntPtr memory=Marshal::AllocHGlobal(4);
        try {
            int* pointer=static_cast<int*>(memory);
            *pointer=0x44332211;
            return Marshal::ReadInt32(memory);
        }
        finally { Marshal::FreeHGlobal(memory); }
    }
    static int Copy() {
        IntPtr memory = Marshal::AllocHGlobal(4);
        try {
            void* raw = memory.ToPointer();
            IntPtr wrapped(raw); // The pointer constructor used by WPF.
            Marshal::WriteInt32(wrapped, 0x44332211);
            array<Byte>^ bytes = gcnew array<Byte>(4);
            Marshal::Copy(wrapped, bytes, 0, 4);
            return bytes[0] + (bytes[1] << 8);
        }
        finally { Marshal::FreeHGlobal(memory); }
    }
};
int main() {
    Console::WriteLine(NativeInts::Bits(NativeInts::Make(5000000002LL)));
    Console::WriteLine(NativeInts::Bits(NativeInts::Zero()));
    Console::WriteLine(NativeInts::Copy());
    return 0;
}
