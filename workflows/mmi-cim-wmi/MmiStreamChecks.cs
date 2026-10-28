using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;
using Microsoft.Management.Infrastructure;
using Microsoft.Management.Infrastructure.Options;
internal static class MmiStreamChecks
{
    const BindingFlags Internal=BindingFlags.Instance|BindingFlags.Static|BindingFlags.NonPublic;
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]delegate int Driver(IntPtr callbacks,IntPtr operation,IntPtr instance,IntPtr final,int mode);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]delegate uint Size();
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]delegate IntPtr CreateOperation();
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]delegate int InspectOperation(IntPtr operation,int mode);
    static void Check(bool ok,string text){if(!ok)throw new Exception(text);}
    static object Call(object target,string name,params object[] args)
    {try{return target.GetType().GetMethod(name,Internal|BindingFlags.Public).Invoke(target,args);}catch(TargetInvocationException e){throw e.InnerException;}}
    static object Get(object target,string name)=>target.GetType().GetProperty(name,Internal).GetValue(target);
    static IntPtr Pointer(object target)=>(IntPtr)Get(target,"Ptr");
    sealed class Observer:IObserver<CimMethodResultBase>
    {
        internal List<CimMethodResultBase> Values=new List<CimMethodResultBase>();
        internal Exception Error;
        internal int ErrorCount;
        internal int Completed;
        internal int FinalValues;
        internal bool Throw;
        public void OnNext(CimMethodResultBase value)
        {
            if(Throw)throw new InvalidOperationException("stream-observer-error");
            if(value is CimMethodStreamedResult stream && stream.ItemValue is CimInstance instance)
                Check((uint)instance.CimInstanceProperties["Code"].Value==42,"Instance invalid inside callback");
            if(value is CimMethodResult final){Check(Convert.ToUInt32(final.ReturnValue.Value)==0,"Final value during callback");FinalValues++;}
            Values.Add(value);GC.Collect(2,GCCollectionMode.Forced,true,true);GC.WaitForPendingFinalizers();
        }
        public void OnError(Exception error){Error=error;ErrorCount++;}
        public void OnCompleted(){Completed++;}
    }
    static void CheckItem(CimMethodStreamedResult value,int type,int item,bool shortened,Guid id)
    {
        Check(value.ParameterName=="Output-π","Stream parameter name");
        Check(value.ItemType==(CimType)(type+1),"Stream element type "+type);
        object actual=value.ItemValue;
        if((type==13||type==14||type==15)&&item==1){Check(actual==null,"Null array element");return;}
        switch(type)
        {
            case 0:Check(actual is bool b&&b==(item==0),"Boolean");break;
            case 1:Check(actual is byte&&Equals(actual,item==0?(byte)123:(byte)42),"UInt8");break;
            case 2:Check(actual is sbyte&&Equals(actual,item==0?(sbyte)-12:(sbyte)-42),"SInt8");break;
            case 3:Check(actual is ushort&&Equals(actual,item==0?(ushort)1234:(ushort)42),"UInt16");break;
            case 4:Check(actual is short&&Equals(actual,item==0?(short)-1234:(short)-42),"SInt16");break;
            case 5:Check(actual is uint&&Equals(actual,item==0?(uint)123456:(uint)42),"UInt32");break;
            case 6:Check(actual is int&&Equals(actual,item==0?-123456:-42),"SInt32");break;
            case 7:Check(actual is ulong&&Equals(actual,item==0?1234567890123UL:42UL),"UInt64");break;
            case 8:Check(actual is long&&Equals(actual,item==0?-1234567890123L:-42L),"SInt64");break;
            case 9:Check(actual is float&&Equals(actual,item==0?1.25f:2.5f),"Real32");break;
            case 10:Check(actual is double&&Equals(actual,item==0?1.25:2.5),"Real64");break;
            case 11:Check(actual is char&&Equals(actual,item==0?'π':'名'),"Char16");break;
            case 12:Check(item==0?actual is DateTime date&&date.Year==2026&&date.Month==10&&date.Day==26:actual is TimeSpan span&&span==TimeSpan.FromSeconds(42),"Datetime");break;
            case 13:Check((string)actual=="stream-π-名","Copied Unicode string");break;
            case 14:case 15:
                var instance=(CimInstance)actual;
                if(shortened)
                {
                    try{var unused=instance.CimInstanceProperties;throw new Exception("Short lifetime instance retained");}catch(ObjectDisposedException){}
                }
                else
                {
                    Check((uint)instance.CimInstanceProperties["Code"].Value==42,"Instance clone after native source disposal");
                    Check((Guid)Call(instance,"GetCimSessionInstanceId")==id,"Instance session id");
                    Check((string)Call(instance,"GetCimSessionComputerName")=="stream-fixture","Instance session computer");
                }
                break;
        }
    }
    static void Test(Driver driver,CreateOperation create,InspectOperation inspect,int mode)
    {
        var asm=typeof(CimSession).Assembly;
        Type proxyType=asm.GetType("Microsoft.Management.Infrastructure.Internal.Operations.CimAsyncMethodResultObserverProxy");
        Type callbacksType=asm.GetType("Microsoft.Management.Infrastructure.Native.MI_OperationCallbacks");
        Type bridgeType=asm.GetType("Microsoft.Management.Infrastructure.Native.MI_OperationCallbackBridge");
        Type operationType=asm.GetType("Microsoft.Management.Infrastructure.Internal.Operations.CimOperation");
        int roots=(int)bridgeType.GetField("ActiveCount",Internal).GetValue(null);
        var observer=new Observer { Throw=mode==2 };Guid id=Guid.NewGuid();
        var proxy=Activator.CreateInstance(proxyType,Internal,null,new object[]{observer,id,"stream-fixture",mode==1},null);
        var callbacks=Activator.CreateInstance(callbacksType,true);
        using var options=new CimOperationOptions { EnableMethodResultStreaming=true };
        Call(proxy,"RegisterAcceptedAsyncCallbacks",callbacks,options);
        IntPtr operationPointer=create();Check(operationPointer!=IntPtr.Zero,"Native operation fixture allocation");
        Type nativeType=asm.GetType("Microsoft.Management.Infrastructure.Native.MI_Operation");
        var nativeOperation=nativeType.GetMethod("NewFromDirectPtr",Internal).Invoke(null,new object[]{operationPointer});
        var operation=Activator.CreateInstance(operationType,Internal,null,new object[]{nativeOperation,null},null);
        Call(proxy,"SetOperation",operation);
        var bridge=Activator.CreateInstance(bridgeType,Internal,null,new[]{nativeOperation,callbacks},null);
        var input=new CimInstance("StreamFixture");input.CimInstanceProperties.Add(CimProperty.Create("Code",(uint)42,CimType.UInt32,CimFlags.None));
        var instanceType=asm.GetType("Microsoft.Management.Infrastructure.Native.MI_Instance");
        var duplicate=instanceType.GetMethod("NewFromDirectPtr",Internal).Invoke(null,new object[]{Pointer(Get(input,"InstanceHandle"))});
        ((IDisposable)duplicate).Dispose();((IDisposable)duplicate).Dispose();
        Check((IntPtr)duplicate.GetType().GetField("allocatedData",Internal).GetValue(duplicate)==IntPtr.Zero,"Indirect wrapper release address");
        Check((uint)input.CimInstanceProperties["Code"].Value==42,"Borrowed wrapper release changed native instance");
        using var final=new CimInstance("StreamFinal");final.CimInstanceProperties.Add(CimProperty.Create("ReturnValue",(uint)0,CimType.UInt32,CimFlags.Out));
        try
        {
            Check(driver((IntPtr)Get(bridge,"Pointer"),operationPointer,Pointer(Get(input,"InstanceHandle")),Pointer(Get(final,"InstanceHandle")),mode)==0,"Native stream callback/ack count");
            input.Dispose();GC.Collect(2,GCCollectionMode.Forced,true,true);GC.WaitForPendingFinalizers();
            if(mode==2||mode==3||mode==4)Check(observer.ErrorCount==1&&observer.Completed==0&&observer.Values.Count==(mode==3?1:0),"Single terminal error and suppressed queued result");
            if(mode==2)Check(observer.Error is InvalidOperationException&&observer.Error.Message=="stream-observer-error","Observer exception propagated");
            else if(mode==3)Check(observer.Error is CimException c&&c.NativeErrorCode==NativeErrorCode.Failed,"Ack failure propagated");
            else if(mode==4)Check(observer.Error is ArgumentOutOfRangeException,"Invalid native type contained");
            else if(mode==5)
            {Check(observer.Error==null&&observer.Values.Count==1&&observer.Values[0] is CimMethodStreamedResult r&&r.ItemValue==null&&r.ItemType==CimType.String,"Null native value");}
            else
            {
                Check(observer.Error==null&&observer.Completed==1,"Final completion: "+observer.Error);
                var streams=observer.Values.OfType<CimMethodStreamedResult>().ToArray();
                Check(streams.Length==(mode==6?0:48),"Array chunks flattened into individual items");
                if(mode!=6)
                {
                    for(int i=0;i<16;i++)CheckItem(streams[i],i,0,mode==1,id);
                    for(int i=0;i<16;i++)for(int j=0;j<2;j++)CheckItem(streams[16+2*i+j],i,j,mode==1,id);
                }
                var finalValue=observer.Values.OfType<CimMethodResult>().Single();
                Check(observer.FinalValues==1,"Regular final method result delivery");
                if(mode==1){try{var unused=finalValue.ReturnValue;throw new Exception("Shortened final method result retained");}catch(ObjectDisposedException){}}
                else Check(Convert.ToUInt32(finalValue.ReturnValue.Value)==0,"Regular final method result");
            }
        }
        finally
        {
            input.Dispose();((IDisposable)bridge).Dispose();((IDisposable)operation).Dispose();
            Check(SpinWait.SpinUntil(()=>inspect(operationPointer,0)==1,TimeSpan.FromSeconds(10)),"Native close worker");
            Check(SpinWait.SpinUntil(()=>(IntPtr)nativeOperation.GetType().GetField("allocatedData",Internal).GetValue(nativeOperation)==IntPtr.Zero,TimeSpan.FromSeconds(10)),"Native wrapper disposal");
            if(mode==2||mode==3||mode==4)Check(inspect(operationPointer,1)==1,"Native cancellation after callback error");
            inspect(operationPointer,2);
            foreach(var value in observer.Values)
            {
                (value as IDisposable)?.Dispose();
                if(value is CimMethodStreamedResult r)(r.ItemValue as CimInstance)?.Dispose();
            }
            Check((int)bridgeType.GetField("ActiveCount",Internal).GetValue(null)==roots,"Stream root cleanup");
        }
    }
    internal static void Run()=>Run(Path.Combine(AppContext.BaseDirectory,"mmi-stream-driver.dll"));
    internal static void Run(string library)
    {
        IntPtr native=NativeLibrary.Load(Path.GetFullPath(library));
        try
        {
            var size=Marshal.GetDelegateForFunctionPointer<Size>(NativeLibrary.GetExport(native,"value_size"));
            var type=typeof(CimSession).Assembly.GetType("Microsoft.Management.Infrastructure.Native.MI_Value");
            Check((int)type.GetField("MI_ValueSize",Internal).GetValue(null)==size(),"MI_Value size vs mi.h");
            var driver=Marshal.GetDelegateForFunctionPointer<Driver>(NativeLibrary.GetExport(native,"stream_results"));
            var create=Marshal.GetDelegateForFunctionPointer<CreateOperation>(NativeLibrary.GetExport(native,"create_fixture_operation"));
            var inspect=Marshal.GetDelegateForFunctionPointer<InspectOperation>(NativeLibrary.GetExport(native,"inspect_fixture_operation"));
            for(int mode=0;mode<=6;mode++){Console.WriteLine("Streaming mode "+mode);Test(driver,create,inspect,mode);}
        }
        finally{NativeLibrary.Free(native);}
        Console.WriteLine("PASS native stream ABI for all 32 MI types, scalar/chunk flattening, Unicode/null/empty values, clone lifetimes/session metadata, shortening, errors/ack failures, regular final result and GC/root cleanup");
    }
}
