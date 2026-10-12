using System;
using System.Collections.Generic;
using EachFixtures;
static void Expect<T>(Action action) where T : Exception {
    try { action(); } catch (T) { return; }
    throw new Exception("missing " + typeof(T).Name);
}
var source = new ProbeSequence();
if (EachBridge.Once(source) != 6 || EachBridge.Receivers() != 1 || source.Gets != 1 ||
    source.Moves != 4 || source.Currents != 3 || source.Disposes != 1)
    throw new Exception("foreach receiver/element evaluation counts lost");
int[] results = {6, 1, 4, 1};
for (int mode = 0; mode < results.Length; ++mode) {
    source = new ProbeSequence();
    if (EachBridge.Sum(source, mode) != results[mode] || source.Disposes != 1)
        throw new Exception("normal/break/continue/return cleanup failed");
}
source = new ProbeSequence();
Expect<InvalidOperationException>(() => EachBridge.Sum(source, 4));
if (source.Disposes != 1) throw new Exception("body exception cleanup failed");
for (int mode = 1; mode <= 4; ++mode) {
    source = new ProbeSequence { Mode = mode };
    Expect<InvalidOperationException>(() => EachBridge.Sum(source, 0));
    if (source.Disposes != (mode == 1 ? 0 : 1)) throw new Exception("enumerator exception cleanup failed");
}
source = new ProbeSequence { Mode = 5 };
Expect<NullReferenceException>(() => EachBridge.Sum(source, 0));
if (source.Disposes != 0) throw new Exception("null enumerator cleanup failed");
Expect<NullReferenceException>(() => EachBridge.Sum(null, 0));
source = new ProbeSequence();
if (EachBridge.Nested(source) != 18 || source.Gets != 4 || source.Disposes != 4)
    throw new Exception("nested enumerator cleanup failed");
source = new ProbeSequence();
if (new EachProperty(source).First != 1 || source.Disposes != 1)
    throw new Exception("foreach in property accessor failed");
if (EachBridge.Untyped(new PlainSequence()) != 6 || EachBridge.Last(new[] {"α", "東京"}) != "東京")
    throw new Exception("nongeneric unboxing/reference iteration failed");
var explicitSource = new ExplicitSequence();
if (EachBridge.Explicit(explicitSource) != 6 || explicitSource.Inner.Gets != 1 || explicitSource.Inner.Disposes != 1)
    throw new Exception("explicit IEnumerable implementation lookup/cleanup failed");
Expect<InvalidCastException>(() => EachBridge.Last(new object[] {42}));
if (EachBridge.Characters("α東京") != 'α' + '東' + '京' || EachBridge.Characters("😀") != 0xd83d + 0xde00 || EachBridge.Characters("") != 0)
    throw new Exception("UTF-16 character enumerator iteration failed");
Expect<NullReferenceException>(() => EachBridge.Characters(null));
if (EachBridge.ArraySum(new[] {1, 2, 3}) != 4 || EachBridge.ArraySum(Array.Empty<int>()) != 0)
    throw new Exception("array iteration or numeric conversion failed");
Expect<NullReferenceException>(() => EachBridge.ArraySum(null));
if (EachBridge.Flags(new long[] {0, 1, 0x100000000L, -2}) != 3 || EachBridge.Unsigned(new[] {ulong.MaxValue}) < 1.8e19)
    throw new Exception("foreach boolean width or unsigned floating conversion failed");
var pairs = new Dictionary<string,int> { ["one"] = 1, ["two"] = 2 };
var copied = new KeyValuePair<string,int>[2];
EachBridge.CopyPairs(pairs, copied);
if (EachBridge.PairSum(pairs) != 3 || copied[0].Key != "one" || copied[1].Value != 2)
    throw new Exception("WPF-shaped value-element iteration/copy failed");
Console.WriteLine("CLR for each enumeration and cleanup OK");
