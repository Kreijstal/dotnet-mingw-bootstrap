#include <windows.h>
extern "C" __declspec(dllexport) int NativePinned(const wchar_t* chars, unsigned short* values) {
    if (chars[3] != 0x00e9 || chars[5] != 0x6771 || chars[6] != 0x4eac ||
        chars[8] != 0xd83d || chars[9] != 0xde00 || chars[10] != 0 || values[0] != 65535) return -1;
    Sleep(150); // The C# caller forces compacting collections during this call.
    if (chars[3] != 0x00e9 || chars[5] != 0x6771 || chars[6] != 0x4eac ||
        chars[8] != 0xd83d || chars[9] != 0xde00 || values[0] != 65535) return -2;
    values[1] = 65530;
    return 42;
}
extern "C" __declspec(dllexport) int NativePinnedWord(unsigned int* value) {
    if (*value != 0x44332211u) return -1;
    Sleep(150);
    if (*value != 0x44332211u) return -2;
    *value = 0x88776655u;
    return 42;
}
