// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
#pragma pack(push,1)
struct PackedEntry { unsigned char Tag; int Value; };
#pragma pack(pop)
enum Choice { First=17, Second=201 };
public ref class NativeLocalArrays abstract sealed {
public:
    static int Calls;
    static int Next() { return ++Calls; }
    static void Check() {
        const unsigned char charsets[]={178,177,222,186,136,238,134,161,129,77,255,204,128,2,162,163,130};
        if(sizeof(charsets)!=17 || charsets[0]!=178 || charsets[16]!=130)
            throw gcnew InvalidOperationException("charset constants");
        int values[5]={Next(),Next()};
        int* alias=values;
        GC::Collect();
        alias[2]=40;
        if(Calls!=2 || values[0]!=1 || values[1]!=2 || values[2]!=40 || values[3]!=0 || values[4]!=0)
            throw gcnew InvalidOperationException("scalar storage/order/filler");
        PackedEntry entries[3]={{3,100},{7,200}};
        PackedEntry* records=entries;
        records[1].Value+=9;
        if(sizeof(entries)!=15 || entries[0].Tag!=3 || entries[1].Tag!=7 || entries[1].Value!=209 || entries[2].Value!=0)
            throw gcnew InvalidOperationException("packed POD layout");
        wchar_t wide[8]=L"\u03bb\U0001f680";
        char narrow[6]="abc";
        if(wide[0]!=0x03bb || wide[1]!=0xd83d || wide[2]!=0xde80 || wide[3]!=0 || wide[7]!=0 || narrow[2]!='c' || narrow[5]!=0)
            throw gcnew InvalidOperationException("string initialization");
        int* pointers[3]={values,&values[2]};
        if(*pointers[0]!=1 || *pointers[1]!=40 || pointers[2]!=nullptr)
            throw gcnew InvalidOperationException("pointer elements");
        Choice choices[]={First,Second};
        double doubles[3]={1.5,-2.25};
        if(choices[1]!=Second || doubles[0]+doubles[1]!=-0.75 || doubles[2]!=0)
            throw gcnew InvalidOperationException("enum/floating elements");
        for(int iteration=0;iteration<3;++iteration) {
            int repeated[2]={Next()};
            if(repeated[0]!=iteration+3 || repeated[1]!=0)
                throw gcnew InvalidOperationException("declaration reinitialization");
            repeated[1]=123;
        }
        GC::Collect();
        if(records[1].Value!=209 || alias[2]!=40) throw gcnew InvalidOperationException("GC aliases");
    }
};
int main() {
    NativeLocalArrays::Check();
    Console::WriteLine("native local arrays: charset constants, packed POD, pointers, strings, order, zero filler, reinitialization and GC PASS");
    return 0;
}
