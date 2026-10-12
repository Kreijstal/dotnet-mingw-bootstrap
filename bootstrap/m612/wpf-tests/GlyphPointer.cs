using System;
using System.Reflection;
using System.Runtime.InteropServices;

Array values = (Array)WpfGlyphPointer.Allocate(3);
var type = values.GetType().GetElementType();
if (type.IsPublic || !type.IsExplicitLayout || Marshal.SizeOf(type) != 28)
    throw new Exception("original WPF GlyphMetrics type/layout was changed");
string[] fields = { "LeftSideBearing", "AdvanceWidth", "RightSideBearing", "TopSideBearing", "AdvanceHeight", "BottomSideBearing", "VerticalOriginY" };
for (int i = 0; i < fields.Length; ++i)
    if (Marshal.OffsetOf(type, fields[i]).ToInt32() != i * 4) throw new Exception("original glyph field offset was lost");
var pin = GCHandle.Alloc(values, GCHandleType.Pinned);
try {
    if (WpfGlyphPointer.Fill(pin.AddrOfPinnedObject(), 3) != 42) throw new Exception("native glyph pointer ABI failed");
    GC.Collect(2, GCCollectionMode.Forced, blocking: true, compacting: true);
    for (int i = 0; i < values.Length; ++i) {
        object value = values.GetValue(i);
        long[] expected = { -17 - i, 70000 + i, 19 + i, -23 - i, 80000 + i, -29 - i, 31 + i };
        for (int j = 0; j < fields.Length; ++j)
            if (Convert.ToInt64(type.GetField(fields[j], BindingFlags.Instance | BindingFlags.Public).GetValue(value)) != expected[j])
                throw new Exception("native DirectWrite glyph bytes did not reach the managed array");
    }
} finally { pin.Free(); }
var native = typeof(WpfGlyphPointer).Assembly.ManifestModule.GetMethod("NativeGlyphs");
if (native.GetParameters()[0].ParameterType != type.MakePointerType())
    throw new Exception("native GlyphMetrics pointer was emitted as a box handle");
Console.WriteLine("original WPF GlyphMetrics pointer, native DirectWrite ABI and pinned GC storage OK");
