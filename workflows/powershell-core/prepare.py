"""Prepare a source-only PowerShell core build without narrowing its Windows sources."""
from pathlib import Path
import hashlib,json,subprocess,sys,xml.etree.ElementTree as E
source=Path(sys.argv[1]).resolve();root=source.parent
assert source.name=='PowerShell-7.4.20'
assert not (source/'Directory.Build.targets').exists()
removed=[]
for p in source.rglob('*'):
 if p.is_file() and p.suffix.lower() in {'.dll','.exe','.pdb','.nupkg','.so','.dylib','.obj','.lib','.a','.resources'}:
  removed.append(dict(path=p.relative_to(source).as_posix(),sha256=hashlib.sha256(p.read_bytes()).hexdigest()));p.unlink()
generator=source/'src/System.Management.Automation/SourceGenerators/PSVersionInfoGenerator/PSVersionInfoGenerator.csproj'
original_generator=generator.read_bytes();tree=E.parse(generator);framework=tree.find('.//TargetFramework');assert framework.text=='netstandard2.0';framework.text='net8.0';E.indent(tree);tree.write(generator,encoding='utf-8',xml_declaration=True)
original_global=(source/'global.json').read_text();(source/'global.json').write_text(json.dumps({'sdk':{'version':'8.0.131','rollForward':'disable','allowPrerelease':False}},indent=2)+'\n')
native=lambda p:subprocess.check_output(['cygpath','-am',str(p)],text=True).strip()
(root/'NuGet.config').write_text('<configuration><packageSources><clear/><add key="installed-source" value="'+native(Path('/clang64/lib/dotnet/library-packs'))+'"/></packageSources></configuration>\n')
commit='013edf910bbe781f503c27370941ce54d02d3f33'
p=E.Element('Project');props=E.SubElement(p,'PropertyGroup')
values=dict(RuntimeIdentifiers='',RuntimeIdentifier='',UseAppHost='false',PublishReadyToRun='false',PublishReadyToRunEmitSymbols='false',NuGetAudit='false',EnableNETAnalyzers='true',EnforceCodeStyleInBuild='true',UseSharedCompilation='false',CscToolPath=native(Path('/clang64/lib/dotnet/compiler-packs/roslyn-4.9.2')),CscToolExe='csc49.cmd',ReleaseTag='7.4.20',PowerShellVersion='v7.4.20-0-g'+commit,PSCoreBuildVersion='7.4.20',PSCoreCommitSHA=commit,PSCoreFileVersion='7.4.20.500',PSCoreFormattedVersion='7.4.20 SHA: '+commit,ProductVersion='7.4.20 SHA: '+commit,Version='7.4.20',FileVersion='7.4.20.500',AssemblyVersion='7.4.6.500',InformationalVersion='7.4.20 SHA: '+commit)
for k,v in values.items():E.SubElement(props,k).text=v
# The source generator executes in the source-built .NET8 compiler host. Its
# production algorithm is unchanged; source Roslyn references target net8.0.
E.SubElement(props,'TargetFramework',Condition="'$(MSBuildProjectName)' == 'PSVersionInfoGenerator'").text='net8.0'
E.SubElement(props,'PublicSign',Condition="'$(SignAssembly)' == 'true'").text='true'
E.SubElement(props,'DelaySign',Condition="'$(SignAssembly)' == 'true'").text='false'
# Archive builds have no .git checkout. Preserve the pinned release/commit values
# above instead of invoking git describe or inventing a mutable build identity.
target=E.SubElement(p,'Target',Name='GetPSCoreVersionFromGit',BeforeTargets='_GenerateRestoreProjectSpec;GenerateNuspec;BeforeBuild')
E.SubElement(target,'RemoveDir',Directories='$(CompilerGeneratedFilesOutputPath)',Condition="'$(CompilerGeneratedFilesOutputPath)' != '' and Exists('$(CompilerGeneratedFilesOutputPath)')")
items=E.SubElement(p,'ItemGroup');E.SubElement(items,'Compile',Remove='$(CompilerGeneratedFilesOutputPath)/**/*.cs',Condition="'$(CompilerGeneratedFilesOutputPath)' != ''")
(root/'manifests').mkdir()
target=E.SubElement(p,'Target',Name='CaptureSourceBuildInputs',BeforeTargets='CoreCompile')
E.SubElement(target,'WriteLinesToFile',File=native(root/'manifests')+'/$(MSBuildProjectName)-compile.txt',Lines='@(Compile->\'%(FullPath)\')',Overwrite='true')
E.SubElement(target,'WriteLinesToFile',File=native(root/'manifests')+'/$(MSBuildProjectName)-analyzers.txt',Lines='@(Analyzer->\'%(FullPath)\')',Overwrite='true')
E.SubElement(target,'WriteLinesToFile',File=native(root/'manifests')+'/$(MSBuildProjectName)-references.txt',Lines='@(ReferencePath->\'%(FullPath)\')',Overwrite='true')
# Resolve the complete core reference set before compilation, following the SDK
# type-catalog target's explicit exclusion of MMI. The full SDK catalog remains
# separate until its SqlClient/Compatibility closure is completed.
target=E.SubElement(p,'Target',Name='GetCoreSourceReferences',DependsOnTargets='ResolveReferences')
items=E.SubElement(target,'ItemGroup');E.SubElement(items,'_CoreSourceReference',Include='@(ReferencePath)',Condition="'%(ReferencePath.Filename)' != 'Microsoft.Management.Infrastructure'")
E.SubElement(target,'WriteLinesToFile',File=native(root/'core-references.inc'),Lines='@(_CoreSourceReference->\'%(FullPath);\')',Overwrite='true')
E.indent(p);E.ElementTree(p).write(source/'Directory.Build.targets',encoding='utf-8',xml_declaration=True)
(root/'source-build-inputs.json').write_text(json.dumps(dict(version='7.4.20',source_commit=commit,source_archive_sha256='3dac352752acd64a0502c1f002ecf65eca177d2919bea3bcefbf8856afedd9eb',original_global_json=original_global,project_overrides=[dict(path=generator.relative_to(source).as_posix(),original_sha256=hashlib.sha256(original_generator).hexdigest(),source_build_sha256=hashlib.sha256(generator.read_bytes()).hexdigest(),change='TargetFramework netstandard2.0 -> net8.0; original generator algorithm unchanged')],removed_prebuilt_inputs=removed,build_adjustments=['select installed source SDK8.0.131','source archive release metadata instead of git describe','source generator targets host net8.0 for source Roslyn4.9.2','public signing with original public key','preserve generator cleanup and exclude persisted generator outputs from Compile','framework-dependent IL build, no apphost or R2R download','source SDK .NET analyzers8.0.0-preview.26274.2, CodeStyle4.9.2 and explicit source Documentation/StyleCop/CodeAnalysis analyzers retained'],limits=['MMI3 is experimental','actual runtime8.0.0 servicing remains','core-reference catalog only; full SDK closure still needs SqlClient/Compatibility','full PowerShell application and modules not yet built']),indent=2)+'\n')
print('Prepared complete original Windows core projects, original source generators and installed-source-only feed')
