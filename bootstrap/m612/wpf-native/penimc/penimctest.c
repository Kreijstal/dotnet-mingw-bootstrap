/* Smoke test of PenImc_cor3.dll's COM side: class factory lookup and the
 * merged proxy/stub entry point.  SPDX-License-Identifier: MIT */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <rpcproxy.h>
#include <stdio.h>
#include "PenImc.h"   /* widl output; CLSIDs come from penimc_i.c */

typedef HRESULT (WINAPI *PFNGCO)(REFCLSID, REFIID, void **);
typedef HRESULT (WINAPI *PFNCUN)(void);
typedef void (WINAPI *PFNPROXYINFO)(const ProxyFileInfo ***, const CLSID **);

int main(int argc, char **argv)
{
    HMODULE h = LoadLibraryA(argc > 1 ? argv[1] : "PenImc_cor3.dll");
    const ProxyFileInfo **pinfo = NULL, **p;
    const CLSID *pclsid = NULL;
    IClassFactory *cf = NULL, *cf2 = NULL;
    HRESULT hr, hr2;
    int nif = 0;

    if (!h) { printf("LoadLibrary failed %lu\n", GetLastError()); return 1; }
    PFNGCO gco = (PFNGCO)(void *)GetProcAddress(h, "DllGetClassObject");
    PFNCUN cun = (PFNCUN)(void *)GetProcAddress(h, "DllCanUnloadNow");
    PFNPROXYINFO gpi = (PFNPROXYINFO)(void *)GetProcAddress(h, "GetProxyDllInfo");

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    gpi(&pinfo, &pclsid);
    for (p = pinfo; p && *p; p++)
        nif += (*p)->TableSize;
    printf("GetProxyDllInfo: %d proxied interfaces\n", nif);

    hr = gco(&CLSID_PimcManager3, &IID_IClassFactory, (void **)&cf);
    printf("DllGetClassObject(PimcManager3) hr=0x%08lx\n", (unsigned long)hr);
    hr2 = gco(&CLSID_NULL, &IID_IClassFactory, (void **)&cf2);
    printf("DllGetClassObject(CLSID_NULL) hr=0x%08lx (expect 0x80040111)\n", (unsigned long)hr2);
    if (cf) {
        printf("DllCanUnloadNow with factory held: hr=0x%08lx\n", (unsigned long)cun());
        IClassFactory_Release(cf);
    }
    printf("DllCanUnloadNow: hr=0x%08lx\n", (unsigned long)cun());
    CoUninitialize();
    return (SUCCEEDED(hr) && hr2 == CLASS_E_CLASSNOTAVAILABLE && nif > 0) ? 0 : 1;
}
