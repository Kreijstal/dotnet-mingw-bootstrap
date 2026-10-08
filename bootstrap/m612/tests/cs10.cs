// C# 10 feature smoke test for a bootstrapped Roslyn running on Mono.
global using System;
global using System.Linq;
namespace Bootstrap.Smoke;

public readonly record struct Point(int X, int Y);
public record Person(string Name, Point Home);

public static class Program
{
    const string Lang = "C#";
    const string Banner = $"{Lang} 10 on Mono";               // constant interpolated string
    public static int Main()
    {
        var p = new Person("Ada", new Point(3, 4));
        var moved = p with { Home = p.Home with { X = 10 } };
        var f = (int a, int b) => a * b;                        // lambda natural type
        string where = moved switch { { Home.X: > 5 } => "east", _ => "west" }; // extended property pattern
        var (x, y) = moved.Home;
        int sum = Enumerable.Range(1, 10).Where(i => i % 2 == 0).Sum();
        Console.WriteLine($"{Banner}: {moved} {where} {f(x, y)} {sum}");
        return f(x, y) == 40 && sum == 30 && where == "east" ? 0 : 1;
    }
}

