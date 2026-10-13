#!/usr/bin/env python3
"""Stage source-built WPF, approved interim WinForms and WindowsDesktop SDK inputs."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import shutil
import subprocess
import xml.etree.ElementTree as E


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('build','oob','source','sdk','dest','metadata'): parser.add_argument(name,type=Path)
    args=parser.parse_args()
    b,o,s,sdk,d=args.build,args.oob,args.source,args.sdk,args.dest
    version='8.0.0-bootstrap.wpf.8.0.0'
    sdkver='8.0.131'
    framework=d/'shared/Microsoft.WindowsDesktop.App'/version
    ref=d/'packs/Microsoft.WindowsDesktop.App.Ref'/version
    runtime=d/'packs/Microsoft.WindowsDesktop.App.Runtime.win-x64'/version
    wpf=('WindowsBase','System.Xaml','PresentationCore','PresentationFramework','ReachFramework','DirectWriteForwarder',
         'System.Printing','PresentationUI','UIAutomationTypes','UIAutomationProvider','UIAutomationClient',
         'UIAutomationClientSideProviders','System.Windows.Input.Manipulations','System.Windows.Presentation',
         'System.Windows.Controls.Ribbon','WindowsFormsIntegration',
         *('PresentationFramework.'+name for name in ('Aero','Aero2','AeroLite','Classic','Luna','Royale')),
         *('PresentationFramework-'+name for name in ('SystemCore','SystemData','SystemDrawing','SystemXml','SystemXmlLinq')))
    forms=('Accessibility','System.Drawing.Common','System.Windows.Forms','System.Windows.Forms.Primitives')
    shared=('Microsoft.Win32.SystemEvents','System.Configuration.ConfigurationManager','System.Diagnostics.EventLog',
            'System.Security.Cryptography.ProtectedData','System.Resources.Extensions','System.Windows.Extensions',
            'System.IO.Packaging','System.Security.Permissions','System.DirectoryServices',
            'System.Security.Cryptography.Pkcs','System.Security.Cryptography.Xml')
    native=('PresentationNative_cor3.dll','wpfgfx_cor3.dll','PenImc_cor3.dll','wpf-dwrite-loader.dll','wpf-subsetter.dll')
    # Include transitive C/C++ runtime dependencies from the trusted MSYS2 seed.
    # Windows-provided libraries remain OS dependencies. Record package owners.
    native_files={name:b/'lib'/name for name in native}
    seed_bin=Path('/clang64/bin')
    candidates={p.name.casefold():p for p in seed_bin.glob('*.dll')}
    pending=list(native_files.values()); inspected=set(); seed_dependencies=[]
    while pending:
        file=pending.pop()
        if file.name.casefold() in inspected: continue
        inspected.add(file.name.casefold())
        imports=subprocess.check_output(['objdump','-p',str(file)],text=True)
        for name in re.findall(r'DLL Name:\s*(\S+)',imports):
            dependency=candidates.get(name.casefold())
            if dependency is None or dependency.name.casefold() in {n.casefold() for n in native_files}: continue
            owner=subprocess.check_output(['pacman','-Qoq',str(dependency)],text=True).strip()
            if not owner.startswith('mingw-w64-clang-x86_64-'): raise RuntimeError('unexpected native seed owner: '+owner)
            native_files[dependency.name]=dependency; pending.append(dependency)
            seed_dependencies.append({'file':dependency.name,'package':owner,'sha256':hashlib.sha256(dependency.read_bytes()).hexdigest()})
    identities={entry['Name']:entry for entry in json.loads(args.metadata.read_text())['assemblies']}
    files={p.stem:p for p in o.glob('*.dll')}
    formsroot=sdk/'shared/Microsoft.WindowsDesktop.App/8.0.0-bootstrap.7.0.20'
    files.update({p.stem:p for p in formsroot.glob('*.dll')})
    files.update({p.stem:p for p in (b/'lib').glob('*.dll')})
    for path in (framework,ref/'ref/net8.0',ref/'data',runtime/'runtimes/win-x64/lib/net8.0',
                 runtime/'runtimes/win-x64/native',runtime/'data'): path.mkdir(parents=True,exist_ok=True)
    fl=E.Element('FileList',TargetFrameworkIdentifier='.NETCoreApp',TargetFrameworkVersion='8.0',FrameworkName='Microsoft.WindowsDesktop.App',Name='Source WPF 8 and interim WinForms 7 bootstrap')
    rl=E.Element('FileList',TargetFrameworkIdentifier='.NETCoreApp',TargetFrameworkVersion='8.0',FrameworkName='Microsoft.WindowsDesktop.App')
    provenance=[]
    names=(*wpf,*forms,*shared)
    core={p.stem for p in (sdk/'shared/Microsoft.NETCore.App').glob('*/*.dll')}
    missing={name:sorted(set(identities[name]['References'])-set(names)-core) for name in names}
    missing={name:dependencies for name,dependencies in missing.items() if dependencies}
    if missing: raise RuntimeError('desktop runtime dependency closure is incomplete: '+json.dumps(missing,sort_keys=True))
    for name in names:
        source=files[name]
        reference=b/name/'obj/Release/net8.0/ref'/source.name
        if not reference.is_file(): reference=source
        shutil.copy2(source,framework/source.name)
        shutil.copy2(reference,ref/'ref/net8.0'/source.name)
        shutil.copy2(source,runtime/'runtimes/win-x64/lib/net8.0'/source.name)
        identity=identities[name]
        profile='WPF' if name in wpf and name!='WindowsFormsIntegration' else 'WindowsForms' if name in forms else 'WPF;WindowsForms'
        attrs={'Type':'Managed','AssemblyName':name,'PublicKeyToken':identity['Token'],
               'AssemblyVersion':identity['Version'],'FileVersion':'8.0.0.0','Profile':profile}
        E.SubElement(fl,'File',Path='ref/net8.0/'+source.name,**attrs)
        E.SubElement(rl,'File',Path='runtimes/win-x64/lib/net8.0/'+source.name,**attrs)
        provenance.append({'name':name,'implementation':str(source),'reference':str(reference),
                           'implementationSha256':hashlib.sha256(source.read_bytes()).hexdigest(),
                           'referenceSha256':hashlib.sha256(reference.read_bytes()).hexdigest()})
    for name,file in native_files.items():
        shutil.copy2(file,framework/name)
        shutil.copy2(file,runtime/'runtimes/win-x64/native'/name)
        E.SubElement(rl,'File',Type='Native',Path='runtimes/win-x64/native/'+name)
    generator=sdk/'packs/Microsoft.WindowsDesktop.App.Ref/8.0.0-bootstrap.7.0.20/analyzers/dotnet/cs/System.Windows.Forms.Analyzers.CSharp.dll'
    analyzers=ref/'analyzers/dotnet/cs'; analyzers.mkdir(parents=True,exist_ok=True)
    shutil.copy2(generator,analyzers/generator.name)
    forms_list=E.parse(sdk/'packs/Microsoft.WindowsDesktop.App.Ref/8.0.0-bootstrap.7.0.20/data/FrameworkList.xml')
    analyzer=next(entry for entry in forms_list.getroot() if entry.get('Type')=='Analyzer' and Path(entry.get('Path','')).name==generator.name)
    E.SubElement(fl,'File',**dict(analyzer.attrib))
    for element in (fl,rl): E.indent(element)
    E.ElementTree(fl).write(ref/'data/FrameworkList.xml',encoding='utf-8')
    E.ElementTree(rl).write(runtime/'data/RuntimeList.xml',encoding='utf-8')
    (ref/'data/PlatformManifest.txt').write_text(''.join(name+'.dll|Microsoft.WindowsDesktop.App.Ref|'+identities[name]['Version']+'|8.0.0.0\n' for name in sorted(names)))
    target='.NETCoreApp,Version=v8.0/win-x64'
    library='Microsoft.WindowsDesktop.App/'+version
    assets={'runtime':{name+'.dll':{'assemblyVersion':identities[name]['Version'],'fileVersion':'8.0.0.0'} for name in names},
            'native':{name:{} for name in native_files}}
    deps={'runtimeTarget':{'name':target,'signature':''},'compilationOptions':{},'targets':{target:{library:assets}},
          'libraries':{library:{'type':'project','serviceable':False,'sha512':''}}}
    (framework/'Microsoft.WindowsDesktop.App.deps.json').write_text(json.dumps(deps,indent=2)+'\n')
    (framework/'Microsoft.WindowsDesktop.App.runtimeconfig.json').write_text(json.dumps({'runtimeOptions':{'tfm':'net8.0','framework':{'name':'Microsoft.NETCore.App','version':'8.0.0'}}},indent=2)+'\n')
    status={'wpfSource':'v8.0.0','winformsSource':'v7.0.20','frameworkVersion':version,'assemblies':provenance,
            'nativeCoverage':json.loads((b/'native-coverage.json').read_text()),'trustedNativeSeedDependencies':seed_dependencies,
            'remaining':['complete native text/paragraph/natural-language behavior','WinForms 8 metadata bootstrap','physical print-job coverage','localized/design-time components']}
    (framework/'source-bootstrap.json').write_text(json.dumps(status,indent=2)+'\n')
    desktop=d/'sdk'/sdkver/'Sdks/Microsoft.NET.Sdk.WindowsDesktop'
    shutil.copytree(s/'packaging/Microsoft.NET.Sdk.WindowsDesktop/Sdk',desktop/'Sdk',dirs_exist_ok=True)
    shutil.copytree(s/'packaging/Microsoft.NET.Sdk.WindowsDesktop/targets',desktop/'targets',dirs_exist_ok=True)
    existing=sdk/'sdk'/sdkver/'Sdks/Microsoft.NET.Sdk.WindowsDesktop/targets'
    for source in existing.iterdir():
        target=desktop/'targets'/source.name
        if source.suffix in ('.props','.targets') and not target.exists(): shutil.copy2(source,target)
    tasks=s/'src/Microsoft.DotNet.Wpf/src/PresentationBuildTasks'
    for source in tasks.glob('*.targets'): shutil.copy2(source,desktop/'targets'/source.name)
    tools=desktop/'tools/net8.0'; tools.mkdir(parents=True,exist_ok=True)
    for source in (b/'lib/PresentationBuildTasks.dll',o/'System.CodeDom.dll',o/'System.Reflection.MetadataLoadContext.dll'):
        shutil.copy2(source,tools/source.name)
    props=desktop/'targets/Microsoft.NET.Sdk.WindowsDesktop.targets'
    text=props.read_text().replace('Condition="\'$(ImportFrameworkWinFXTargets)\' != \'true\'"','Condition="\'$(UseWPF)\' == \'true\' And \'$(ImportFrameworkWinFXTargets)\' != \'true\'"')
    extra=E.Element('ItemGroup')
    for suffix,profile in (('',None),('.WPF','WPF'),('.WindowsForms','WindowsForms')):
        attrs={'Include':'Microsoft.WindowsDesktop.App'+suffix,'TargetFramework':'net8.0',
               'RuntimeFrameworkName':'Microsoft.WindowsDesktop.App','DefaultRuntimeFrameworkVersion':version,
               'LatestRuntimeFrameworkVersion':version,'TargetingPackName':'Microsoft.WindowsDesktop.App.Ref',
               'TargetingPackVersion':version,'RuntimePackNamePatterns':'Microsoft.WindowsDesktop.App.Runtime.**RID**',
               'RuntimePackRuntimeIdentifiers':'win-x64','IsWindowsOnly':'true'}
        if profile: attrs['Profile']=profile
        E.SubElement(extra,'KnownFrameworkReference',**attrs)
    E.indent(extra)
    props.write_text(text.replace('</Project>',E.tostring(extra,encoding='unicode')+'\n</Project>'))
    print(f'Staged {len(names)} managed assemblies, {len(native_files)} native libraries and source WindowsDesktop SDK at {d}')


if __name__=='__main__': main()
