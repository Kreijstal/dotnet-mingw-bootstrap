// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
int main() {
    wchar_t* text=new wchar_t[4]();
    text[0]=0x03bb; text[1]=0xd83d; text[2]=0xde00;
    const wchar_t* constant=text;
    String^ copy=gcnew String(constant);
    char16_t* characters=reinterpret_cast<char16_t*>(text);
    String^ slice=gcnew String(characters,1,2);
    text[0]='X'; delete[] text;
    GC::Collect();
    if(copy->Length!=3 || copy->get_Chars(0)!=0x03bb || copy->get_Chars(1)!=0xd83d || copy->get_Chars(2)!=0xde00) return 1;
    if(slice->Length!=2 || slice->get_Chars(0)!=0xd83d || slice->get_Chars(1)!=0xde00) return 2;
    String^ empty=gcnew String(static_cast<wchar_t*>(nullptr));
    if(empty->Length!=0) return 3;
    char* narrow=new char[3](); narrow[0]='O'; narrow[1]='K';
    String^ ascii=gcnew String(static_cast<const char*>(narrow));
    narrow[0]='X'; delete[] narrow;
    if(ascii->Length!=2 || ascii->get_Chars(0)!='O' || ascii->get_Chars(1)!='K') return 4;
    Console::WriteLine(copy);
    Console::WriteLine(slice);
    Console::WriteLine(ascii);
    Console::WriteLine("source-metadata native UTF-16/narrow String constructors, slices, null, copy ownership and GC OK");
    return 0;
}
