using System;
using System.Collections.Generic;
class Program {
    static void Check(bool value, string message) { if(!value) throw new Exception(message); }
    static void Main() {
        var calls=new List<int>();
        Listener a=x=>calls.Add(x+1), b=x=>calls.Add(x+2), c=x=>calls.Add(x+3);
        Listener all=MulticastBridge.Append(null!,a);
        all=MulticastBridge.Append(all,b);
        all=MulticastBridge.Append(all,a);
        var original=all;
        all=MulticastBridge.Remove(all,a);
        MulticastBridge.Invoke(all,10);
        Check(string.Join(",",calls)=="11,12","last matching handler removal/order"); calls.Clear();
        MulticastBridge.Invoke(original,10);
        Check(string.Join(",",calls)=="11,12,11","immutable original invocation list"); calls.Clear();
        Check(MulticastBridge.Append(all,null!)==all,"null addition");
        Check(MulticastBridge.Remove(all,null!)==all,"null removal");
        Check(MulticastBridge.Remove(null!,a)==null,"empty removal");
        Check(MulticastBridge.Remove(all,c)==all,"absent handler removal");
        Check(MulticastBridge.Remove(a,a)==null,"final handler removal");
        Listener pair=a+b, duplicate=pair+pair;
        Check(MulticastBridge.Remove(duplicate,pair)==pair,"multicast subsequence removal");
        var source=new MulticastBridge();
        MulticastBridge.Add(source,a); MulticastBridge.Add(source,b); MulticastBridge.Subtract(source,a);
        Check(source.Handlers==b && MulticastBridge.ReceiverCount==3 && MulticastBridge.HandlerCount==3,"receiver/rhs evaluated once");
        MulticastBridge.AddShared(a); MulticastBridge.AddShared(b); MulticastBridge.RemoveShared(a);
        Check(MulticastBridge.Shared==b,"static field assignment");
        Check(MulticastBridge.Expression(a,b)==a+b,"assignment expression value");
        var array=new Listener[]{a,c};
        Check(MulticastBridge.ArrayAdd(array,0,b)==a+b && array[0]==a+b && array[1]==c,"array addition");
        Check(MulticastBridge.ArrayRemove(array,0,a)==b && array[0]==b,"array removal");
        Change first=(ref int x)=>x+=2, second=(ref int x)=>x*=3;
        int v=4; MulticastBridge.AppendChange(first,second)(ref v); Check(v==18,"byref invocation order");
        int actionCalls=0; Action action=()=>++actionCalls;
        MulticastBridge.AppendAction(action,action)(); Check(actionCalls==2,"imported delegate");
        Listener throwing=x=>throw new InvalidOperationException();
        try { MulticastBridge.Invoke(MulticastBridge.Append(throwing,b),0); throw new Exception("no handler exception"); }
        catch(InvalidOperationException) {}
        Check(calls.Count==0,"throwing handler stops dispatch");
        GC.Collect(); GC.WaitForPendingFinalizers();
        source.Handlers(40); Check(calls[0]==42,"GC retains delegate target");
        Console.WriteLine("delegate multicast: order, duplicates/subsequences, nulls, identity, assignment storage/value, receiver once, byref, imported delegates, exceptions and GC PASS");
    }
}
