#!/usr/bin/env python3
"""Build upstream WPF C# dependency/cycle projects with source-only inputs.

Cycle-breaker and ref projects produce compile-only reference assemblies. Only
real implementation projects are copied to build/lib for runtime consumption.
"""
import argparse
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
import csproj_items as ci

def main():
    parser=argparse.ArgumentParser()
    for name in ('source','build','sdk','oob','keys','project'):
        parser.add_argument(name)
    args=parser.parse_args()
    source,build,sdk,oob,keys=map(Path,(args.source,args.build,args.sdk,args.oob,args.keys))
    root=source/'src/Microsoft.DotNet.Wpf/src'
    cycles=source/'src/Microsoft.DotNet.Wpf/cycle-breakers'
    project=Path(args.project)
    if not project.is_absolute(): project=root/project
    mappings={}
    active=[]
    completed={}
    def win(path):
        return subprocess.check_output(['cygpath','-m',str(path.resolve())],text=True).strip()
    def model(path):
        props={'TargetFramework':'net8.0','TargetFrameworkIdentifier':'.NETCoreApp','TargetFrameworkVersion':'v8.0',
               'TargetOS':'windows','TargetsWindows':'true','Configuration':'Release','MSBuildProjectName':path.stem,
               'DefaultLanguageSourceExtension':'.cs','WpfSourceDir':str(root.resolve())+'/',
               'WpfSharedDir':str((root/'Shared').resolve())+'/', 'WpfCommonDir':str((root/'Common').resolve())+'/',
               'WpfCycleBreakersDir':str(cycles.resolve())+'/', 'SystemIOPackagingPackage':'System.IO.Packaging',
               'SystemDirectoryServicesPackage':'System.DirectoryServices','DefineConstants':''}
        evaluator=ci.Evaluator(props,[name for name in props if name!='DefineConstants'])
        evaluator.load(str(path),{'ProjectReference'},True)
        return evaluator
    def build_project(path):
        path=path.resolve(); key=str(path)
        if key in completed: return completed[key]
        if key in active: raise SystemExit('Unbroken WPF source dependency cycle: '+' -> '.join(active+[key]))
        if not path.is_file(): raise SystemExit('Missing upstream WPF project: '+key)
        if path.suffix.lower()=='.vcxproj':
            artifact=build/'lib'/(path.stem+'.dll')
            if path.stem in {'DirectWriteForwarder','System.Printing'} and artifact.is_file():
                completed[key]=artifact; mappings[key]=str(artifact.resolve()); return artifact
            if path.stem=='System.Printing':
                artifact=build_project(path.parent/'ref/System.Printing-ref.csproj')
                completed[key]=artifact; mappings[key]=str(artifact.resolve())
                print('Compile-only printing reference; native implementation remains pending',flush=True)
                return artifact
            raise SystemExit('Native/CLI implementation must be source-built first: '+key)
        evaluator=model(path)
        assembly=evaluator.props.get('AssemblyName',path.stem)
        reference_only='cycle-breakers' in path.parts or path.parent.name=='ref'
        # These verified implementation artifacts already precede this graph.
        seeds={'WindowsBase','System.Xaml','System.Windows.Input.Manipulations','UIAutomationTypes','UIAutomationProvider','PresentationCore','DirectWriteForwarder','PresentationFramework','ReachFramework','PresentationUI'}
        candidate=build/'lib'/(assembly+'.dll')
        if path!=project.resolve() and not reference_only and assembly in seeds and candidate.is_file():
            completed[key]=candidate; mappings[key]=str(candidate.resolve()); return candidate
        if path.suffix.lower()!='.csproj':
            raise SystemExit('Native/CLI implementation must be source-built first: '+key)
        active.append(key)
        for dependency,metadata in evaluator.items.get('ProjectReference',[]):
            if metadata.get('ReferenceOutputAssembly')=='false': continue
            build_project(Path(dependency))
        unit=path.stem
        out=build/unit
        out.mkdir(parents=True,exist_ok=True)
        mapfile=out/'dependency-map.json'
        mapfile.write_text(json.dumps(mappings,indent=2)+'\n')
        command=['/usr/bin/python3',str(Path(__file__).with_name('wpfplan.py')),str(source),str(build),str(sdk),str(oob),str(keys),unit,
                 '--project',str(path),'--dependency-map',str(mapfile)]
        if reference_only: command.append('--reference-only')
        subprocess.run(command,check=True)
        generated=out/(unit+'.csproj')
        with (out/'build.log').open('w') as log:
            result=subprocess.run([str(sdk/'dotnet.exe'),'build',win(generated),'-c','Release',
                                   '--configfile',win(build/'NuGet.config'),'-p:UseSharedCompilation=false','-p:NuGetAudit=false','-v:q'],stdout=log,stderr=subprocess.STDOUT)
        if result.returncode:
            print((out/'build.log').read_text())
            raise SystemExit('WPF source build failed: '+unit)
        artifact=out/'bin/Release/net8.0'/(assembly+'.dll')
        if not artifact.is_file(): raise SystemExit('Missing source-built WPF artifact: '+str(artifact))
        if not reference_only:
            candidate.write_bytes(artifact.read_bytes()); artifact=candidate
        completed[key]=artifact; mappings[key]=str(artifact.resolve()); active.pop()
        print(('Compiled reference: ' if reference_only else 'Built implementation: ')+unit+' -> '+str(artifact),flush=True)
        return artifact
    result=build_project(project)
    manifest=build/'wpf-source-graph.json'
    manifest.write_text(json.dumps({'target':str(project.resolve()),'artifact':str(result.resolve()),'projects':mappings},indent=2)+'\n')

if __name__=='__main__': main()
