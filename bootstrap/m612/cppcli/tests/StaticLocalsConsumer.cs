using System;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;
using System.Reflection;
foreach(var type in typeof(StaticLocals).Assembly.GetTypes())
    foreach(var method in type.GetMethods(BindingFlags.Public|BindingFlags.Static|BindingFlags.Instance|BindingFlags.DeclaredOnly))
        if(method.GetMethodBody()!=null) RuntimeHelpers.PrepareMethod(method.MethodHandle);
using var start=new ManualResetEventSlim(false);
var values=new int[16];
var threads=Enumerable.Range(0,16).Select(i=>new Thread(()=> { start.Wait(); values[i]=StaticLocals.Cached(42); })).ToArray();
foreach(var thread in threads) thread.Start();
start.Set(); foreach(var thread in threads) thread.Join();
if(values.Any(value=>value!=42) || StaticLocals.Initializations!=1 || StaticLocals.Cached(43)!=42)
    throw new Exception("static initialization under simultaneous callers");
if(new StaticLocals(44).Instance()!=44 || new StaticLocals(45).Instance()!=44)
    throw new Exception("instance initializer must use first reaching receiver");
try { StaticLocals.Retry(); throw new Exception("initializer exception was lost"); }
catch(ArgumentException) {}
var retry=Task.Run(()=>StaticLocals.Retry());
if(!retry.Wait(TimeSpan.FromSeconds(10)) || retry.Result!=79 || StaticLocals.Retry()!=79 || StaticLocals.Attempts!=2)
    throw new Exception("exception releases initialization lock and permits retry");
var seed=new string("λ🚀".ToCharArray());
if(!ReferenceEquals(seed,StaticLocals.Text(seed))) throw new Exception("static handle initializer identity");
ref int address=ref StaticLocals.Address(); address=90;
ref var data=ref StaticLocals.Data(seed); data.Value=91;
for(int i=0;i<20;i++) {
    GC.Collect(2,GCCollectionMode.Forced,true,true);
    if(!ReferenceEquals(seed,StaticLocals.Text("unused")) || address!=90 || data.Value!=91 || !ReferenceEquals(seed,data.Text))
        throw new Exception("static managed/value/byref lifetime across compacting GC");
}
if(StaticLocals.Count()!=1 || StaticLocals.Count()!=2 || StaticLocals.Scoped(true)!=83 || StaticLocals.Scoped(false)!=85 ||
   StaticLocals.Scoped(true)!=84 || StaticLocals.Scoped(false)!=86)
    throw new Exception("zero initialization, persistence and distinct lexical static locals");
Console.WriteLine("static locals: first reaching receiver/arguments, sixteen concurrent callers, exception retry, distinct scopes, zero/persistent storage and managed/byref lifetime across GC PASS");
