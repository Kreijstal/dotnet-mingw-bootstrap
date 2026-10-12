using System;
using System.Reflection;
using System.Runtime.CompilerServices;

CleanupTrace.Reset();
((IDisposable)new CleanupDerived()).Dispose();
if (CleanupTrace.Read() != 21) throw new Exception("derived/base destructor order was lost");
CleanupTrace.Reset();
CleanupCalls.Drop(new CleanupDerived());
CleanupCalls.Drop(null);
if (CleanupTrace.Read() != 21 || CleanupCalls.Receivers() != 2) throw new Exception("managed delete receiver or null behavior failed");
CleanupTrace.Reset();
CleanupCalls.Explicit(new CleanupDerived());
if (CleanupTrace.Read() != 21) throw new Exception("explicit destructor did not dispatch through IDisposable");
CleanupTrace.Reset();
try { ((IDisposable)new CleanupThrows()).Dispose(); throw new Exception("missing destructor exception"); }
catch (InvalidOperationException) { }
if (CleanupTrace.Read() != 31) throw new Exception("base cleanup did not run after destructor threw");
CleanupTrace.Reset();
var stream = new CleanupStream();
((IDisposable)stream).Dispose();
if (stream.CanRead || CleanupTrace.Read() != 4) throw new Exception("imported dispose-pattern base was not closed");
CleanupTrace.Reset();
((IDisposable)new CleanupUnrelatedDerived()).Dispose();
if (CleanupTrace.Read() != 6) throw new Exception("an unrelated Dispose(bool) method was treated as the disposal pattern");
CleanupTrace.Reset();
((IDisposable)new CleanupSimpleDerived()).Dispose();
if (CleanupTrace.Read() != 87) throw new Exception("parameterless base disposal was not called after the destructor");
var owner = new CleanupOwner();
var snapshot = CleanupOwner.SnapshotValue(42, new string("東京 😀".ToCharArray()));
GC.Collect(2, GCCollectionMode.Forced, blocking: true, compacting: true);
if (snapshot.Number != 42 || snapshot.Text != "東京 😀" || typeof(CleanupOwner.Snapshot).DeclaringType != typeof(CleanupOwner))
    throw new Exception("nested CLR value identity/copy was lost");
if (owner.ReadLeaf() != 17 || owner.Value() != 117) throw new Exception("nested private outer access failed");
var leaf = owner.Open();
((IDisposable)leaf).Dispose();
if (owner.Value() != 217 || !leaf.GetType().IsNestedPrivate || leaf.GetType().DeclaringType != typeof(CleanupOwner))
    throw new Exception("nested CLR identity or destructor was lost");
var boolMethod = typeof(CleanupBase).GetMethod("Dispose", BindingFlags.Instance | BindingFlags.NonPublic, null, new[] { typeof(bool) }, null);
if (!boolMethod.IsFamily || !boolMethod.IsVirtual ||
    typeof(CleanupBase).GetMethod("Finalize", BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.DeclaredOnly) != null)
    throw new Exception("destructor generated incorrect dispose/finalizer metadata");
CleanupTrace.Reset();
boolMethod.Invoke(new CleanupBase(), new object[] { false });
if (CleanupTrace.Read() != 0) throw new Exception("Dispose(false) called the destructor");
var weak = Finalized.MakeDisposed();
for (int i = 0; i < 10; ++i) { GC.Collect(); GC.WaitForPendingFinalizers(); }
if (weak.IsAlive || Finalized.Calls != 0) throw new Exception("disposal did not suppress the inherited finalizer");
Console.WriteLine("managed destructors, base cleanup, nested access and finalizer suppression OK");
class Finalized : CleanupBase {
    public static int Calls;
    ~Finalized() { ++Calls; }
    [MethodImpl(MethodImplOptions.NoInlining)]
    public static WeakReference MakeDisposed() {
        var value = new Finalized();
        ((IDisposable)value).Dispose();
        return new WeakReference(value);
    }
}
