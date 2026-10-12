// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "native-fields.hpp"
PackedFields* Identity(PackedFields* value, int% calls) { ++calls; return value; }
public ref class NativeFields abstract sealed {
public:
    static int Verify() {
        PackedFields value = { 7, { 0x12345678U, 5000000002LL }, 3 };
        PackedFields copy = value;
        int calls = 0;
        long long previous = Identity(&copy, calls)->Inner.Count++;
        if (previous != 5000000002LL || calls != 1) return 1;
        Identity(&copy, calls)->Inner.Count += 7;
        if (calls != 2 || copy.Inner.Count != 5000000010LL) return 2;
        unsigned int* word = &copy.Inner.Word;
        *word = 0x12345678U;
        copy.Flag = 9;
        UnionFields choice = { 0x11223344U };
        if (choice.Word != 0x11223344U || value.Inner.Count != 5000000002LL) return 3;
        MacroPackedFields macro = { 7, 5000000002LL };
        NaturalFields natural = { 9, 5000000004LL };
        return NativeFieldsCheck(&copy) + NativePackCheck(&macro, &natural);
    }
};
int main() {
    if (NativeFields::Verify() != 0) return 1;
    System::Console::WriteLine("native POD aggregate, packed nested fields, addresses and single evaluation OK");
    return 0;
}
