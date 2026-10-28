using System;

internal sealed class Counter
{
    public int Value { get; set => field = value; }
}

internal static class Program
{
    public static int Main()
    {
        var counter = new Counter { Value = 14 };
        if (counter.Value != 14) return 1;
        Console.WriteLine("PASS C#14 field-backed property compiled by source Roslyn5 and executed on source.NET8");
        return 0;
    }
}
