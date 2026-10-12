// SPDX-License-Identifier: MIT
using System;
using System.Reflection;
using System.Runtime.InteropServices;
var assembly = typeof(WpfMetricsIdentity).Assembly;
var font = assembly.GetType("MS.Internal.Text.TextInterface.FontMetrics", throwOnError: true);
var glyph = assembly.GetType("MS.Internal.Text.TextInterface.GlyphMetrics", throwOnError: true);
if (font.IsPublic || glyph.IsPublic || font.IsValueType || !glyph.IsValueType || !font.IsExplicitLayout || !glyph.IsExplicitLayout)
    throw new Exception("upstream metrics identity or layout lost");
string[] fontNames = { "DesignUnitsPerEm", "Ascent", "Descent", "LineGap", "CapHeight", "XHeight", "UnderlinePosition", "UnderlineThickness", "StrikethroughPosition", "StrikethroughThickness" };
int[] fontOffsets = { 0, 2, 4, 8, 10, 12, 14, 16, 18, 20 };
for (int i = 0; i < fontNames.Length; i++)
    if (Marshal.OffsetOf(font, fontNames[i]).ToInt32() != fontOffsets[i]) throw new Exception("upstream font field offset lost");
var metrics = Activator.CreateInstance(font);
font.GetField("DesignUnitsPerEm").SetValue(metrics, (ushort)1000);
font.GetField("Ascent").SetValue(metrics, (ushort)800);
font.GetField("Descent").SetValue(metrics, (ushort)200);
font.GetField("LineGap").SetValue(metrics, (short)100);
if ((double)font.GetProperty("Baseline").GetValue(metrics) != 0.85 || (double)font.GetProperty("LineSpacing").GetValue(metrics) != 1.1)
    throw new Exception("upstream font metric calculation failed");
string[] glyphNames = { "LeftSideBearing", "AdvanceWidth", "RightSideBearing", "TopSideBearing", "AdvanceHeight", "BottomSideBearing", "VerticalOriginY" };
object[] values = { -3, 1200u, -5, 17, 2048u, -9, 1550 };
if (Marshal.SizeOf(glyph) != Native.WpfGlyphMetricsOffset(0)) throw new Exception("DirectWrite glyph size mismatch");
var glyphMetrics = Activator.CreateInstance(glyph);
for (int i = 0; i < glyphNames.Length; i++) {
    if (Marshal.OffsetOf(glyph, glyphNames[i]).ToInt32() != Native.WpfGlyphMetricsOffset(i+1)) throw new Exception("DirectWrite glyph field offset mismatch");
    glyph.GetField(glyphNames[i]).SetValue(glyphMetrics, values[i]);
}
var buffer = Marshal.AllocHGlobal(Marshal.SizeOf(glyph));
try {
    Marshal.StructureToPtr(glyphMetrics, buffer, false);
    if (Native.WpfGlyphMetricsRead(buffer) != 1) throw new Exception("native DirectWrite glyph layout read failed");
} finally { Marshal.FreeHGlobal(buffer); }
Console.WriteLine("upstream WPF font/glyph layout OK: native glyph size 28");
static class Native {
    [DllImport("wpf-metrics-native.dll", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int WpfGlyphMetricsOffset(int index);
    [DllImport("wpf-metrics-native.dll", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int WpfGlyphMetricsRead(IntPtr value);
}
