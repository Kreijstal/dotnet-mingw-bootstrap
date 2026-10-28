using System;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

internal static class Program
{
    private static int Number { get; set => field = value + 1; }
    public static async Task<int> Main()
    {
        var gate = new Lock();
        lock (gate) Number = 41;
        int sum = Enumerable.Sequence(1, 3, 1).Sum();
        await foreach (int value in AsyncEnumerable.Range(1, 3)) sum += value;
        Console.WriteLine("source10 contracts: " + Number + "/" + sum);
        return Number == 42 && sum == 12 ? 0 : 1;
    }
}
