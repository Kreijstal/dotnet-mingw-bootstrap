// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include <windows.h>
struct NativeLayout { char First; long long Second; };
int main() {
    int effects = 0;
    if (sizeof(WCHAR) != 2 || sizeof(void*) != 8 || sizeof(GUID) != 16 || alignof(GUID) != 4 || sizeof(NativeLayout) != 16) return 1;
    if (sizeof(++effects) != 4 || effects != 0) return 2;
    System::Console::WriteLine("native sizeof/alignof, SDK layout and unevaluated operands OK");
    return 0;
}
