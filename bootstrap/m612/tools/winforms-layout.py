#!/usr/bin/env python3
'''Stage the explicitly labelled WinForms-only .NET 8 bootstrap framework and SDK.'''
import argparse,json,shutil,xml.etree.ElementTree as E
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('build');p.add_argument('oob');p.add_argument('source');p.add_argument('wpf');p.add_argument('dest');p.add_argument('--metadata',required=True);a=p.parse_args()
b,o,s,w,d=map(Path,(a.build,a.oob,a.source,a.wpf,a.dest))
v='8.0.0-bootstrap.7.0.20'; sdkver='8.0.131'
framework=d/'shared/Microsoft.WindowsDesktop.App'/v
ref=d/'packs/Microsoft.WindowsDesktop.App.Ref'/v
runtime=d/'packs/Microsoft.WindowsDesktop.App.Runtime.win-x64'/v
names=('Accessibility','System.Drawing.Common','System.Windows.Forms','System.Windows.Forms.Primitives','Microsoft.Win32.SystemEvents','System.Configuration.ConfigurationManager','System.Diagnostics.EventLog','System.Security.Cryptography.ProtectedData','System.Resources.Extensions','System.Windows.Extensions')
for path in (framework,ref/'ref/net8.0',ref/'data',ref/'analyzers/dotnet/cs',runtime/'runtimes/win-x64/lib/net8.0',runtime/'data'): path.mkdir(parents=True,exist_ok=True)
files={f.stem:f for f in o.glob('*.dll')};files.update({f.stem:f for f in (b/'lib').glob('*.dll')})
refs={}
for name in names:
    source=files[name]
    shutil.copy2(source,framework/source.name)
    reference=b/name/'obj/Release/net8.0/ref'/source.name
    refs[name]=reference if reference.exists() else source
    shutil.copy2(refs[name],ref/'ref/net8.0'/source.name)
    shutil.copy2(source,runtime/'runtimes/win-x64/lib/net8.0'/source.name)
identities={entry['Name']:entry for entry in json.loads(Path(a.metadata).read_text())['assemblies']}
fl=E.Element('FileList',TargetFrameworkIdentifier='.NETCoreApp',TargetFrameworkVersion='8.0',FrameworkName='Microsoft.WindowsDesktop.App',Name='WinForms 7.0.20 source bootstrap on .NET 8 (WindowsForms only)')
rl=E.Element('FileList',TargetFrameworkIdentifier='.NETCoreApp',TargetFrameworkVersion='8.0',FrameworkName='Microsoft.WindowsDesktop.App')
for name in names:
    attrs={'Type':'Managed','AssemblyName':name,'PublicKeyToken':identities[name]['Token'],'AssemblyVersion':identities[name]['Version'],'FileVersion':'8.0.0.0','Profile':'WindowsForms'}
    E.SubElement(fl,'File',Path='ref/net8.0/'+name+'.dll',**attrs)
    E.SubElement(rl,'File',Path='runtimes/win-x64/lib/net8.0/'+name+'.dll',**attrs)
generator=b/'lib/System.Windows.Forms.Analyzers.CSharp.dll'
shutil.copy2(generator,ref/'analyzers/dotnet/cs'/generator.name)
E.SubElement(fl,'File',Type='Analyzer',Language='cs',Path='analyzers/dotnet/cs/'+generator.name,AssemblyName=generator.stem,AssemblyVersion='8.0.0.0',FileVersion='8.0.0.0',PublicKeyToken=identities[generator.stem]['Token'],Profile='WindowsForms')
E.indent(fl);E.indent(rl)
E.ElementTree(fl).write(ref/'data/FrameworkList.xml',encoding='utf-8')
E.ElementTree(rl).write(runtime/'data/RuntimeList.xml',encoding='utf-8')
(ref/'data/PlatformManifest.txt').write_text(''.join(n+'.dll|Microsoft.WindowsDesktop.App.Ref|8.0.0.0|8.0.0.0\n' for n in sorted(names)))
(framework/'Microsoft.WindowsDesktop.App.runtimeconfig.json').write_text(json.dumps({'runtimeOptions':{'tfm':'net8.0','framework':{'name':'Microsoft.NETCore.App','version':'8.0.0'}}},indent=2)+'\n')
(framework/'Microsoft.WindowsDesktop.App.deps.json').write_text(json.dumps({'runtimeTarget':{'name':'.NETCoreApp,Version=v8.0/win-x64','signature':''},'compilationOptions':{},'targets':{'.NETCoreApp,Version=v8.0/win-x64':{'Microsoft.WindowsDesktop.App/'+v:{'runtime':{n+'.dll':{'assemblyVersion':'8.0.0.0','fileVersion':'8.0.0.0'} for n in names}}}},'libraries':{'Microsoft.WindowsDesktop.App/'+v:{'type':'project','serviceable':False,'sha512':''}}},indent=2)+'\n')
(framework/'source-bootstrap.json').write_text(json.dumps({'status':'interim WindowsForms-only source port','winformsSource':'v7.0.20','drawingSource':'v8.0.0','runtimeSource':'v8.0.0','targetFramework':'net8.0','frameworkVersion':v,'excluded':['WinForms 8 CsWin32 migration','WPF','design-time assemblies','localized satellite assemblies']},indent=2)+'\n')
sdk=d/'sdk'/sdkver/'Sdks/Microsoft.NET.Sdk.WindowsDesktop'
for rel in ('Sdk/Sdk.props','Sdk/Sdk.targets','targets/Microsoft.NET.Sdk.WindowsDesktop.props','targets/Microsoft.NET.Sdk.WindowsDesktop.targets','targets/Microsoft.NET.Sdk.WindowsDesktop.WPF.props'):
    src=w/'packaging/Microsoft.NET.Sdk.WindowsDesktop'/rel;dst=sdk/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
for src in (s/'pkg/Microsoft.Private.Winforms/sdk/dotnet-wpf').glob('*.props'): shutil.copy2(src,sdk/'targets'/src.name)
for src in (s/'pkg/Microsoft.Private.Winforms/sdk/dotnet-wpf').glob('*.targets'): shutil.copy2(src,sdk/'targets'/src.name)
target=sdk/'targets/Microsoft.NET.Sdk.WindowsDesktop.targets'
text=target.read_text().replace('Condition="\'$(ImportFrameworkWinFXTargets)\' != \'true\'"','Condition="\'$(UseWPF)\' == \'true\' And \'$(ImportFrameworkWinFXTargets)\' != \'true\'"')
# WPF build tasks are not part of this interim package. Diagnose before any missing import.
text=text.replace('</Project>', '''  <ItemGroup>
    <KnownFrameworkReference Include="Microsoft.WindowsDesktop.App" TargetFramework="net8.0"
      RuntimeFrameworkName="Microsoft.WindowsDesktop.App" DefaultRuntimeFrameworkVersion="'''+v+'''"
      LatestRuntimeFrameworkVersion="'''+v+'''" TargetingPackName="Microsoft.WindowsDesktop.App.Ref"
      TargetingPackVersion="'''+v+'''" RuntimePackNamePatterns="Microsoft.WindowsDesktop.App.Runtime.**RID**"
      RuntimePackRuntimeIdentifiers="win-x64" IsWindowsOnly="true" />
    <KnownFrameworkReference Include="Microsoft.WindowsDesktop.App.WindowsForms" TargetFramework="net8.0"
      RuntimeFrameworkName="Microsoft.WindowsDesktop.App" DefaultRuntimeFrameworkVersion="'''+v+'''"
      LatestRuntimeFrameworkVersion="'''+v+'''" TargetingPackName="Microsoft.WindowsDesktop.App.Ref"
      TargetingPackVersion="'''+v+'''" RuntimePackNamePatterns="Microsoft.WindowsDesktop.App.Runtime.**RID**"
      RuntimePackRuntimeIdentifiers="win-x64" Profile="WindowsForms" IsWindowsOnly="true" />
  </ItemGroup>
</Project>''')
target.write_text(text)
(sdk/'targets/Microsoft.WinFX.targets').write_text('''<Project><Target Name="SourceBootstrapWpfUnavailable" BeforeTargets="PrepareForBuild"><Error Text="This source bootstrap package supports Windows Forms only; WPF is not built yet." /></Target></Project>''')
print('Staged '+v+' at '+str(d))
