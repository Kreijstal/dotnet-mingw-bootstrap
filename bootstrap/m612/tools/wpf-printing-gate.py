#!/usr/bin/env python3
"""Run the original System.Printing project inputs through the source CLI frontend."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET


def win(path):
    return subprocess.check_output(["cygpath", "-m", str(path.resolve())], text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("build", type=Path)
    parser.add_argument("sdk", type=Path)
    parser.add_argument("oob", type=Path)
    parser.add_argument("frontend", type=Path)
    args = parser.parse_args()
    root = args.source / "src/Microsoft.DotNet.Wpf/src"
    original = root / "System.Printing"
    work = args.build / "printing-gate"
    work.mkdir(parents=True, exist_ok=True)
    cpp = work / "CPP"
    shutil.copytree(original / "CPP", cpp, dirs_exist_ok=True)
    # This temporary MSVC code-analysis include has no runtime declarations.
    # MinGW's Windows headers already provide the SAL annotations used here.
    header = cpp / "Win32Inc.hpp"
    text = header.read_text()
    old = "#include <codeanalysis\\sourceannotations.h>  //TEMPORARY INCLUDE"
    if old not in text:
        raise SystemExit("System.Printing code-analysis include changed; review port preparation")
    header.write_text(text.replace(old, "#if !defined(__MINGW32__)\n" + old + "\n#endif"))
    # These two Vista printer flags are missing from the trusted MinGW header.
    # Values: Microsoft's win32metadata RecompiledIdlHeaders/um/winspool.h.
    flags={"PRINTER_ATTRIBUTE_PUSHED_USER":"0x00020000", "PRINTER_ATTRIBUTE_PUSHED_MACHINE":"0x00040000"}
    header.write_text(header.read_text()+"\n"+"".join(
        "#ifndef "+name+"\n#define "+name+" "+value+"\n#endif\n" for name,value in flags.items()))
    # The upstream implementation uses DocumentEvent only as a managed
    # property or a DllImport entry-point string. MinGW's generic API macro
    # otherwise rewrites the property to DocumentEventW; native declarations
    # and the literal P/Invoke entry point remain intact.
    header.write_text(header.read_text()+"\n#ifdef DocumentEvent\n#undef DocumentEvent\n#endif\n")
    sdk_probe=work/"sdk-property-probe.cpp"
    sdk_probe.write_text("#include <windows.h>\n#include <winspool.h>\nPrintPropertyValue value;\nPrintPropertiesCollection collection;\n")
    missing_properties=subprocess.run(["/clang64/bin/clang++.exe","-fsyntax-only",win(sdk_probe)],
                                      stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode!=0
    if missing_properties:
        port=Path(__file__).resolve().parents[1]/"wpf-native/PrintingSdk.h"
        header.write_text(header.read_text()+'\n#include "'+win(port)+'"\n')
    check=Path(__file__).resolve().parents[1]/"wpf-tests/PrintingSdk.cpp"
    sdk_check=work/"sdk-property-check.exe"
    native=["/clang64/bin/clang++.exe","-std=c++17","-O2",win(check),"-I",win(port.parent if missing_properties else check.parent),"-o",win(sdk_check)]
    if missing_properties:
        native += ["-DWPF_MISSING_PRINT_PROPERTIES"]
    subprocess.run(native,check=True)
    subprocess.run([str(sdk_check)],check=True)
    inputs = [item.attrib["Include"].replace("\\", "/")
              for item in ET.parse(original / "System.Printing.vcxproj").iter()
              if item.tag.rsplit("}", 1)[-1].lower() == "clcompile" and "Include" in item.attrib]
    for file in inputs:
        if not (work / file).is_file():
            raise SystemExit("Missing original printing source: " + file)
    combined = work / "System.Printing.cpp"
    combined.write_text('using namespace System::Reflection;\n'
                        '[assembly:AssemblyVersion("8.0.0.0")];\n'
                        '[assembly:AssemblyFileVersion("8.0.0.0")];\n'
                        '[assembly:AssemblyInformationalVersion("8.0.0")];\n' +
                        "".join('#include "' + file + '"\n' for file in inputs))
    refs = sorted((args.sdk / "packs/Microsoft.NETCore.App.Ref").glob("*/ref/net8.0"))[-1]
    command = ["/clang64/bin/python.exe", win(args.frontend / "cli-clang"),
               "--sdk", win(args.sdk), "--clang", win(Path("/clang64/bin/clang++.exe")),
               win(combined), "--std", "c++14", "--target", "library", "--emit", "il",
               "--assembly-name", "System.Printing", "--public-key", win(args.build / "keys/MSShared.snk"),
               "-o", win(work / "System.Printing.il")]
    for include in [cpp, cpp / "inc", cpp / "inc/GDIExporter", root / "Shared/inc"]:
        command += ["-I", win(include)]
    for define in ["UNICODE=1", "_UNICODE=1", "_CLR_NETCORE=1", "DDBANNED_H=1"]:
        command += ["-D", define]
    # The copied SDK headers still use pre-SAL2 annotation spellings.
    # These affect static analysis only; retain all native declarations.
    legacy_sal = {"__callback": "", "__in": "_In_", "__out": "_Out_", "__inout": "_Inout_",
                  "__in_opt": "_In_opt_", "__out_opt": "_Out_opt_", "__inout_opt": "_Inout_opt_",
                  "__success(x)": "_Success_(x)", "__in_bcount(x)": "_In_reads_bytes_(x)",
                  "__in_ecount(x)": "_In_reads_(x)", "__out_bcount(x)": "_Out_writes_bytes_(x)",
                  "__out_ecount(x)": "_Out_writes_(x)", "__inout_bcount(x)": "_Inout_updates_bytes_(x)",
                  "__inout_ecount(x)": "_Inout_updates_(x)", "__in_bcount_opt(x)": "_In_reads_bytes_opt_(x)",
                  "__in_ecount_opt(x)": "_In_reads_opt_(x)", "__out_bcount_opt(x)": "_Out_writes_bytes_opt_(x)",
                  "__out_ecount_opt(x)": "_Out_writes_opt_(x)", "__typefix(type)": ""}
    for name, replacement in legacy_sal.items():
        command += ["-D", name + "=" + replacement]
    # Preserve the upstream #using/as_friend boundaries and source identities.
    for macro, name in [("SYSTEMXAML_DLL", "System.Xaml"), ("WINDOWSBASE_DLL", "WindowsBase"),
                        ("PRESENTATIONCORE_DLL", "PresentationCore"),
                        ("PRESENTATIONFRAMEWORK_DLL", "PresentationFramework"), ("REACHFRAMEWORK_DLL", "ReachFramework")]:
        file = args.build / "lib" / (name + ".dll")
        if not file.is_file():
            raise SystemExit("Build original printing dependency first: " + name)
        command += ["-D", macro + '="' + win(file) + '"', "--reference", win(file)]
    for file in sorted(refs.glob("*.dll")) + sorted(args.oob.glob("*.dll")):
        if file.stem in {"System.Xaml", "WindowsBase", "PresentationCore", "PresentationFramework", "ReachFramework"}:
            continue
        command += ["--reference", win(file)]
    types = ["System.Exception", "System.SystemException", "System.ArgumentException", "System.ArgumentNullException", "System.ArgumentOutOfRangeException",
             "System.InvalidOperationException", "System.NotSupportedException", "System.NotImplementedException",
             "System.ObjectDisposedException", "System.OutOfMemoryException", "System.IDisposable", "System.MarshalByRefObject",
             "System.Boolean", "System.Int32", "System.UInt32", "System.Int64", "System.Double", "System.Enum", "System.Type",
             "System.RuntimeTypeHandle", "System.Array", "System.DateTime", "System.TimeSpan", "System.EventArgs",
             "System.EventHandler", "System.Delegate", "System.MulticastDelegate", "System.Action", "System.IFormatProvider",
             "System.IO.Stream", "System.IO.MemoryStream", "System.IO.FileStream", "System.IO.FileMode", "System.IO.FileAccess",
             "System.IO.SeekOrigin", "System.Threading.Monitor", "System.Threading.Thread", "System.Diagnostics.Debug",
             "System.Collections.IEnumerable", "System.Collections.IEnumerator", "System.Collections.ICollection",
             "System.Collections.IDictionary", "System.Collections.Hashtable", "System.Collections.ArrayList",
             "System.Collections.Specialized.StringCollection", "System.Globalization.CultureInfo",
             "System.Runtime.InteropServices.Marshal", "System.Runtime.InteropServices.GCHandle",
             "System.Runtime.InteropServices.GCHandleType", "System.Runtime.InteropServices.SafeHandle",
             "System.Resources.ResourceManager", "System.Reflection.Assembly", "System.Security.SecurityCriticalAttribute",
             "System.Windows.Threading.DispatcherObject", "System.Windows.Threading.Dispatcher",
             "System.Windows.DependencyObject", "System.Windows.Point", "System.Windows.Size", "System.Windows.Rect",
             "System.Printing.PrintTicket", "System.Printing.PrintCapabilities"]
    types += ["System.Byte", "System.Char", "System.Guid", "System.Single", "System.UInt64",
              "System.Runtime.InteropServices.LayoutKind", "System.Runtime.InteropServices.CharSet",
              "System.Runtime.InteropServices.UnmanagedType", "System.Runtime.InteropServices.CallingConvention",
              "System.Runtime.InteropServices.StructLayoutAttribute", "System.Runtime.InteropServices.FieldOffsetAttribute",
              "System.Runtime.InteropServices.CriticalHandle", "Microsoft.Win32.SafeHandles.SafeHandleZeroOrMinusOneIsInvalid",
              "System.Xml.XmlDocument", "System.Xml.XmlNode", "System.Xml.XmlReader", "System.Xml.XmlWriter",
              "System.Xml.XPath.XPathNavigator", "System.Xml.Serialization.XmlSerializer",
              "System.IO.Packaging.Package", "System.Windows.Documents.FixedDocument",
              "System.Windows.Xps.Serialization.XpsSerializationManager", "System.Windows.Xps.Packaging.XpsDocument",
              "System.Drawing.Printing.PrinterSettings", "MS.Internal.Invariant",
              "MS.Internal.Telemetry.PresentationCore.XpsOMPrintingTraceLogger"]
    types += ["System.Collections.Generic.Dictionary`2", "System.Collections.Generic.List`1",
              "System.Collections.Generic.IEnumerable`1", "System.Collections.Generic.IEnumerator`1",
              "System.Collections.Generic.KeyValuePair`2", "System.Collections.Generic.ICollection`1",
              "System.Collections.Generic.IDictionary`2", "System.Collections.Generic.IList`1", "System.Collections.Generic.Queue`1",
              "System.Windows.Xps.Packaging.PackagingAction", "System.Windows.Xps.Serialization.XpsDocumentEventType",
              "System.Printing.Interop.PrintTicketConverter", "System.Printing.Interop.BaseDevModeType",
              "System.Runtime.ConstrainedExecution.Consistency", "System.Runtime.ConstrainedExecution.Cer",
              "System.Runtime.ConstrainedExecution.ReliabilityContractAttribute",
              "System.Windows.Xps.Serialization.RCW.IXpsDocumentPackageTarget",
              "System.Windows.Xps.Serialization.RCW.IXpsOMPackageWriter",
              "System.Windows.Xps.Serialization.RCW.IPrintDocumentPackageTarget",
              "System.Windows.Xps.Serialization.RCW.PrintDocumentPackageStatusProvider",
              "System.Text.StringBuilder", "System.ComponentModel.Win32Exception",
              "Microsoft.Win32.SafeHandles.SafeFileHandle", "Microsoft.Win32.SafeHandles.SafeWaitHandle",
              "System.Runtime.InteropServices.ComTypes.IStream",
              "System.Attribute", "System.AttributeTargets", "System.AttributeUsageAttribute",
              "System.Runtime.Serialization.SerializationInfo", "System.Runtime.Serialization.StreamingContext",
              "System.Windows.Documents.Serialization.SerializerWriter", "System.Windows.Documents.FixedDocumentSequence",
              "System.Windows.Documents.DocumentPaginator", "System.Windows.Documents.FixedPage",
              "System.Windows.Media.Visual", "System.Windows.Documents.Serialization.SerializerWriterCollator"]
    types += ["System.ComponentModel.AsyncCompletedEventArgs", "System.ComponentModel.ProgressChangedEventArgs"]
    types += ["System.Windows.Documents.Serialization." + name for name in
              ["WritingPrintTicketRequiredEventHandler", "WritingProgressChangedEventHandler",
               "WritingCompletedEventHandler", "WritingCancelledEventHandler",
               "WritingPrintTicketRequiredEventArgs", "WritingProgressChangedEventArgs",
               "WritingCompletedEventArgs", "WritingCancelledEventArgs",
               "WritingProgressChangeLevel"]]
    types += ["System.Windows.Xps.Serialization.PrintTicketLevel"]
    types += ["System.Windows.Xps.Serialization." + name for name in
              ["XpsSerializationPrintTicketRequiredEventArgs", "XpsSerializationCompletedEventArgs",
               "XpsSerializationProgressChangedEventArgs", "XpsWritingProgressChangeLevel",
               "PackageSerializationManager", "XpsSerializationPrintTicketRequiredEventHandler",
               "MXDWSerializationManager"]]
    types += ["System.Windows.Xps.Packaging.IXpsFixedDocumentSequenceReader",
              "System.Windows.Media.Geometry", "System.Windows.Media.Matrix", "System.Windows.Media.Brush",
              "System.Windows.Media.Pen", "System.Windows.Media.GlyphRun", "System.Windows.Media.Imaging.BitmapSource"]
    types += ["System.Printing.ValidationResult", "System.Printing.PrintTicketScope",
              "System.Windows.Controls.PageRangeSelection", "System.Windows.Controls.PageRange",
              "System.Printing.PrintTicketManager",
              "System.Windows.Xps.Serialization.XpsSerializationXpsDriverDocEventArgs"]
    types += ["System.Windows.Controls.PrintDialog", "System.IndexOutOfRangeException", "System.Threading.ThreadPriority"]
    types += ["System.Collections.ObjectModel.Collection`1"]
    types += ["System.Runtime.Serialization.IDeserializationCallback", "System.ICloneable"]
    types += ["System.Xml.XPath.XPathItem", "System.Runtime.Serialization.ISerializable",
              "System.IAsyncResult", "System.AsyncCallback", "System.Threading.WaitHandle",
              "System.Windows.Xps.Packaging.PackagingProgressEventArgs"]
    types += ["System.Threading.AutoResetEvent", "System.Threading.EventWaitHandle", "System.Threading.EventResetMode",
              "System.Collections.IDictionaryEnumerator", "System.Collections.DictionaryEntry"]
    types += ["System.Printing.PrintSystemException", "System.Printing.PrintCommitAttributesException",
              "System.Printing.PrintQueueException", "System.Printing.PrintServerException",
              "System.Printing.PrintJobException", "System.IO.IOException",
              "Microsoft.Win32.Registry", "Microsoft.Win32.RegistryKey", "Microsoft.Win32.RegistryValueKind"]
    types += ["System.SystemException", "System.MarshalByRefObject", "System.Reflection.MemberInfo",
              "System.Reflection.ICustomAttributeProvider"]
    types += ["System.StringComparison", "System.Windows.Xps.Packaging.PackagingProgressEventHandler"]
    types += ["System.Windows.Xps.Serialization." + name for name in
              ["NgcSerializationManager", "NgcSerializationManagerAsync", "XpsSerializationManagerAsync",
               "XpsPackagingPolicy", "BasePackagingPolicy",
               "FontSubsetterCommitPolicies", "XpsSerializationXpsDriverDocEventHandler"]]
    types += ["System.Windows.Xps.Packaging.PackageInterleavingOrder", "System.Windows.Xps.Packaging.XpsOMPackagingPolicy"]
    types += ["System.Windows.Xps.Serialization.XpsOMSerializationManager",
              "System.Windows.Xps.Serialization.XpsOMSerializationManagerAsync",
              "System.Printing.PageImageableArea", "System.Nullable`1"]
    for name in types:
        command += ["--type", name]
    command += ["--source-type", "System.Printing.PrintQueue"]
    # SHLWAPI and KERNEL32 both export DelayLoadFailureHook. Keep SHLWAPI
    # bindings pending explicit duplicate-symbol resolution rather than
    # silently selecting one DLL. Used unavailable functions still fail.
    for name in ["kernel32", "advapi32", "winspool", "gdi32", "user32", "ole32"]:
        command += ["--native-import", win(Path("/clang64/lib/lib" + name + ".a"))]
    tree_count = len(list((original / "CPP").rglob("*.cpp")))
    arguments = work / "arguments.json"
    arguments.write_text(json.dumps(command[2:], ensure_ascii=False), encoding="utf-8")
    invocation = command[:2] + ["--arguments-json", win(arguments)]
    (work / "inputs.json").write_text(json.dumps({
        "project": "System.Printing.vcxproj", "projectInputs": inputs,
        "treeCppCount": tree_count,
        "portPreparation": ["MinGW skips temporary MSVC code-analysis include", "disable MSVC banned-API static analysis",
                            "map legacy copied-SDK annotations to trusted MinGW SAL2 spellings",
                            "remove MinGW DocumentEvent macro collision with original managed property"],
        "legacySalAliases": legacy_sal,
        "missingSdkFlags": flags,
        "missingPrintPropertyTypes": missing_properties,
        "sdkFlagSource": "https://raw.githubusercontent.com/microsoft/win32metadata/main/generation/WinSDK/RecompiledIdlHeaders/um/winspool.h",
        "scope": "combined-source compiler diagnostic gate; success does not prove separate-unit linking or printing runtime",
        "command": command,
        "invocation": invocation,
    }, indent=2) + "\n")
    print(f"System.Printing: {len(inputs)} project compilation inputs; original tree has {tree_count} C++ files", flush=True)
    return subprocess.run(invocation).returncode


if __name__ == "__main__":
    sys.exit(main())
