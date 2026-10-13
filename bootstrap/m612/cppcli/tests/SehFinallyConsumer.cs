using System;
using System.Threading;
using System.Threading.Tasks;
if(SehFinally.Return(4)!=4 || SehFinally.Cleanups!=1 || SehFinally.Leave()!=5 || SehFinally.Cleanups!=2 ||
   SehFinally.Loop()!=2 || SehFinally.Cleanups!=6 || SehFinally.Nested()!=6 || SehFinally.Cleanups!=116)
    throw new Exception("SEH spelling: return, leave, break, continue and nested finally execution");
var gate=new object();
try { SehFinally.Throw(gate); throw new Exception("managed exception lost"); }
catch(ArgumentException error) { if(error.Message!="seh cleanup") throw; }
var acquired=Task.Run(()=> { if(!Monitor.TryEnter(gate,1000)) return false; Monitor.Exit(gate); return true; });
if(!acquired.Wait(TimeSpan.FromSeconds(5)) || !acquired.Result || SehFinally.Cleanups!=117)
    throw new Exception("cleanup must release actual Monitor to another thread during unwind");
var owner=new SehFinally {Value=7}; ref int address=ref owner.Address();
GC.Collect(2,GCCollectionMode.Forced,true,true); address=8;
if(owner.Value!=8 || SehFinally.Cleanups!=118) throw new Exception("tracked return through __finally");
Console.WriteLine("__try/__finally: CLR cleanup across return/leave/break/continue/nested regions, managed exception propagation, cross-thread lock release and byref GC PASS");
