using System;
using System.Collections;
using System.Collections.Generic;
namespace EachFixtures {
public class ProbeSequence : IEnumerable<int> {
    public int Mode, Gets, Moves, Currents, Disposes;
    public IEnumerator<int> GetEnumerator() {
        Gets++;
        if (Mode == 1) throw new InvalidOperationException("GetEnumerator");
        if (Mode == 5) return null;
        return new Cursor(this);
    }
    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
    sealed class Cursor : IEnumerator<int> {
        readonly ProbeSequence owner;
        int index;
        public Cursor(ProbeSequence owner) { this.owner = owner; }
        public bool MoveNext() {
            owner.Moves++;
            if (owner.Mode == 2) throw new InvalidOperationException("MoveNext");
            return ++index <= 3;
        }
        public int Current {
            get {
                owner.Currents++;
                if (owner.Mode == 3) throw new InvalidOperationException("Current");
                return index;
            }
        }
        object IEnumerator.Current => Current;
        public void Reset() { index = 0; }
        public void Dispose() {
            owner.Disposes++;
            if (owner.Mode == 4) throw new InvalidOperationException("Dispose");
        }
    }
}
public class PlainSequence : IEnumerable {
    public IEnumerator GetEnumerator() => new Cursor();
    sealed class Cursor : IEnumerator {
        int index;
        public bool MoveNext() => ++index <= 3;
        public object Current => index;
        public void Reset() { index = 0; }
    }
}
public class ExplicitSequence : IEnumerable<int> {
    public ProbeSequence Inner = new ProbeSequence();
    IEnumerator<int> IEnumerable<int>.GetEnumerator() => Inner.GetEnumerator();
    IEnumerator IEnumerable.GetEnumerator() => Inner.GetEnumerator();
}
}
