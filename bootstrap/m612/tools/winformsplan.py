#!/usr/bin/env python3
"""Evaluate WinForms source items into offline SDK projects (no Arcade/NuGet binaries)."""
import argparse, glob, json, os, subprocess, xml.etree.ElementTree as ET
from pathlib import Path
import csproj_items as ci

p = argparse.ArgumentParser()
p.add_argument('source'); p.add_argument('build'); p.add_argument('sdk'); p.add_argument('oob'); p.add_argument('keys'); p.add_argument('name'); p.add_argument('--bindings')
a = p.parse_args()
source, build, sdk, oob, keys = map(Path, (a.source,a.build,a.sdk,a.oob,a.keys))
name = a.name

def native(path):
    path = str(path.resolve())
    if os.name != 'nt' and Path('/usr/bin/cygpath.exe').exists():
        return subprocess.check_output(['cygpath','-m',path], text=True).strip()
    return path

src = source/'src'/name/'src'
proj = src/(name+'.csproj')
props = {'TargetFramework':'net8.0', 'TargetFrameworkIdentifier':'.NETCoreApp',
         'TargetFrameworkVersion':'v8.0', 'TargetOS':'windows', 'TargetsWindows':'true',
         'Configuration':'Release', 'AssemblyName':name, 'RepoRoot':str(source.resolve())+'/',
         'DefaultLanguageSourceExtension':'.cs', 'NetCurrent':'net8.0', 'NetPrevious':'net7.0',
         'NetMinimum':'net6.0', 'NetFrameworkMinimum':'net462', 'MSBuildProjectName':name}
ev = ci.Evaluator(props, list(props))
if name != 'System.Drawing.Common':
    ev.items['Compile'] = [(str(f),{}) for f in sorted(src.rglob('*.cs')) if not {'obj','bin'}.intersection(f.parts)]
    ev.items['EmbeddedResource'] = [(str(f),{}) for f in sorted(src.rglob('*.resx'))]
ev.load(str(proj), {'Compile','EmbeddedResource'}, True)
if (src/'NativeMethods.txt').exists():
    if not a.bindings or not any(Path(a.bindings).rglob('*.cs')):
        raise SystemExit('WinForms 8 requires source-generated CsWin32 bindings: bootstrap Win32Metadata/ClangSharp and CsWin32 first, then supply --bindings. Binary WinMD/NuGet inputs are not accepted.')
    ev.items.setdefault('Compile',[]).extend((str(f),{}) for f in sorted(Path(a.bindings).rglob('*.cs')))
if name not in ('System.Drawing.Common','System.Windows.Forms.PrivateSourceGenerators','System.Windows.Forms.Analyzers.CSharp'):
    common_sr = source/'src/Common/src/SR.cs'
    if common_sr.exists(): ev.items.setdefault('Compile',[]).append((str(common_sr),{}))

out = build/name; out.mkdir(parents=True,exist_ok=True)
root = ET.Element('Project',Sdk='Microsoft.NET.Sdk')
pg = ET.SubElement(root,'PropertyGroup')
settings = {'TargetFramework':'net8.0','AssemblyName':name,'EnableDefaultItems':'false',
    'AllowUnsafeBlocks':'true','Nullable':'enable','LangVersion':'preview','ImplicitUsings': 'disable' if name=='System.Drawing.Common' else 'enable',
    'ProduceReferenceAssembly':'true','GenerateAssemblyInfo':'true','AssemblyVersion':'8.0.0.0',
    'Version':'8.0.0','FileVersion':'8.0.0.0','EnableNETAnalyzers':'false','EnableTrimAnalyzer':'false',
    'UseSharedCompilation':'false','NuGetAudit':'false','DebugType':'portable','NoWarn':'CA1416;SYSLIB0051;SYSLIB0003;0618;8981',
    'RootNamespace':ev.props.get('RootNamespace',name),'EnableUnsafeBinaryFormatterSerialization':'true','GenerateTargetFrameworkAttribute':'true','SignAssembly':'true','PublicSign':'true',
    'AssemblyOriginatorKeyFile':native(keys/('Open.snk' if name=='System.Drawing.Common' else 'ECMA.snk')),
    'CLSCompliant':'true','DefineConstants':'TRACE;RELEASE;'+ev.props.get('DefineConstants','')}
if 'PrivateSourceGenerators' in name or '.Analyzers' in name:
    settings['DefineConstants']+=';WINFORMS_ANALYZERS'
    settings.update({'SignAssembly':'false','PublicSign':'false','GenerateAssemblyInfo':'true','ProduceReferenceAssembly':'false'})
for k,v in settings.items(): ET.SubElement(pg,k).text=v
ig = ET.SubElement(root,'ItemGroup')
for using,meta in ev.items.get('Using',[]): ET.SubElement(ig,'Using',Include=using)
seen = set()
for f,meta in ev.items.get('Compile',[]):
    f=Path(f)
    if f.resolve() in seen: continue
    if not f.is_file(): raise SystemExit('Missing source item: '+str(f))
    seen.add(f.resolve()); ET.SubElement(ig,'Compile',Include=native(f))
resources=[]
for f,meta in ev.items.get('EmbeddedResource',[]):
    f=Path(f)
    if not f.is_file(): raise SystemExit('Missing resource: '+str(f))
    item=ET.SubElement(ig,'EmbeddedResource',Include=native(f))
    logical=meta.get('LogicalName') or meta.get('ManifestResourceName')
    if not logical:
        logical = name+'.'+str(f.relative_to(src)).replace(os.sep,'.')
        if f.suffix=='.resx': logical = logical[:-5]+'.resources'
    elif f.suffix=='.resx' and not logical.endswith('.resources'): logical += '.resources'
    ET.SubElement(item,'LogicalName').text=logical
    if f.suffix=='.resx':
        ET.SubElement(item,'GenerateResourceUsePreserializedResources').text='true'
    if meta.get('GenerateSource')=='true' or f.name in ('SR.resx','Strings.resx'):
        generated=out/(f.stem+'.g.cs')
        ns=meta.get('ClassName') or meta.get('Namespace','System')+'.SR'
        if (source/'src/Common/src/SR.cs').exists() or name=='System.Drawing.Common':
            resources.append([native(f),native(generated),ns,'--fx',logical[:-10]])
        else:
            ns=meta.get('ClassName') or meta.get('Namespace',name+'.Resources')+'.SR'
            resources.append([native(f),native(generated),ns,'--resource-name',logical[:-10]])
            helpers=out/'SR.Format.g.cs'
            namespace,cls=ns.rsplit('.',1)
            helpers.write_text('namespace '+namespace+' { internal static partial class '+cls+' { internal static string Format(string f, params object?[] args) => string.Format(f, args); } }')
            ET.SubElement(ig,'Compile',Include=native(helpers))
        ET.SubElement(ig,'Compile',Include=native(generated))
refs={}
for f in sorted(oob.glob('*.dll')): refs[f.stem]=f
for f in sorted((build/'lib').glob('*.dll')): refs[f.stem]=f
for dep in (() if '.Analyzers' in name or 'PrivateSourceGenerators' in name else ('Microsoft.Win32.SystemEvents','System.Configuration.ConfigurationManager','System.Drawing.Common','System.Resources.Extensions','System.Windows.Extensions','Accessibility','System.Windows.Forms.Primitives')):
    if dep==name: continue
    if dep in refs:
        item=ET.SubElement(ig,'Reference',Include=dep); ET.SubElement(item,'HintPath').text=native(refs[dep]); ET.SubElement(item,'Private').text='false'
    elif dep in [x[0] for x in ev.items.get('PackageReference',[])] and dep!='System.Runtime.CompilerServices.Unsafe':
        raise SystemExit('Build source dependency first: '+dep)
if 'PrivateSourceGenerators' in name or '.Analyzers' in name:
    for f in (sdk/'sdk/8.0.131/Roslyn/bincore').glob('Microsoft.CodeAnalysis*.dll'):
        item=ET.SubElement(ig,'Reference',Include=f.stem); ET.SubElement(item,'HintPath').text=native(f); ET.SubElement(item,'Private').text='false'
else:
    generator=build/'lib/System.Windows.Forms.PrivateSourceGenerators.dll'
    if name=='System.Windows.Forms' and generator.exists(): ET.SubElement(ig,'Analyzer',Include=native(generator))
ET.indent(root)
ET.ElementTree(root).write(out/(name+'.csproj'),encoding='utf-8',xml_declaration=True)
(out/'resources.json').write_text(json.dumps(resources,indent=2))
(out/'items.json').write_text(json.dumps({'source':native(source),'project':native(proj),'compile':len(seen),'warnings':ev.warnings},indent=2))
print(name+': '+str(len(seen))+' source files, '+str(len(resources))+' resource classes')
