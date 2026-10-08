// C# 12 feature smoke test: primary constructors, collection expressions,
// alias any type, default lambda parameters, inline arrays are skipped
// (need runtime support).
using System;
using System.Collections.Generic;
using Pair = (int Left, int Right);

sealed class Counter(string name, int start)
{
    private int _value = start;
    public string Name => name;
    public int Next() => ++_value;
}

static class Program12
{
    public static int Main()
    {
        var c = new Counter("c", 41);
        int[] arr = [1, 2, 3];
        List<int> list = [.. arr, 4, 5];
        Pair p = (list.Count, arr.Length);
        var add = (int a, int b = 10) => a + b;
        Console.WriteLine($"C# 12 on Mono: {c.Name}={c.Next()} {string.Join(",", list)} {p.Left}/{p.Right} {add(5)}");
        return c.Next() == 43 && list.Count == 5 && add(5) == 15 ? 0 : 1;
    }
}
