// SPDX-License-Identifier: MIT
#include <windows.h>
#include <cstdio>
#include "intsafe_private_copy.h"
int main() {
    UINT u = 0;
    ULONG value = 0;
    if (UIntAdd(0xffffffffU, 0, &u) != S_OK || u != 0xffffffffU) return 1;
    if (UIntAdd(0xffffffffU, 1, &u) != INTSAFE_E_ARITHMETIC_OVERFLOW || u != 0xffffffffU) return 2;
    if (ULongLongToULong(0xffffffffULL, &value) != S_OK || value != 0xffffffffUL) return 3;
    if (ULongLongToULong(0x100000000ULL, &value) != INTSAFE_E_ARITHMETIC_OVERFLOW || value != 0xffffffffUL) return 4;
    if (ULongMult(0x10000UL, 0x10000UL, &value) != INTSAFE_E_ARITHMETIC_OVERFLOW || value != 0xffffffffUL) return 5;
    if (ULongMult(21, 2, &value) != S_OK || value != 42) return 6;
    if (ULongSub(42, 2, &value) != S_OK || value != 40) return 7;
    if (ULongSub(0, 1, &value) != INTSAFE_E_ARITHMETIC_OVERFLOW || value != 0xffffffffUL) return 8;
    std::puts("WPF checked integers OK");
    return 0;
}
