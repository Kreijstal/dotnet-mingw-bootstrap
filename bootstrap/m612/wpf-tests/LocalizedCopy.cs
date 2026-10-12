using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;

var english = CultureInfo.GetCultureInfo("en-US");
var japanese = CultureInfo.GetCultureInfo("ja-JP");
var sequence = new Counting(new[] {
    new KeyValuePair<CultureInfo, string>(english, "Hello"),
    new KeyValuePair<CultureInfo, string>(japanese, "こんにちは")
});
var bridge = new WpfLocalizedCopy(sequence);
var output = new KeyValuePair<CultureInfo, string>[4];
bridge.CopyTo(output, 1);
if (output[0].Key != null || output[3].Key != null || output[1].Key != english ||
    output[1].Value != "Hello" || output[2].Key != japanese || output[2].Value != "こんにちは" ||
    sequence.Disposes != 1 || sequence.Currents != 2)
    throw new Exception("upstream CopyTo offsets/value copies/cleanup failed");
static void Expect<T>(Action action) where T : Exception {
    try { action(); } catch (T) { return; }
    throw new Exception("missing " + typeof(T).Name);
}
Expect<IndexOutOfRangeException>(() => bridge.CopyTo(output, -1));
Expect<IndexOutOfRangeException>(() => bridge.CopyTo(output, 3));
Expect<NullReferenceException>(() => bridge.CopyTo(null, 0));
if (sequence.Disposes != 4) throw new Exception("upstream CopyTo array-failure cleanup lost");
sequence = new Counting(Array.Empty<KeyValuePair<CultureInfo, string>>());
new WpfLocalizedCopy(sequence).CopyTo(null, -1);
if (sequence.Disposes != 1 || sequence.Currents != 0)
    throw new Exception("upstream empty CopyTo behavior changed");
Console.WriteLine("upstream WPF LocalizedStrings::CopyTo OK");

sealed class Counting : IEnumerable<KeyValuePair<CultureInfo, string>> {
    readonly IEnumerable<KeyValuePair<CultureInfo, string>> data;
    public int Disposes, Currents;
    public Counting(IEnumerable<KeyValuePair<CultureInfo, string>> data) { this.data = data; }
    public IEnumerator<KeyValuePair<CultureInfo, string>> GetEnumerator() => new Cursor(this, data.GetEnumerator());
    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
    sealed class Cursor : IEnumerator<KeyValuePair<CultureInfo, string>> {
        readonly Counting owner;
        readonly IEnumerator<KeyValuePair<CultureInfo, string>> inner;
        public Cursor(Counting owner, IEnumerator<KeyValuePair<CultureInfo, string>> inner) { this.owner = owner; this.inner = inner; }
        public KeyValuePair<CultureInfo, string> Current { get { owner.Currents++; return inner.Current; } }
        object IEnumerator.Current => Current;
        public bool MoveNext() => inner.MoveNext();
        public void Reset() => inner.Reset();
        public void Dispose() { owner.Disposes++; inner.Dispose(); }
    }
}
