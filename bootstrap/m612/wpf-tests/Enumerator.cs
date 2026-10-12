using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using MS.Internal.Text.TextInterface;

var cultures = new[] { CultureInfo.GetCultureInfo("en-US"), CultureInfo.GetCultureInfo("ja-JP") };
var values = new[] { "café", "東京 😀" };
var source = new LocalizedStrings(cultures, values);
var iterator = source.Open();
var type = iterator.GetType();
if (!type.IsNestedPublic || type.DeclaringType != typeof(LocalizedStrings) ||
    !typeof(IDisposable).IsAssignableFrom(type) ||
    type.GetMethod("Finalize", BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.DeclaredOnly) != null)
    throw new Exception("original WPF enumerator nesting/disposal metadata is incorrect");
try { _ = iterator.Current; throw new Exception("enumeration unexpectedly started"); }
catch (InvalidOperationException) { }
for (int i = 0; i < values.Length; ++i) {
    GC.Collect(2, GCCollectionMode.Forced, blocking: true, compacting: true);
    if (!iterator.MoveNext() || iterator.Current.Key != cultures[i] || iterator.Current.Value != values[i])
        throw new Exception("original WPF enumerator lost its array data");
    var boxed = (KeyValuePair<CultureInfo, string>)((IEnumerator)iterator).Current;
    if (boxed.Key != cultures[i] || boxed.Value != values[i]) throw new Exception("named nongeneric Current override failed");
}
if (iterator.MoveNext() || iterator.MoveNext()) throw new Exception("original WPF enumeration did not stop");
try { _ = iterator.Current; throw new Exception("enumeration unexpectedly continued"); }
catch (InvalidOperationException) { }
((IEnumerator)iterator).Reset();
if (!iterator.MoveNext() || iterator.Current.Value != values[0]) throw new Exception("original WPF Reset failed");
iterator.Dispose();
iterator.Dispose(); // The original empty destructor has no once-only state.
Console.WriteLine("original WPF nested enumerator, getters, boxing and IDisposable cleanup OK");
