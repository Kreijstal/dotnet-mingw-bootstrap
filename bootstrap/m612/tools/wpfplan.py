#!/usr/bin/env python3
"""Evaluate WPF source manifests into offline projects for the bootstrapped SDK."""
import argparse
import json
import os
import shutil
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

import csproj_items as ci

native_roots = []

def native(path):
    path = str(Path(path).resolve())
    if os.name != "nt" and Path("/usr/bin/cygpath.exe").exists():
        for posix, windows in native_roots:
            if path == posix or path.startswith(posix + "/"):
                return windows + path[len(posix):]
        return subprocess.check_output(["cygpath", "-m", path], text=True).strip()
    return path


def main():
    parser = argparse.ArgumentParser()
    for argument in ("source", "build", "sdk", "oob", "keys", "name"):
        parser.add_argument(argument)
    parser.add_argument("--project", type=Path)
    parser.add_argument("--reference-only", action="store_true")
    parser.add_argument("--dependency-map", type=Path)
    args = parser.parse_args()
    source, build, sdk, oob, keys = (Path(getattr(args, name)) for name in
                                   ("source", "build", "sdk", "oob", "keys"))
    if os.name != "nt" and Path("/usr/bin/cygpath.exe").exists():
        for path in (source, build, sdk, oob, keys):
            posix = str(path.resolve())
            windows = subprocess.check_output(["cygpath", "-m", posix], text=True).strip()
            native_roots.append((posix, windows))
    name = args.name
    root_source = source / "src/Microsoft.DotNet.Wpf/src"
    folder = root_source / ("UIAutomation" if name.startswith("UIAutomation") else "") / name
    project = args.project or folder / (name + ".csproj")
    folder = project.parent
    props = {
        "TargetFramework": "net8.0", "TargetFrameworkIdentifier": ".NETCoreApp",
        "TargetFrameworkVersion": "v8.0", "TargetOS": "windows", "TargetsWindows": "true",
        "Configuration": "Release", "AssemblyName": name, "MSBuildProjectName": name,
        "DefaultLanguageSourceExtension": ".cs", "WpfSourceDir": str(root_source.resolve()) + "/",
        "WpfSharedDir": str((root_source / "Shared").resolve()) + "/",
        "WpfCommonDir": str((root_source / "Common").resolve()) + "/",
        "WpfCycleBreakersDir": str((source / "src/Microsoft.DotNet.Wpf/cycle-breakers").resolve()) + "/",
        "SystemIOPackagingPackage": "System.IO.Packaging", "SystemDirectoryServicesPackage":"System.DirectoryServices", "DefineConstants": "",
    }
    ev = ci.Evaluator(props, [key for key in props if key != "DefineConstants" and not (args.project and key == "AssemblyName")])
    ev.items["Compile"] = [(str(path), {}) for path in sorted(folder.rglob("*.cs"))
                           if not {"obj", "bin", "ref"}.intersection(path.parts)]
    ev.items["EmbeddedResource"] = [(str(path), {}) for path in sorted(folder.rglob("*.resx"))]
    # The default globs are enabled only for projects which rely on SDK default items.
    original = ET.parse(project)
    defaults = {node.tag.rsplit("}", 1)[-1]: node.text for node in original.iter()
                if node.tag.rsplit("}", 1)[-1] in
                ("EnableDefaultItems", "EnableDefaultCompileItems", "EnableDefaultEmbeddedResourceItems")}
    if defaults.get("EnableDefaultItems") == "false" or defaults.get("EnableDefaultCompileItems") == "false":
        ev.items["Compile"] = []
    if defaults.get("EnableDefaultItems") == "false" or defaults.get("EnableDefaultEmbeddedResourceItems") == "false":
        ev.items["EmbeddedResource"] = []
    ev.load(str(project), {"Compile", "EmbeddedResource", "PackageReference", "ProjectReference", "Page", "ApplicationDefinition", "Resource", "Content"}, True)
    assembly_name = ev.props.get("AssemblyName", name)
    out = build / name
    out.mkdir(parents=True, exist_ok=True)
    generated = []
    if name == "WindowsBase":
        table = out / "PackageXmlStringTable.cs"
        subprocess.run([
            "perl", str(source / "eng/WpfArcadeSdk/tools/GenXmlStringTable.pl"),
            "-o", str(table), "-n", str(folder / "MS/Internal/IO/Packaging/PackageXmlNamespaces.txt"),
            "-x", str(folder / "MS/Internal/IO/Packaging/PackageXmlStrings.txt"),
            "-e", "MS.Internal.IO.Packaging.PackageXmlEnum", "-c", "MS.Internal.IO.Packaging.PackageXmlStringTable",
        ], check=True)
        generated.append(table)

    namespace = {"WindowsBase": "MS.Internal.WindowsBase", "PresentationCore": "MS.Internal.PresentationCore",
                 "PresentationFramework": "System.Windows", "UIAutomationProvider": "MS.Internal.Automation",
                 "ReachFramework": "System.Windows.Xps", "PresentationBuildTasks": "MS.Utility",
                 "PresentationUI": "System.Windows.TrustUI",
                 "System.Windows.Controls.Ribbon": "Microsoft.Windows.Controls",
                 "WindowsFormsIntegration": "System.Windows"}.get(name, "System")
    resx = folder / "Resources/Strings.resx"
    if resx.exists():
        logical = name + ".Resources.Strings"
        entries = ET.parse(resx).getroot().findall("data")
        sr = out / "SR.g.cs"
        lines = ["// Generated from upstream Resources/Strings.resx.", "#nullable disable",
                 f"namespace {namespace} {{ internal static partial class SR {{",
                 f'internal static global::System.Resources.ResourceManager ResourceManager {{ get; }} = new global::System.Resources.ResourceManager("{logical}", typeof(SR).Assembly);']
        for entry in entries:
            if entry.get("type") or entry.get("mimetype"):
                continue
            key = entry.get("name")
            lines.append(f"internal static string @{key} => GetResourceString({json.dumps(key)});")
        lines.append("} }")
        sr.write_text("\n".join(lines) + "\n")
        generated.append(sr)

    markup = bool(ev.items.get("Page") or ev.items.get("ApplicationDefinition")) and not args.reference_only
    root = ET.Element("Project", **({} if markup else {"Sdk":"Microsoft.NET.Sdk"}))
    if markup:
        ET.SubElement(root,"Import",Project="Sdk.props",Sdk="Microsoft.NET.Sdk")
    pg = ET.SubElement(root, "PropertyGroup")
    shipping = ET.parse(source / "eng/WpfArcadeSdk/tools/ShippingProjects.props")
    shared_keys = {item.strip().casefold() for node in shipping.iter()
                   if node.tag.rsplit("}",1)[-1] == "UseMicrosoftSharedKeyId"
                   for item in (node.text or "").split(";") if item.strip()}
    key = "MSShared" if assembly_name.casefold() in shared_keys else "ECMA"
    settings = {
        "TargetFramework": "net8.0", "AssemblyName": assembly_name, "RootNamespace": assembly_name,
        "EnableDefaultItems": "false", "GenerateAssemblyInfo": "true", "ProduceReferenceAssembly": "true",
        "AssemblyVersion": "8.0.0.0", "FileVersion": "8.0.0.0", "Version": "8.0.0",
        "AllowUnsafeBlocks": "true", "Nullable": "disable", "LangVersion": "preview",
        "EnableNETAnalyzers": "false", "EnableTrimAnalyzer": "false", "UseSharedCompilation": "false",
        "NuGetAudit": "false", "SignAssembly": "true", "PublicSign": "true",
        "AssemblyOriginatorKeyFile": native(keys / (key + ".snk")),
        "NoWarn": "0618;0436;1058;1705;3001;3002;3003;3009;3024;SYSLIB0003;SYSLIB0011;SYSLIB0051",
        "CLSCompliant": "true", "EnableUnsafeBinaryFormatterSerialization": "true",
        "DefineConstants": "TRACE;RELEASE;" + ev.props.get("DefineConstants", ""),
    }
    if args.reference_only:
        settings.update({"ProduceReferenceAssembly": "false", "ProduceOnlyReferenceAssembly": "true"})
    for key, value in settings.items():
        ET.SubElement(pg, key).text = value
    if markup:
        taskdir=build/'markup-tasks'
        taskdir.mkdir(parents=True,exist_ok=True)
        for input in (build/'lib/PresentationBuildTasks.dll',oob/'System.CodeDom.dll',oob/'System.Reflection.MetadataLoadContext.dll'):
            if not input.is_file(): raise SystemExit("Build markup task dependency first: "+str(input))
            shutil.copyfile(input,taskdir/input.name)
        for key,value in {"_PresentationBuildTasksAssembly":native(taskdir/'PresentationBuildTasks.dll'),
                          "AlwaysCompileMarkupFilesInSeparateDomain":"false", "NoInternalTypeHelper":ev.props.get("NoInternalTypeHelper","false"),
                          "IncludePackageReferencesDuringMarkupCompilation":"false"}.items():
            ET.SubElement(pg,key).text=value
    items = ET.SubElement(root, "ItemGroup")
    seen = set()
    for file, _ in ev.items.get("Compile", []) + [(str(path), {}) for path in generated]:
        file = Path(file.strip())
        if file.resolve() in seen:
            continue
        if not file.is_file():
            raise SystemExit("Missing WPF source: " + str(file))
        seen.add(file.resolve())
        ET.SubElement(items, "Compile", Include=native(file))
    seen_resources = set()
    for file, metadata in ev.items.get("EmbeddedResource", []):
        file = Path(file.strip())
        if file.resolve() in seen_resources:
            continue
        seen_resources.add(file.resolve())
        if not file.is_file():
            raise SystemExit("Missing WPF resource: " + str(file))
        node = ET.SubElement(items, "EmbeddedResource", Include=native(file))
        logical = metadata.get("ManifestResourceName") or metadata.get("LogicalName")
        if not logical:
            logical = name + "." + str(file.relative_to(folder)).replace(os.sep, ".")
        if file.suffix == ".resx":
            logical = logical.removesuffix(".resx").removesuffix(".resources") + ".resources"
        ET.SubElement(node, "LogicalName").text = logical
        for key in ("ManifestResourceName","StronglyTypedClassName","StronglyTypedNameSpace","StronglyTypedLanguage"):
            if metadata.get(key): ET.SubElement(node,key).text=metadata[key]
        if metadata.get("StronglyTypedFileName"):
            ET.SubElement(node,"StronglyTypedFileName").text=native(out/(metadata.get("StronglyTypedNameSpace","Resources")+'.'+metadata.get("StronglyTypedClassName","Resources")+'.g.cs'))
    for kind in ("Page","ApplicationDefinition","Resource","Content"):
        for file,metadata in ev.items.get(kind,[]):
            file=Path(file.strip())
            if not file.is_file(): raise SystemExit("Missing WPF markup/resource input: "+str(file))
            node=ET.SubElement(items,kind,Include=native(file))
            # Absolute source paths live outside the generated project directory.
            # Link retains their original relative resource/BAML URI.
            link=metadata.get('Link') or str(file.relative_to(folder)).replace(os.sep,'/')
            ET.SubElement(node,'Link').text=link
            for key in ('Generator','LogicalName','CopyToOutputDirectory'):
                if metadata.get(key): ET.SubElement(node,key).text=metadata[key]
    refs = {path.stem: path for path in oob.glob("*.dll")}
    if name == "PresentationBuildTasks":
        # The installed SDK's MSBuild assemblies are themselves source-built.
        for dependency in ("Microsoft.Build.Framework", "Microsoft.Build.Utilities.Core"):
            path = sdk / "sdk/8.0.131" / (dependency + ".dll")
            if not path.is_file():
                raise SystemExit("Missing source-built SDK dependency: " + str(path))
            refs[dependency] = path
    refs.update({path.stem: path for path in
                 (sdk / "shared/Microsoft.WindowsDesktop.App/8.0.0-bootstrap.7.0.20").glob("*.dll")})
    dependencies = [(Path(dependency), metadata) for dependency, metadata in ev.items.get("ProjectReference", [])
                    if metadata.get("ReferenceOutputAssembly") != "false"]
    mappings = json.loads(args.dependency_map.read_text()) if args.dependency_map else {}
    def identity(dependency):
        root = ET.parse(dependency)
        return next((node.text for node in root.iter() if node.tag.rsplit('}',1)[-1] == 'AssemblyName' and node.text and '$(' not in node.text), dependency.stem)
    project_dependencies = {identity(dependency) for dependency, _ in dependencies}
    native_libraries = {"PresentationNative_cor3", "wpfgfx_cor3", "PenImc_cor3"}
    refs.update({path.stem: path for path in (build / "lib").glob("*.dll")
                 if path.stem not in native_libraries and path.stem in project_dependencies})
    for dependency, _ in dependencies:
        key = str(dependency.resolve())
        if key in mappings:
            artifact = Path(mappings[key])
            if not artifact.is_file(): raise SystemExit("Missing WPF dependency artifact: " + str(artifact))
            refs[identity(dependency)] = artifact
    for dependency, path in sorted(refs.items()):
        if dependency == assembly_name:
            continue
        node = ET.SubElement(items, "Reference", Include=dependency)
        ET.SubElement(node, "HintPath").text = native(path)
        ET.SubElement(node, "Private").text = "false"
    for dependency, metadata in dependencies:
        dependency_name = identity(dependency)
        if dependency_name not in refs:
            raise SystemExit("Build WPF dependency first: " + dependency.stem)
    for dependency, _ in ev.items.get("PackageReference", []):
        dependency = Path(dependency).name
        if dependency and dependency not in refs:
            raise SystemExit("Build source dependency first: " + dependency)
    if markup:
        ET.SubElement(root,'Import',Project='Sdk.targets',Sdk='Microsoft.NET.Sdk')
        ET.SubElement(root,'Import',Project=native(root_source/'PresentationBuildTasks/Microsoft.WinFX.targets'))
        ET.SubElement(root,'Import',Project=native(source/'eng/WpfArcadeSdk/tools/NoInternalTypeHelper.targets'))
        if name=='PresentationUI':
            # Preserve upstream's resource-designer-before-markup dependency.
            footer=ET.SubElement(root,'PropertyGroup')
            ET.SubElement(footer,'PrepareResourcesDependsOn').text='PrepareResourceNames;ResGen;$(PrepareResourcesDependsOn)'
    ET.indent(root)
    ET.ElementTree(root).write(out / (name + ".csproj"), encoding="utf-8", xml_declaration=True)
    (out / "source-items.json").write_text(json.dumps({"project": str(project), "assembly": assembly_name, "reference_only": args.reference_only,
                                                    "dependencies": [str(path.resolve()) for path, _ in dependencies], "compile": sorted(map(str, seen)),
                                                    "resources": sorted(map(str, seen_resources)),
                                                    "pages":[file for file,_ in ev.items.get('Page',[])], "warnings": ev.warnings}, indent=2))
    print(f"{name}: {len(seen)} source files, {len(seen_resources)} resources")


if __name__ == "__main__":
    main()
