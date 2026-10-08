namespace Net8Lib;

public static class Stats
{
    // .NET 8 APIs: generic math, FrozenDictionary, Random.Shared.GetItems, TimeProvider
    public static T Sum<T>(ReadOnlySpan<T> xs) where T : System.Numerics.INumber<T>
    {
        T acc = T.Zero;
        foreach (var x in xs) acc += x;
        return acc;
    }
    public static System.Collections.Frozen.FrozenDictionary<string, int> Index(IEnumerable<string> words) =>
        System.Collections.Frozen.FrozenDictionary.ToFrozenDictionary(words.Select((w, i) => (w, i)), p => p.w, p => p.i);
    public static DateTimeOffset Now(TimeProvider tp) => tp.GetUtcNow();
}
