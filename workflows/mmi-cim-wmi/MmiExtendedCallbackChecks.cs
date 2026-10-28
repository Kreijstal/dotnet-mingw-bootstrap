using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using Microsoft.Management.Infrastructure;
using Microsoft.Management.Infrastructure.Options;
internal static class MmiExtendedCallbackChecks
{
    const BindingFlags Internal=BindingFlags.Instance|BindingFlags.Static|BindingFlags.NonPublic;
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    delegate int NativeDriver(IntPtr callbacks,IntPtr error,int mode);
    static void Check(bool ok,string message){if(!ok)throw new Exception(message);}
    static object Call(object target,string name,params object[] args)
    {
        try{return target.GetType().GetMethod(name,Internal).Invoke(target,args);}
        catch(TargetInvocationException e){throw e.InnerException;}
    }
    static object Get(object target,string name)=>target.GetType().GetProperty(name,Internal).GetValue(target);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int CloneProbe();
    static void Test(NativeDriver driver,int mode)
    {
        var assembly=typeof(CimSession).Assembly;
        var operationType=assembly.GetType("Microsoft.Management.Infrastructure.Native.MI_Operation");
        var bridgeType=assembly.GetType("Microsoft.Management.Infrastructure.Native.MI_OperationCallbackBridge");
        var factory=assembly.GetType("Microsoft.Management.Infrastructure.Options.Internal.OperationOptionsExtensionMethods");
        int originalCount=(int)bridgeType.GetField("ActiveCount",Internal).GetValue(null);
        int prompts=0,errors=0,messages=0,progress=0;
        using var original=new CimOperationOptions { Timeout=TimeSpan.FromSeconds(123), KeysOnly=true, ClassNamesOnly=true, ShortenLifetimeOfResults=true, EnableMethodResultStreaming=true, Flags=CimOperationFlags.ReportOperationStarted, CancellationToken=new System.Threading.CancellationToken(false) };
        original.PromptUser=(message,type)=>
        {
            prompts++;Check(message=="prompt-π"&&type==CimPromptType.Critical,"Prompt data");
            GC.Collect(2,GCCollectionMode.Forced,true,true);
            if(mode==1)throw new InvalidOperationException("prompt-exception");
            return CimResponseType.NoToAll;
        };
        original.WriteError=error=>
        {
            errors++;Check((uint)error.CimInstanceProperties["Code"].Value==42,"Error instance clone");
            if(mode==2)throw new InvalidOperationException("error-exception");
            return CimResponseType.YesToAll;
        };
        original.WriteMessage=(channel,message)=>{messages++;Check(channel==1&&message=="message-名","Message data");};
        original.WriteProgress=(activity,current,status,percent,seconds)=>
        {progress++;Check(activity=="activity-é"&&current=="current-π"&&status=="status-名"&&percent==42&&seconds==17,"Progress data");};
        using var options=new CimOperationOptions(original);original.Dispose();
        Check(options.Timeout==TimeSpan.FromSeconds(123),"Independent native operation options clone");
        Check(options.KeysOnly&&options.ClassNamesOnly&&options.ShortenLifetimeOfResults&&options.EnableMethodResultStreaming&&options.ReportOperationStarted&&options.CancellationToken.HasValue,"Copied managed operation settings");
        var method=Array.Find(factory.GetMethods(Internal),m=>m.Name=="GetOperationCallbacks"&&m.GetParameters().Length==1);
        var callbacks=method.Invoke(null,new object[]{options});
        var operation=operationType.GetMethod("NewDirectPtr",Internal).Invoke(null,null);
        var bridge=Activator.CreateInstance(bridgeType,Internal,null,new[]{operation,callbacks},null);
        using var errorInstance=new CimInstance("MsysCallbackFixture");
        errorInstance.CimInstanceProperties.Add(CimProperty.Create("Code",(uint)42,CimType.UInt32,CimFlags.None));
        var handle=Get(errorInstance,"InstanceHandle");var pointer=(IntPtr)Get(handle,"Ptr");
        try
        {
            Check((int)bridgeType.GetField("ActiveCount",Internal).GetValue(null)==originalCount+1,"Callback root acquisition");
            GC.Collect(2,GCCollectionMode.Forced,true,true);GC.WaitForPendingFinalizers();
            Check(driver((IntPtr)Get(bridge,"Pointer"),pointer,mode)==0,"mi.h callback ABI/reply result");
            Check(prompts==1,"Prompt delivery");
            if(mode==0)
            {
                Check(errors==1&&messages==1&&progress==1,"Extended callback delivery");
                Call(operation,"ThrowCallbackError");
            }
            else
            {
                try{Call(operation,"ThrowCallbackError");throw new Exception("Callback exception lost");}
                catch(InvalidOperationException e){Check((mode==1&&e.Message=="prompt-exception")||(mode==2&&e.Message=="error-exception"),"Original callback exception");}
                catch(CimException e){Check(mode==3&&e.NativeErrorCode==NativeErrorCode.Failed,"Native reply failure");}
            }
        }
        finally
        {
            ((IDisposable)bridge).Dispose();((IDisposable)operation).Dispose();
            Check((int)bridgeType.GetField("ActiveCount",Internal).GetValue(null)==originalCount,"Callback root cleanup");
        }
    }
    internal static void Run()=>Run(Path.Combine(AppContext.BaseDirectory,"mmi-callback-driver.dll"));
    internal static void Run(string library)
    {
        IntPtr native=NativeLibrary.Load(Path.GetFullPath(library));
        try
        {
            var driver=Marshal.GetDelegateForFunctionPointer<NativeDriver>(NativeLibrary.GetExport(native,"invoke_extended"));
            var clone=Marshal.GetDelegateForFunctionPointer<CloneProbe>(NativeLibrary.GetExport(native,"test_operation_clone"));
            int nativeResult=clone();
            Check(nativeResult==0||nativeResult==606,"Native option clone probe failed: "+nativeResult);
            Console.WriteLine("Native interval clone probe (before/after MI_Result): "+nativeResult);
            for(int i=0;i<4;i++)Test(driver,i);
        }
        finally{NativeLibrary.Free(native);}
        Console.WriteLine("PASS mi.h native prompt/error/message/progress callback table, Unicode values, cloned error instance, reply values/exactly-once replies, managed/reply exceptions, compacting GC, options clone and root cleanup");
    }
}
