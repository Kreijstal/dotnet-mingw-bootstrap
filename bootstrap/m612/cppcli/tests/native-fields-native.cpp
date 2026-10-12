// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "native-fields.hpp"
static_assert(sizeof(PackedFields) == 15 && alignof(PackedFields) == 1);
static_assert(sizeof(MacroPackedFields) == 10 && alignof(MacroPackedFields) == 2);
static_assert(sizeof(NaturalFields) == 16 && alignof(NaturalFields) == 8);
extern "C" __declspec(dllexport) int NativeFieldsCheck(const void* pointer) {
    const auto& value = *static_cast<const PackedFields*>(pointer);
    return value.Tag == 7 && value.Inner.Word == 0x12345678U && value.Inner.Count == 5000000010LL && value.Flag == 9 ? 0 : 4;
}
extern "C" __declspec(dllexport) int NativePackCheck(const void* macro, const void* natural) {
    const auto& first = *static_cast<const MacroPackedFields*>(macro);
    const auto& second = *static_cast<const NaturalFields*>(natural);
    return first.Tag == 7 && first.Count == 5000000002LL && second.Tag == 9 && second.Count == 5000000004LL ? 0 : 5;
}
