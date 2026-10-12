#include <vcclr.h>
using namespace System;
extern "C" int NativePinned(const wchar_t*, unsigned short*);
public ref class PinBridge {
public:
    static int Read(String^ text, array<UInt16>^ values) {
        pin_ptr<const wchar_t> chars = PtrToStringChars(text);
        pin_ptr<UInt16> data = &values[0];
        return NativePinned(chars, data);
    }
    static int Alias(array<Int32>^ values) {
        interior_ptr<Int32> element = &values[1];
        GC::Collect();
        return *element;
    }
    static int Early(String^ text) {
        pin_ptr<const wchar_t> chars = PtrToStringChars(text);
        return *chars;
    }
    static void Fail(String^ text) {
        pin_ptr<const wchar_t> chars = PtrToStringChars(text);
        throw gcnew InvalidOperationException();
    }
    static int Loop(array<Int32>^ values) {
        int sum = 0;
        for (int i = 0; i < 3; ++i) {
            pin_ptr<Int32> item = &values[i];
            if (i == 1) continue;
            sum += *item;
            if (i == 2) break;
        }
        return sum;
    }
};
