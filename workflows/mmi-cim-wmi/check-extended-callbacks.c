#define UNICODE
#define _UNICODE
#include <windows.h>
#include <mi.h>
#include <stddef.h>
#include <stdio.h>
_Static_assert(sizeof(MI_OperationCallbacks) == 9 * sizeof(void *), "MI callback table");
static MI_OperationCallback_ResponseType answer;
static unsigned replies;
static int fail_reply;
static MI_Result MI_CALL reply(MI_Operation *operation, MI_OperationCallback_ResponseType response)
{
    if (operation != NULL) return MI_RESULT_INVALID_PARAMETER;
    answer = response;
    ++replies;
    return fail_reply ? MI_RESULT_FAILED : MI_RESULT_OK;
}
__declspec(dllexport) int __cdecl invoke_extended(const MI_OperationCallbacks *callbacks, MI_Instance *error, int mode)
{
    if (!callbacks || !callbacks->promptUser || !callbacks->writeError || !callbacks->writeMessage || !callbacks->writeProgress) return 1;
    replies = 0;
    fail_reply = mode == 3;
    callbacks->promptUser(NULL, NULL, L"prompt-π", MI_PROMPTTYPE_CRITICAL, reply);
    if (replies != 1 || answer != (mode == 1 ? MI_OperationCallback_ResponseType_No : MI_OperationCallback_ResponseType_NoToAll)) return 2;
    if (mode == 1 || mode == 3) return 0;
    replies = 0;
    callbacks->writeError(NULL, NULL, error, reply);
    if (replies != 1 || answer != (mode == 2 ? MI_OperationCallback_ResponseType_No : MI_OperationCallback_ResponseType_YesToAll)) return 3;
    if (mode == 2) return 0;
    callbacks->writeMessage(NULL, NULL, 1, L"message-名");
    callbacks->writeProgress(NULL, NULL, L"activity-é", L"current-π", L"status-名", 42, 17);
    return 0;
}
__declspec(dllexport) int __cdecl test_operation_clone(void)
{
    HMODULE mi = LoadLibraryW(L"mi.dll");
    if (!mi) return 1;
    typedef MI_Result (MI_MAIN_CALL *Init)(MI_Uint32,const MI_Char*,MI_Instance**,MI_Application*);
    Init init = (Init)(void*)GetProcAddress(mi,"MI_Application_InitializeV1");
    MI_Application app = MI_APPLICATION_NULL;
    if (init(0,L"native-option-clone",NULL,&app) != MI_RESULT_OK) return 2;
    MI_OperationOptions original = MI_OPERATIONOPTIONS_NULL, clone = MI_OPERATIONOPTIONS_NULL;
    if (MI_Application_NewOperationOptions(&app,MI_FALSE,&original) != MI_RESULT_OK) return 3;
    MI_Interval value = {0}; value.seconds = 123;
    if (original.ft->SetInterval(&original,L"__MI_OPERATIONOPTIONS_TIMEOUT",&value,0) != MI_RESULT_OK) return 4;
    if (MI_OperationOptions_Clone(&original,&clone) != MI_RESULT_OK) return 5;
    MI_Uint32 index = 0, flags = 0;
    MI_Interval output = {0};
    MI_Result orig = original.ft->GetInterval(&original,L"__MI_OPERATIONOPTIONS_TIMEOUT",&output,&index,&flags);
    MI_Uint32 count = 0;
    MI_OperationOptions_GetOptionCount(&original,&count);
    printf("Native original interval=%u count=%u\n",(unsigned)orig,(unsigned)count);
    for (MI_Uint32 i=0;i<count;i++) {
        const MI_Char *name = NULL; MI_Value v = {0}; MI_Type t = MI_BOOLEAN;
        MI_Result r=MI_OperationOptions_GetOptionAt(&original,i,&name,&v,&t,&flags);
        printf("Native option result=%u type=%u flags=%u\n",(unsigned)r,(unsigned)t,(unsigned)flags);
    }
    MI_Result before = clone.ft->GetInterval(&clone,L"__MI_OPERATIONOPTIONS_TIMEOUT",&output,&index,&flags);
    MI_OperationOptions_Delete(&original);
    MI_Result after = clone.ft->GetInterval(&clone,L"__MI_OPERATIONOPTIONS_TIMEOUT",&output,&index,&flags);
    MI_OperationOptions_Delete(&clone); MI_Application_Close(&app); FreeLibrary(mi);
    fflush(stdout);
    return (int)(before * 100 + after);
}
