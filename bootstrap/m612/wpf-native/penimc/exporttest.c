/* LoadLibrary the DLL and GetProcAddress every name listed in the EXPORTS
 * section of a (preprocessed) .def file.  usage: exporttest DLL DEF
 * SPDX-License-Identifier: MIT */
#include <windows.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    char line[512];
    int in_exports = 0, n = 0, missing = 0;
    HMODULE h;
    FILE *f;

    if (argc != 3) {
        fprintf(stderr, "usage: %s DLL DEF\n", argv[0]);
        return 2;
    }
    h = LoadLibraryA(argv[1]);
    if (!h) {
        printf("LoadLibrary(%s) failed: error %lu\n", argv[1], GetLastError());
        return 1;
    }
    printf("LoadLibrary(%s) ok\n", argv[1]);
    f = fopen(argv[2], "r");
    if (!f) {
        perror(argv[2]);
        return 2;
    }
    while (fgets(line, sizeof line, f)) {
        char name[256];
        char *c = strchr(line, ';');
        if (c)
            *c = 0;
        if (sscanf(line, " %255s", name) != 1)
            continue;
        if (!strcmp(name, "EXPORTS")) {
            in_exports = 1;
            continue;
        }
        if (!in_exports || !strcmp(name, "LIBRARY"))
            continue;
        c = strchr(name, '=');      /* name=internal */
        if (c)
            *c = 0;
        n++;
        if (GetProcAddress(h, name))
            printf("  ok       %s\n", name);
        else {
            printf("  MISSING  %s\n", name);
            missing++;
        }
    }
    fclose(f);
    printf("%d exports checked, %d missing\n", n, missing);
    FreeLibrary(h);
    return missing ? 1 : (n ? 0 : 1);
}
