"""Validate full generator compiler inputs, actual references and PE resources."""
from pathlib import Path
import argparse,hashlib,json,posixpath,subprocess

parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('root',type=Path);parser.add_argument('metadata',type=Path);args=parser.parse_args()
root=args.root.resolve();out=root/'targeting-generators';source=root/'m/src/runtime-8.0.31-native'
records=json.loads((out/'source-inputs.json').read_text());metadata=json.loads(args.metadata.read_text());by_file={posixpath.normpath(row['file'].replace('\\','/')).lower():row for row in metadata}
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
native=lambda p:subprocess.check_output(['cygpath','-am',str(p)],text=True).strip().replace('\\','/')
prefix=posixpath.normpath(native(source))+'/'
result={}
for label,item in records.items():
 folder=out/label;actual={};generated={}
 for line in (folder/'obj/Release/net8.0/Compile.txt').read_text(encoding='utf-8-sig').splitlines():
  line=posixpath.normpath(line.replace('\\','/'))
  if line.lower().startswith(prefix.lower()):
   path=line[len(prefix):];actual[path]=sha(source/path)
  else:
   base=posixpath.normpath(native(folder))+'/'
   assert line.lower().startswith(base.lower()),line
   path=line[len(base):]
   assert path in ['SR.Generated.cs','obj/Release/net8.0/Build.AssemblyInfo.cs','obj/Release/net8.0/.NETCoreApp,Version=v8.0.AssemblyAttributes.cs'],path
   generated[path]=sha(folder/path)
 assert actual==item['original_compiler_inputs'],(label,sorted(set(item['original_compiler_inputs'])-set(actual)),sorted(set(actual)-set(item['original_compiler_inputs'])))
 for path,digest in item['original_project_resource_hashes'].items():assert sha(source/path)==digest,path
 references=(folder/'obj/Release/net8.0/ReferencePathWithRefAssemblies.txt').read_text(encoding='utf-8-sig').splitlines()
 assert references,label
 if label.startswith('Json'):assert not any(Path(line.replace('\\','/')).name=='System.Text.Json.dll' for line in references),label
 for line in references:
  line=line.replace('\\','/')
  assert '/targeting-generators/cache/' in line or '/packs/Microsoft.NETCore.App.Ref/8.0.31/ref/net8.0/' in line or '/targeting-generators/Microsoft.Interop.SourceGeneration/' in line,line
 assembly=folder/'bin/Release/net8.0'/(item['assembly']+'.dll')
 paths=[(assembly,'',item['neutral_strings'])]+[(assembly.parent/culture/(item['assembly']+'.resources.dll'),culture,strings) for culture,strings in item['translated_strings'].items()]
 outputs={}
 for file,culture,strings in paths:
  key=posixpath.normpath(native(file)).lower();row=by_file[key]
  assert row['sha256']==sha(file),file
  assert row['version']=='8.0.0.0' and row['public_key_token']=='cc7b13ffcd2ddd51',(file,row['version'],row['public_key_token'])
  assert row['attributes']['AssemblyFileVersionAttribute']=='8.0.3126.42000',file
  anchor='FxResources.'+item['assembly']+'.SR'+('.'+culture if culture else '')+'.resources'
  assert row['resources']=={anchor:strings},(label,culture,sorted(row['resources']))
  outputs[file.relative_to(out).as_posix()]=sha(file)
 result[label]=dict(original_compiler_inputs=len(actual),generated_input_sha256=generated,reference_inputs=references,source_and_satellite_output_sha256=outputs,original_project_resource_hashes=item['original_project_resource_hashes'])
(out/'build-audit.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS full original JS/JSON compiler inputs and actual source references, exact neutral/XLF PE strings, signing/version identities and all'+str(sum(len(item['source_and_satellite_output_sha256']) for item in result.values()))+' outputs; serviced targeting package/legacy host compatibility remain open')
