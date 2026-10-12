using System;
using System.Collections.Generic;
using System.Globalization;
var expected = new Guid("b2d9f3ec-c9fe-4a11-a2ec-d86208f7c0a2");
if (GuidObjectBoxes.GuidValue() != expected || GuidObjectBoxes.WideGuid() != expected ||
    GuidObjectBoxes.GuidBox() is not Guid boxedGuid || boxedGuid != expected || GuidObjectBoxes.SourceLiteral() != "λ value")
    throw new Exception("Guid/String value constructor binding or boxing lost");
try { GuidObjectBoxes.InvalidGuid(); throw new Exception("invalid Guid accepted"); }
catch (FormatException) { }
object first = GuidObjectBoxes.Box(); object second = GuidObjectBoxes.Box();
if (first is not BoxPayload value || value.Value != 17 || ReferenceEquals(first, second))
    throw new Exception("value boxing lost identity/copy semantics");
GuidObjectBoxes.Change();
if (((BoxPayload)first).Value != 17 || ((BoxPayload)GuidObjectBoxes.FromMethod()).Value != 29 || ((BoxPayload)GuidObjectBoxes.FromDirectMethod()).Value != 29)
    throw new Exception("boxed value aliased its source or value instance method failed");
var pairs = new PairEnumerator();
object item = pairs.Current2;
if (item.GetType() != typeof(KeyValuePair<CultureInfo,string>) ||
    item is not KeyValuePair<CultureInfo,string> pair || pair.Key != CultureInfo.InvariantCulture || pair.Value != "pair value" || pairs.Reads() != 1)
    throw new Exception("generic Current-to-Object boxing or getter evaluation count lost");
GC.Collect(); GC.WaitForPendingFinalizers();
if (((KeyValuePair<CultureInfo,string>)item).Value != "pair value")
    throw new Exception("boxed generic reference fields did not survive GC");
Console.WriteLine("CLR Guid construction, value Object boxing and generic property copies OK");
