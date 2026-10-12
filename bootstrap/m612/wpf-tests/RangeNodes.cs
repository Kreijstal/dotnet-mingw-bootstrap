using System;
using System.Linq;
using MS.Internal.Text.TextInterface;
class Check {
    static void Main() {
        uint[] expected={0,0,2,4,4,5,8,9,10,10,uint.MaxValue};
        for(int i=0;i<16;++i) {
            if(!WpfRangeNodes.Verify().SequenceEqual(expected)) throw new Exception("original range merge order or tie handling failed");
        }
        Console.WriteLine("original WPF range nodes, GetNextSmallestPos/SetIsDigit, native links, managed ranges, ties, exhaustion and GC OK");
    }
}
