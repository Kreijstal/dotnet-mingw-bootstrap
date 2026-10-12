// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "system-header.hpp"
// This specialization is accepted only if the adapted header remains a system
// header. The identical specialization in user source must still be rejected.
int main() { System::Console::WriteLine("system header OK"); return 0; }
