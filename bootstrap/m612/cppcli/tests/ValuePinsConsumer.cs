using System;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
unsafe class Program {
    static void Main() {
        if(sizeof(PinnedPoint)!=8 || sizeof(PinnedWide)!=32 || Marshal.OffsetOf<PinnedWide>("y")!=(IntPtr)20)
            throw new Exception("actual CLR point/explicit layout");
        using var stop=new CancellationTokenSource();
        int collections=0;
        var collector=Task.Run(()=> { while(!stop.IsCancellationRequested) { GC.Collect(2,GCCollectionMode.Forced,true,true); Interlocked.Increment(ref collections); Thread.Yield(); } });
        try {
        for(int repeat=0;repeat<20;repeat++) {
        var points=new PinnedPoint[3]; var wide=new PinnedWide[3];
        for(int i=0;i<3;i++) { points[i].x=wide[i].x=10+i; points[i].y=wide[i].y=20+i; }
        if(ValuePins.Points(points)!=0 || points[1].x!=33 || points[1].y!=44 || points[0].x!=10 || points[2].y!=22)
            throw new Exception("original-shaped value pin/native POINT mutation and tracked indexing");
        if(ValuePins.Wide(wide)!=0 || wide[1].x!=33 || wide[1].y!=44 || wide[0].x!=10 || wide[2].y!=22)
            throw new Exception("explicit CLR layout pin/index/update used facade C++ stride");
        ValuePins.Interior(wide);
        GC.Collect(2,GCCollectionMode.Forced,true,true);
        if(wide[2].x!=50 || wide[2].y!=60 || wide[1].x!=33 || wide[1].y!=44)
            throw new Exception("tracked managed value interior pointer lost actual location across GC");
        }
        } finally { stop.Cancel(); collector.GetAwaiter().GetResult(); }
        if(collections==0) throw new Exception("compacting GC stress did not execute");
        Console.WriteLine($"managed value pins: real POINT/32-byte explicit layout, native reads/writes, implicit/explicit casts, CLR strides, tracked updates and {collections} concurrent compacting collections PASS");
    }
}
