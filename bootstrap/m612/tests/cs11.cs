// C# 11 feature smoke test: raw strings, list patterns, required members,
// generic attributes, file-local types, newlines in interpolations.
using System;
using System.Diagnostics.CodeAnalysis;

file sealed class Hidden { public static string Tag => "file-local"; }

[AttributeUsage(AttributeTargets.Class)]
sealed class TypeTagAttribute<T> : Attribute { public Type Type => typeof(T); }

[TypeTag<int>]
sealed class Config
{
    public required string Name { get; init; }
    public int Level { get; init; }
}

static class Program11
{
    static string Classify(int[] xs) => xs switch
    {
        [] => "empty",
        [var only] => $"one:{only}",
        [1, .. var mid, 9] => $"1..{mid.Length}..9",
        _ => "other",
    };
    public static int Main()
    {
        var c = new Config { Name = "boot", Level = 11 };
        string raw = """
            {"lang": "C#", "quote": "\no escapes\"}
            """;
        string s = $"{Classify(new[] { 1, 2, 3, 9 })} {Classify(Array.Empty<int>())} {Hidden.Tag} {
            c.Name + c.Level}";
        var tag = (TypeTagAttribute<int>)Attribute.GetCustomAttribute(typeof(Config), typeof(TypeTagAttribute<int>))!;
        Console.WriteLine($"C# 11 on Mono: {s} {tag.Type.Name} {raw.Length}");
        return s == "1..2..9 empty file-local boot11" && tag.Type == typeof(int) ? 0 : 1;
    }
}
