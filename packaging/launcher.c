// Native relocatable launcher. Packaging supplies launcher-config.h.
// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#include <stdlib.h>
#include <stdio.h>
#include <wctype.h>

#ifndef TARGET_RELATIVE
#error TARGET_RELATIVE must name the executable relative to this launcher's directory
#endif
#ifndef TOOL_RELATIVE
#define TOOL_RELATIVE NULL
#endif

static BOOL WINAPI control(DWORD event) {
    // The child shares the console and receives the event itself. Keep the
    // launcher alive to collect its exit status.
    return event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT;
}

static const wchar_t *arguments(void) {
    const wchar_t *text = GetCommandLineW();
    while (iswspace(*text)) ++text;
    if (*text == L'"') {
        ++text;
        while (*text && *text != L'"') ++text;
        if (*text) ++text;
    } else {
        while (*text && !iswspace(*text)) ++text;
    }
    return text; // Preserve Windows quoting/escaping of all remaining arguments.
}

static wchar_t *relative(const wchar_t *directory, const wchar_t *part) {
    wchar_t *joined = calloc(wcslen(directory) + wcslen(part) + 2, sizeof(wchar_t));
    if (!joined) return NULL;
    wcscpy(joined, directory); wcscat(joined, L"/"); wcscat(joined, part);
    DWORD needed = GetFullPathNameW(joined, 0, NULL, NULL);
    wchar_t *full = needed ? calloc(needed + 1, sizeof(wchar_t)) : NULL;
    if (full && !GetFullPathNameW(joined, needed + 1, full, NULL)) { free(full); full = NULL; }
    free(joined); return full;
}

int wmain(void) {
    wchar_t *directory = calloc(32768, sizeof(wchar_t));
    if (!directory) return 1;
    DWORD length = GetModuleFileNameW(NULL, directory, 32768);
    if (!length || length >= 32768) { free(directory); return 1; }
    wchar_t *slash = wcsrchr(directory, L'\\');
    if (!slash) { free(directory); return 1; }
    *slash = 0;
    wchar_t *target = relative(directory, TARGET_RELATIVE);
    wchar_t *tool = TOOL_RELATIVE ? relative(directory, TOOL_RELATIVE) : NULL;
    if (!target || (TOOL_RELATIVE && !tool)) { free(directory); free(target); free(tool); return 1; }
    const wchar_t *suffix = arguments();
    size_t size = wcslen(target) + wcslen(suffix) + (tool ? wcslen(tool) : 0) + 10;
    wchar_t *command = calloc(size, sizeof(wchar_t));
    if (!command) { free(directory); free(target); free(tool); return 1; }
    if (tool) swprintf(command, size, L"\"%ls\" \"%ls\"%ls", target, tool, suffix);
    else swprintf(command, size, L"\"%ls\"%ls", target, suffix);
    STARTUPINFOW start = {0}; start.cb = sizeof(start);
    PROCESS_INFORMATION child = {0};
    SetConsoleCtrlHandler(control, TRUE);
    BOOL ok = CreateProcessW(target, command, NULL, NULL, TRUE, 0, NULL, NULL, &start, &child);
    DWORD code = 1;
    if (!ok) fwprintf(stderr, L"launcher: cannot start %ls (Windows error %lu)\n", target, GetLastError());
    else {
        CloseHandle(child.hThread);
        if (WaitForSingleObject(child.hProcess, INFINITE) == WAIT_OBJECT_0) GetExitCodeProcess(child.hProcess, &code);
        CloseHandle(child.hProcess);
    }
    free(directory); free(target); free(tool); free(command);
    return (int)code;
}
