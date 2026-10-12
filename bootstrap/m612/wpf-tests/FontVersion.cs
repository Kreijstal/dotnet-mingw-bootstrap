using System;
using System.Globalization;
CultureInfo.CurrentCulture = new CultureInfo("de-DE");
if (new WpfFontVersion().Initial() != double.MinValue || WpfFontVersion.Parse("Version 1.25") != 1.25 ||
    WpfFontVersion.Parse("Version 1.25e2") != 125 || WpfFontVersion.Parse("Version 1,25") != 0 ||
    WpfFontVersion.Parse("Version invalid") != 0 || WpfFontVersion.Parse("1") != 0)
    throw new Exception("WPF font version initialization, invariant parsing or fallback changed");
Console.WriteLine("upstream WPF font version parsing OK");
