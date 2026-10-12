using System;
using System.Runtime.InteropServices;
unsafe class Check {
    static void Main() {
        if(sizeof(PointerCell)!=32 || Marshal.SizeOf<PointerCell>()!=32) throw new Exception("missing explicit CLR layout");
        var values=new PointerCell[3];
        for(int i=0;i<3;++i) { values[i].X=11+i; values[i].Y=101+i; }
        fixed(PointerCell* pointer=values) {
            PointerCell* last=ValuePointerArithmetic.Advance(pointer,2);
            if(last!=pointer+2 || ValuePointerArithmetic.Advance(last,-2)!=pointer ||
               ValuePointerArithmetic.Distance(pointer,last)!=2 || ValuePointerArithmetic.Distance(last,pointer)!=-2 ||
               ValuePointerArithmetic.Verify(pointer)!=0 || values[2].Y!=203)
                throw new Exception("native value pointer arithmetic used the surrogate C++ layout or lost its pointer provenance");
        }
        Console.WriteLine("GC-free value pointer arithmetic uses actual explicit CLR stride and preserves native pointer metadata OK");
    }
}
