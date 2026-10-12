// SPDX-License-Identifier: MIT
using System;
for (int bits = 0; bits < 4; bits++)
    if (WpfFontSimulations.RoundTrip(bits) != bits) throw new Exception("upstream font simulation conversion failed");
try { WpfFontSimulations.RoundTrip(4); throw new Exception("unknown simulation flags accepted"); }
catch (InvalidOperationException) { }
var type = typeof(WpfFontSimulations).Assembly.GetType("MS.Internal.Text.TextInterface.FontSimulations", throwOnError: true);
if (type.IsPublic || !type.IsEnum || !type.IsDefined(typeof(FlagsAttribute), false) || Enum.ToObject(type, 3).ToString() != "Bold, Oblique")
    throw new Exception("upstream Flags enum identity or formatting lost");
Console.WriteLine("upstream WPF font simulation conversions OK");
