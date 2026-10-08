// C# 14 feature smoke test: extension members (blocks), the field keyword,
// null-conditional assignment, nameof on unbound generics, simple lambda
// parameters with modifiers.
#nullable enable
using System;
using System.Collections.Generic;

static class Ext
{
    extension(string s)
    {
        public int Vowels { get { int n = 0; foreach (var c in s) if ("aeiou".IndexOf(c) >= 0) n++; return n; } }
        public string Twice() => s + s;
    }
    extension(int)
    {
        public static int Answer => 42;
    }
}

class Node { public Node? Next; public int Value { get; set => field = Math.Max(0, value); } }

delegate bool Parser(string text, out int result);

static class Program14
{
    public static int Main()
    {
        var a = new Node { Value = -5 };
        Node? none = null;
        none?.Value = 3;                       // null-conditional assignment: no-op
        a?.Next = new Node { Value = 7 };
        Parser p = (text, out result) => int.TryParse(text, out result);
        p("12", out int twelve);
        string name = nameof(List<>);
        Console.WriteLine($"C# 14 on Mono: {"education".Vowels} {"ab".Twice()} {int.Answer} {a.Value} {a.Next!.Value} {twelve} {name}");
        return "education".Vowels == 5 && int.Answer == 42 && a.Value == 0 && a.Next.Value == 7 && twelve == 12 && name == "List" ? 0 : 1;
    }
}
