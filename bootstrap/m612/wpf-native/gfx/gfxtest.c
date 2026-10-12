/* Smoke test of wpfgfx_cor3.dll beyond symbol lookup: version handshake,
 * a geometry utility call and the embedded shader resources.
 * SPDX-License-Identifier: MIT */
#include <windows.h>
#include <stdio.h>

#include "wgx_sdk_version.h"

typedef HRESULT (WINAPI *PFN_VERSIONCHECK)(UINT);

static int check_res(HMODULE h, int id, const char *name)
{
    HRSRC r = FindResourceA(h, MAKEINTRESOURCEA(id), (LPCSTR)RT_RCDATA);
    DWORD size = r ? SizeofResource(h, r) : 0;
    const DWORD *p = r ? (const DWORD *)LockResource(LoadResource(h, r)) : NULL;
    printf("  resource %-30s id %d: %lu bytes, version token 0x%08lx\n", name, id,
           (unsigned long)size, p ? (unsigned long)p[0] : 0ul);
    return p && (p[0] & 0xfffe0000) == 0xfffe0000 && p[size / 4 - 1] == 0x0000ffff;
}

int main(int argc, char **argv)
{
    HMODULE h = LoadLibraryA(argc > 1 ? argv[1] : "wpfgfx_cor3.dll");
    int ok = 1;
    if (!h) { printf("LoadLibrary failed: %lu\n", GetLastError()); return 1; }
    PFN_VERSIONCHECK vc = (PFN_VERSIONCHECK)(void *)GetProcAddress(h, "MilVersionCheck");
    HRESULT hr1 = vc(MIL_SDK_VERSION), hr2 = vc(0x1234);
    printf("MilVersionCheck(MIL_SDK_VERSION) = 0x%08lx (expect 0)\n", (unsigned long)hr1);
    printf("MilVersionCheck(0x1234)          = 0x%08lx (expect failure)\n", (unsigned long)hr2);
    ok &= hr1 == S_OK && FAILED(hr2);
    /* core/hw/Shaders.h (fxasm.py) and core/hw/ShaderAssemblies/Shaders.h (vkd3d) */
    ok &= check_res(h, 100, "g_PixelShader_Text11A_CTSB_P0");
    ok &= check_res(h, 115, "g_PixelShader_Text20L_GSTB_P0");
    ok &= check_res(h, 900, "VS_ShaderEffects20");
    ok &= check_res(h, 901, "VS_ShaderEffects30");
    ok &= check_res(h, 902, "PS_PassThroughShaderEffect");
    ok &= check_res(h, 903, "PS_BlurH");
    FreeLibrary(h);
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
