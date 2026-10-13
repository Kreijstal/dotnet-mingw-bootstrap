using System;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;
foreach(var method in typeof(RecordPins).GetMethods(BindingFlags.Public|BindingFlags.Static|BindingFlags.DeclaredOnly))
    if(method.GetMethodBody()!=null) RuntimeHelpers.PrepareMethod(method.MethodHandle);
int collections=0;
using var stop=new CancellationTokenSource();
var collector=Task.Run(()=> {
    while(!stop.IsCancellationRequested) {
        var inside=RecordPins.Stage()==1;
        for(int i=0;i<128;++i) GC.KeepAlive(new byte[4096]);
        GC.Collect(2,GCCollectionMode.Forced,true,true);
        if(inside && RecordPins.Stage()==1) Interlocked.Increment(ref collections);
    }
});
try {
    for(int i=0;i<3;++i) {
        var input=new byte[RecordPins.Size()];
        var output=new byte[input.Length];
        if(RecordPins.Read(input,output)!=42 || BitConverter.ToInt32(input,0)!=23 || BitConverter.ToInt32(output,0)!=42)
            throw new Exception("native font record pins/Unicode/layout/copy");
        var snapshot=new byte[input.Length];
        RecordPins.Copy(output,snapshot);
        if(!snapshot.SequenceEqual(output)) throw new Exception("source CLR memcpy import full record snapshot");
        if(RecordPins.Font(input)!=23) throw new Exception("real CreateFontIndirectExW/GetObjectW/DeleteObject logical font path");
    }
} finally { stop.Cancel(); collector.GetAwaiter().GetResult(); }
if(collections==0) throw new Exception("no compacting GC inside native record call");
Console.WriteLine($"native ENUMLOGFONTEXDV pins: Unicode/layout/writes, source memcpy import, real GDI font creation/readback/deletion, concurrent compacting GC ({collections} collections) PASS");
