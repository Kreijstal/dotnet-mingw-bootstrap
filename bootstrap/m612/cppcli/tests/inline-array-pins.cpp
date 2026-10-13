// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
struct FaceRecord { int Tag; wchar_t Face[8]; };
public ref class InlineArrayPins abstract sealed {
public:
    static String^ Create() {
        array<FaceRecord>^ values=gcnew array<FaceRecord>(1);
        interior_ptr<FaceRecord> record=&values[0];
        record->Tag=42;
        String^ result;
        {
            pin_ptr<wchar_t> text=record->Face;
            text[0]=0x03bb; text[1]=0xd83d; text[2]=0xde80; text[3]=0;
            GC::Collect();
            result=gcnew String(text);
            text[0]='X';
        }
        GC::Collect();
        if(record->Tag!=42 || record->Face[0]!='X') throw gcnew InvalidOperationException("inline storage");
        return result;
    }
};
int main() {
    String^ result=InlineArrayPins::Create();
    if(result->Length!=3 || result->get_Chars(0)!=0x03bb || result->get_Chars(1)!=0xd83d || result->get_Chars(2)!=0xde80) return 1;
    Console::WriteLine("inline native array decay pin retains parent storage, UTF-16 text copy and GC PASS");
    return 0;
}
