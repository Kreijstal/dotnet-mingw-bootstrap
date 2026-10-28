#define UNICODE
#define _UNICODE
#include <windows.h>
#include <mi.h>
#include <string.h>
_Static_assert(sizeof(MI_OperationCallbacks)==9*sizeof(void*),"callback layout");
static unsigned acknowledgements;
static int fail_ack;
static MI_Operation *expected_operation;
static MI_Value *borrowed;
static MI_Result MI_CALL ack(MI_Operation *operation)
{
    if(operation!=expected_operation)return MI_RESULT_INVALID_PARAMETER;
    acknowledgements++;
    if(borrowed)memset(borrowed,0xcc,sizeof(*borrowed));
    return fail_ack?MI_RESULT_FAILED:MI_RESULT_OK;
}
static void emit_stream(const MI_OperationCallbacks *callbacks,MI_Type type,MI_Value *value)
{
    borrowed=value;
    callbacks->streamedParameterResult(expected_operation,NULL,L"Output-π",type,value,ack);
    borrowed=NULL;
}
__declspec(dllexport) unsigned __cdecl value_size(void){return (unsigned)sizeof(MI_Value);}
__declspec(dllexport) int __cdecl stream_results(const MI_OperationCallbacks *callbacks,MI_Operation *operation,MI_Instance *instance,MI_Instance *final_result,int mode)
{
    if(!callbacks||!callbacks->streamedParameterResult||!callbacks->instanceResult)return -1;
    expected_operation=operation;acknowledgements=0;fail_ack=mode==3;
    MI_Value value={0};
    if(mode==2||mode==3||mode==4){
        value.uint32=42;emit_stream(callbacks,mode==4?(MI_Type)99:MI_UINT32,&value);
        // A provider may deliver a queued result after cancellation. It must
        // still be acknowledged without another user notification.
        memset(&value,0,sizeof(value));value.instance=instance;emit_stream(callbacks,MI_INSTANCE,&value);
        return acknowledgements==2?0:-2;
    }
    if(mode==5){emit_stream(callbacks,MI_STRING,NULL);return acknowledgements==1?0:-3;}
    if(mode==6){for(unsigned type=16;type<32;type++){memset(&value,0,sizeof(value));emit_stream(callbacks,(MI_Type)type,&value);}}
    else {
        MI_Boolean booleans[]={MI_TRUE,MI_FALSE};MI_Uint8 u8[]={123,42};MI_Sint8 i8[]={-12,-42};
        MI_Uint16 u16[]={1234,42};MI_Sint16 i16[]={-1234,-42};MI_Uint32 u32[]={123456,42};MI_Sint32 i32[]={-123456,-42};
        MI_Uint64 u64[]={1234567890123ULL,42};MI_Sint64 i64[]={-1234567890123LL,-42};
        MI_Real32 f32[]={1.25f,2.5f};MI_Real64 f64[]={1.25,2.5};MI_Char16 chars[]={0x03c0,0x540d};
        MI_Datetime dates[2]={{0},{0}};dates[0].isTimestamp=MI_TRUE;dates[0].u.timestamp.year=2026;dates[0].u.timestamp.month=10;dates[0].u.timestamp.day=26;
        dates[1].isTimestamp=MI_FALSE;dates[1].u.interval.seconds=42;
        MI_Char text[]=L"stream-π-名";MI_Char *strings[]={text,NULL};MI_Instance *instances[]={instance,NULL};
        for(unsigned type=0;type<16;type++){
            memset(&value,0,sizeof(value));
            switch(type){
                case MI_BOOLEAN:value.boolean=booleans[0];break;case MI_UINT8:value.uint8=u8[0];break;case MI_SINT8:value.sint8=i8[0];break;
                case MI_UINT16:value.uint16=u16[0];break;case MI_SINT16:value.sint16=i16[0];break;case MI_UINT32:value.uint32=u32[0];break;
                case MI_SINT32:value.sint32=i32[0];break;case MI_UINT64:value.uint64=u64[0];break;case MI_SINT64:value.sint64=i64[0];break;
                case MI_REAL32:value.real32=f32[0];break;case MI_REAL64:value.real64=f64[0];break;case MI_CHAR16:value.char16=chars[0];break;
                case MI_DATETIME:value.datetime=dates[0];break;case MI_STRING:value.string=text;break;case MI_REFERENCE:value.reference=instance;break;case MI_INSTANCE:value.instance=instance;break;
            }
            emit_stream(callbacks,(MI_Type)type,&value);
        }
#define ARRAY_CASE(kind,member,data_source) case kind:value.member.data=data_source;value.member.size=2;break
        for(unsigned type=16;type<32;type++){
            memset(&value,0,sizeof(value));
            switch(type){
                ARRAY_CASE(MI_BOOLEANA,booleana,booleans);ARRAY_CASE(MI_UINT8A,uint8a,u8);ARRAY_CASE(MI_SINT8A,sint8a,i8);
                ARRAY_CASE(MI_UINT16A,uint16a,u16);ARRAY_CASE(MI_SINT16A,sint16a,i16);ARRAY_CASE(MI_UINT32A,uint32a,u32);
                ARRAY_CASE(MI_SINT32A,sint32a,i32);ARRAY_CASE(MI_UINT64A,uint64a,u64);ARRAY_CASE(MI_SINT64A,sint64a,i64);
                ARRAY_CASE(MI_REAL32A,real32a,f32);ARRAY_CASE(MI_REAL64A,real64a,f64);ARRAY_CASE(MI_CHAR16A,char16a,chars);
                ARRAY_CASE(MI_DATETIMEA,datetimea,dates);ARRAY_CASE(MI_STRINGA,stringa,strings);ARRAY_CASE(MI_REFERENCEA,referencea,instances);ARRAY_CASE(MI_INSTANCEA,instancea,instances);
            }
            emit_stream(callbacks,(MI_Type)type,&value);
        }
#undef ARRAY_CASE
        memset(text,'X',sizeof(text));memset(booleans,0,sizeof(booleans));memset(u32,0,sizeof(u32));
    }
    callbacks->instanceResult(operation,NULL,final_result,MI_FALSE,MI_RESULT_OK,NULL,NULL,ack);
    return acknowledgements==(mode==6?17u:33u)?0:-4;
}
typedef struct FixtureOperation {
    MI_Operation operation;
    LONG closed;
    LONG cancelled;
} FixtureOperation;
static MI_Result MI_CALL close_fixture(MI_Operation *operation)
{
    InterlockedIncrement(&((FixtureOperation*)operation)->closed);
    return MI_RESULT_OK;
}
static MI_Result MI_CALL cancel_fixture(MI_Operation *operation,MI_CancellationReason reason)
{
    (void)reason;InterlockedIncrement(&((FixtureOperation*)operation)->cancelled);
    return MI_RESULT_OK;
}
static const MI_OperationFT fixture_operations={.Close=close_fixture,.Cancel=cancel_fixture};
__declspec(dllexport) MI_Operation *__cdecl create_fixture_operation(void)
{
    FixtureOperation *value=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*value));
    if(value)value->operation.ft=&fixture_operations;
    return value?&value->operation:NULL;
}
__declspec(dllexport) int __cdecl inspect_fixture_operation(MI_Operation *operation,int mode)
{
    FixtureOperation *value=(FixtureOperation*)operation;
    if(mode==0)return (int)value->closed;
    if(mode==1)return (int)value->cancelled;
    HeapFree(GetProcessHeap(),0,value);return 0;
}
