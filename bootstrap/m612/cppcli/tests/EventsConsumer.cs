using System;
using System.Collections.Generic;
using System.Reflection;
class Program {
    static void Check(bool value,string message) { if(!value) throw new Exception(message); }
    static void Main() {
        var source=new EventSource();
        Check(source.Context(20,22)==42,"contextual property/event parameter names");
        source.Fire(0); int empty=0; source.FireByRef(ref empty); source.FireImported();
        var calls=new List<int>();
        Changed first=x=>calls.Add(x), second=x=>calls.Add(x+1);
        source.Automatic+=first; EventClient.Add(source,second); source.Automatic+=first;
        source.Custom+=second;
        source.Fire(40);
        Check(string.Join(",",calls)=="40,41,40,41","automatic/custom ordering"); calls.Clear();
        EventClient.Remove(source,first); source.Fire(40);
        Check(string.Join(",",calls)=="40,41,41" && EventClient.Receivers==2,"remove last duplicate and receiver once"); calls.Clear();
        source.Custom-=second; source.Subscribe(first); source.Unsubscribe(first);
        Check(source.Adds==2 && source.Removes==2 && source.PublicAfter()==42,"original accessor bodies/access restoration");
        var derived=new DerivedEvents();
        EventSource baseView=derived; baseView.Custom+=first; derived.FireDerived(41);
        Check(calls.Count==1 && calls[0]==42 && derived.Adds==1,"virtual add and protected raise"); calls.Clear();
        baseView.Custom-=first; derived.FireDerived(0); Check(calls.Count==0,"virtual remove");
        Adjust adjust=(ref int x)=>x+=2, multiply=(ref int x)=>x*=3;
        source.ByReference+=adjust; source.ByReference+=multiply;
        int v=4; source.FireByRef(ref v); Check(v==18,"byref automatic event");
        int imported=0; source.Imported+=()=>++imported; source.FireImported(); Check(imported==1,"imported delegate signature");
        var automatic=typeof(EventSource).GetEvent("Automatic")!;
        var custom=typeof(EventSource).GetEvent("Custom")!;
        Check(automatic.EventHandlerType==typeof(Changed) && automatic.AddMethod!.IsSpecialName && automatic.RemoveMethod!.IsSpecialName,"CLR event rows");
        Check(automatic.GetRaiseMethod(true)!.IsPrivate && custom.GetRaiseMethod(true)!.IsFamily && custom.AddMethod!.IsVirtual,"accessor visibility/virtual metadata");
        Check(typeof(EventSource).GetField("Automatic",BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance)==null,"event facade not emitted as storage");
        source.AddInternal(first); source.FireInternal(42);
        Check(calls.Count==1 && calls[0]==42,"same assembly internal event"); calls.Clear();
        var internalEvent=typeof(EventSource).GetEvent("InternalEvent",BindingFlags.Instance|BindingFlags.NonPublic)!;
        Check(internalEvent.GetAddMethod(true)!.IsAssembly && internalEvent.GetRemoveMethod(true)!.IsAssembly,"internal accessor metadata");
        var importedSource=new EventSource();
        ImportedEventClient.Add(importedSource,first); importedSource.Fire(21);
        Check(string.Join(",",calls)=="21,21","second CLI assembly imported events"); calls.Clear();
        ImportedEventClient.Remove(importedSource,first); importedSource.Fire(0); Check(calls.Count==0,"imported event removal");
        var result=new ResultEvent(); Compute compute=x=>x+1;
        Check(result.Call(1)==-1,"custom return without handler"); result.Evaluate+=compute;
        Check(result.Call(41)==42,"custom typed raise result"); result.Evaluate-=compute; Check(result.Call(1)==-1,"custom return removal");
        var external=new ExternalEvent(); external.ChangedEvent+=first;
        typeof(ExternalEvent).GetEvent("ChangedEvent")!.GetRaiseMethod(true)!.Invoke(external,new object[]{42});
        Check(calls.Count==1 && calls[0]==42,"out-of-line accessors"); calls.Clear(); external.ChangedEvent-=first;
        var reentrant=new EventSource();
        Changed removeSecond=x=>{ calls.Add(x); reentrant.Automatic-=second; };
        reentrant.Automatic+=removeSecond; reentrant.Automatic+=second;
        reentrant.Fire(40); Check(string.Join(",",calls)=="40,41","raise snapshots list during mutation"); calls.Clear();
        reentrant.Fire(42); Check(string.Join(",",calls)=="42","mutation applies to following raise"); calls.Clear();
        GC.Collect(); GC.WaitForPendingFinalizers(); source.Fire(42);
        Check(string.Join(",",calls)=="42,43","GC keeps event subscribers");
        Console.WriteLine("managed events: automatic/custom, C#/CLI subscription, virtual accessors, typed byref/imported raising, empty/null behavior, CLR metadata, receiver once and GC PASS");
    }
}
