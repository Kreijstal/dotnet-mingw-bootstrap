#!/usr/bin/env python3
"""Stage unchanged WPF conversion bodies and the trusted MinGW enum for a check."""
from pathlib import Path
import re
import sys

source, sdk_header, output = map(Path, sys.argv[1:])
wrapper = source / "src/Microsoft.DotNet.Wpf/src/DirectWriteForwarder/CPP/DWriteWrapper"
native = re.search(r"typedef enum DWRITE_FONT_SIMULATIONS\s*\{[^}]+\}\s*DWRITE_FONT_SIMULATIONS;", sdk_header.read_text())
if native is None:
    raise SystemExit("MinGW DWrite font simulation enum declaration not found")
text = (wrapper / "DWriteTypeConverter.cpp").read_text()
begin = text.index("    FontSimulations DWriteTypeConverter::Convert(DWRITE_FONT_SIMULATIONS fontSimulations)")
end = text.index("    DWRITE_FONT_FACE_TYPE DWriteTypeConverter::Convert(FontFaceType fontFaceType)", begin)
# Preserve these two complete original methods byte for byte, including their
# enum case labels, bitwise expressions, explicit casts and exception paths.
methods = text[begin:end]
output.write_text(f'''{native.group()}
#include "{(wrapper / 'FontSimulation.h').as_posix()}"
namespace MS {{ namespace Internal {{ namespace Text {{ namespace TextInterface {{
private ref class DWriteTypeConverter sealed {{
internal:
    static FontSimulations Convert(DWRITE_FONT_SIMULATIONS);
    static unsigned char Convert(FontSimulations);
}};
{methods}
}}}}}}}}
public ref class WpfFontSimulations abstract sealed {{
public:
    static int RoundTrip(int bits) {{
        return MS::Internal::Text::TextInterface::DWriteTypeConverter::Convert(
            MS::Internal::Text::TextInterface::DWriteTypeConverter::Convert((DWRITE_FONT_SIMULATIONS)bits));
    }}
}};
''')
