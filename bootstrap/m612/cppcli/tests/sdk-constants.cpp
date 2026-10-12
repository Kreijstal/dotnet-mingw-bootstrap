// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include <windows.h>
#include <dwrite.h>
#include "sdk-constants.hpp"
int main() {
    System::Console::WriteLine(static_cast<int>(DWRITE_FONT_SIMULATIONS_BOLD | DWRITE_FONT_SIMULATIONS_OBLIQUE));
    System::Console::WriteLine(SdkWideConstant(5));
    System::Console::WriteLine(SdkIntegerConstant(2));
    return 0;
}
