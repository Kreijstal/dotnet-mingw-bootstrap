// C# 13 feature smoke test: params collections, \e escape, partial
// properties, implicit index (^) in object initializers, ref struct
// implementing an interface ('allows ref struct' generics need runtime
// support Mono lacks: CS9240).
using System;
using System.Collections.Generic;

partial class Box { public partial int Size { get; set; } }
partial class Box { private int _size; public partial int Size { get => _size; set => _size = value * 2; } }

class Ring { public int[] Slots { get; } = new int[4]; }

interface IMeasure { int Measure(); }
ref struct Probe : IMeasure { public int V; public int Measure() => V * 3; }

static class Program13
{
    static int Sum(params IEnumerable<int> xs) { int s = 0; foreach (var x in xs) s += x; return s; }
    public static int Main()
    {
        var b = new Box { Size = 21 };
        var r = new Ring { Slots = { [^1] = 7 } };
        var p = new Probe { V = 5 }; int m = p.Measure();
        string esc = "\e[0m";
        Console.WriteLine($"C# 13 on Mono: {Sum(1, 2, 3)} {b.Size} {r.Slots[3]} {m} {(int)esc[0]}");
        return Sum(1, 2, 3) == 6 && b.Size == 42 && r.Slots[3] == 7 && m == 15 && esc[0] == 27 ? 0 : 1;
    }
}
