using System;
using System.Linq;
using System.Reflection;
using System.Threading.Tasks;

nint[] pointers=new nint[8];
Parallel.For(0,pointers.Length,i=>pointers[i]=WpfStartupCheck.Address());
if(pointers[0]==0||pointers.Any(p=>p!=pointers[0])||!WpfStartupCheck.CreateAndRelease())
    throw new Exception("startup initialization or DWrite factory call failed");
var process=(EventHandler)typeof(AppContext).GetField("ProcessExit",BindingFlags.Static|BindingFlags.NonPublic).GetValue(null);
var domain=(EventHandler)typeof(AppDomain).GetField("DomainUnload",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(AppDomain.CurrentDomain);
bool IsCleanup(Delegate d)=>d.Method.DeclaringType?.Name=="WpfModuleInitialize"&&d.Method.DeclaringType.Assembly==typeof(WpfStartupCheck).Assembly;
var registered=process.GetInvocationList().Where(IsCleanup).ToArray();
if(registered.Length!=1||domain.GetInvocationList().Count(IsCleanup)!=1)
    throw new Exception("shutdown handlers were not registered exactly once");
var cleanup=(EventHandler)registered[0];
cleanup(AppDomain.CurrentDomain,EventArgs.Empty);
cleanup(AppDomain.CurrentDomain,EventArgs.Empty);
if(WpfStartupCheck.Address()!=0) throw new Exception("registered cleanup did not clear the factory pointer");
WpfStartupCheck.Reload();
if(WpfStartupCheck.Address()==0||!WpfStartupCheck.CreateAndRelease()) throw new Exception("loader did not recover after explicit cleanup");
AppDomain.CurrentDomain.ProcessExit+=(sender,args)=>{
    if(WpfStartupCheck.Address()!=0) throw new Exception("actual ProcessExit did not run WPF cleanup first");
    Console.WriteLine("actual WPF ProcessExit cleanup OK");
};
Console.WriteLine("ported WPF startup, concurrent first use, real SDK factory call, registered/idempotent cleanup and reload OK");
