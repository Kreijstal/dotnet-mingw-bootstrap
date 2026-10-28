"""Stage an isolated serviced runtime build from two pinned source archives."""
from pathlib import Path
import hashlib,json,shutil,subprocess,sys,tarfile

runtime,bootstrap,out=(Path(p).resolve() for p in sys.argv[1:]);out.mkdir();here=Path(__file__).resolve().parent
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(runtime)=='b32a56a29cc0248bed219e1c7ef026bf8f006d997f5d6346382e63a22ecd9070'
assert sha(bootstrap)=='75bea2026b90c7f4d5f88279c4ff25ca07b4a6eb0e435df90a41c6f485f773c6'
with tarfile.open(bootstrap) as t:t.extractall(out/'bootstrap',filter='data')
base=out/'bootstrap/dotnet-bootstrap-3cbf21078968e2290a54ab093392b4dfeb1c6f1f'
source=out/'m/src/runtime-8.0.31-native';source.mkdir(parents=True)
original={};removed=[];links=[]
with tarfile.open(runtime) as t:
    assert t.pax_headers['comment']=='1219a42122cf5190c7f512850557e38c422430dc'
    for m in t:
        if m.isdir():continue
        prefix,path=m.name.split('/',1);assert prefix=='runtime-8.0.31'
        assert '..' not in Path(path).parts and not Path(path).is_absolute()
        if m.issym():
            assert path=='src/native/external/libunwind/README.md' and m.linkname=='README'
            links.append(dict(path=path,target=m.linkname));continue
        assert m.isfile(),m.name
        data=t.extractfile(m).read()
        if Path(path).suffix.lower() in {'.dll','.exe','.so','.dylib','.nupkg','.a','.lib','.o','.obj','.winmd','.pdb','.resources'}:
            removed.append(dict(path=path,sha256=hashlib.sha256(data).hexdigest()));continue
        target=source/path;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
        original[path]=hashlib.sha256(data).hexdigest()
for link in links:
    target=source/link['path'];target.write_bytes((target.parent/link['target']).read_bytes())
patches=base/'patches/m612/runtime-8.0.31';patches.mkdir()
records=[]
for patch in sorted((here/'patches').glob('*.patch')):
    old=base/'patches/m612/runtime-8.0.0'/patch.name;assert old.is_file()
    shutil.copyfile(patch,patches/patch.name)
    result=subprocess.run(['patch','--fuzz=0','--no-backup-if-mismatch','-p1','-i',str(patch)],cwd=source,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    assert result.returncode==0,result.stdout
    records.append(dict(name=patch.name,original_sha256=sha(old),ported_sha256=sha(patch),log=result.stdout))
assert len(records)==17
(source/'.applied-patches').write_text(''.join(r['name']+'\n' for r in records))
(source/'.extracted').touch();(source/'.commit').write_text('1219a42122cf5190c7f512850557e38c422430dc\n')
# The helper must find the already-verified source input rather than downloading.
shutil.copyfile(runtime,out/'m/src/runtime-8.0.31.tar.gz')
shutil.copyfile(bootstrap,out/'m/src/dotnet-bootstrap-source.tar.gz')
record=dict(runtime_version='8.0.31',runtime_source_commit='1219a42122cf5190c7f512850557e38c422430dc',runtime_archive_sha256=sha(runtime),bootstrap_commit='3cbf21078968e2290a54ab093392b4dfeb1c6f1f',bootstrap_archive_sha256=sha(bootstrap),original_file_sha256=original,materialized_readme_links=links,removed_binary_inputs=removed,patches=records,serviced_runtime_completed=False)
(out/'source-inputs.json').write_text(json.dumps(record,indent=2)+'\n')
print('PASS isolated complete runtime8.0.31/pinned bootstrap source,17 zero-fuzz patches and binary-input removal; runtime build/managed framework/integration remain open')
