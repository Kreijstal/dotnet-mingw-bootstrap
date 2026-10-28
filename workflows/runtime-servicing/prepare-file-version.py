"""Apply the upstream-calculated file version to the original build helpers."""
from pathlib import Path
import hashlib,json,sys
root=Path(sys.argv[1]).resolve()
base=root/'bootstrap/dotnet-bootstrap-3cbf21078968e2290a54ab093392b4dfeb1c6f1f/bootstrap/m612'
assert (root/'file-version.txt').read_text().strip()=='8.0.3126.42000'
changes={
 'corehost.sh':[
  ('  IFS=. read -r maj min pat <<< "$RT_VER"', '  IFS=. read -r maj min pat <<< "$RT_VER"\n  local fmaj fmin fbuild frevision\n  IFS=. read -r fmaj fmin fbuild frevision <<< "${RUNTIME_FILE_VERSION:?missing upstream file version}"',1),
  ('#define RuntimeFileMajorVersion $maj\n#define RuntimeFileMinorVersion $min\n#define RuntimeFileBuildVersion $pat\n#define RuntimeFileRevisionVersion 0', '#define RuntimeFileMajorVersion $fmaj\n#define RuntimeFileMinorVersion $fmin\n#define RuntimeFileBuildVersion $fbuild\n#define RuntimeFileRevisionVersion $frevision',1),
  ('#define VER_PRODUCTVERSION          $maj,$min,$pat,0','#define VER_PRODUCTVERSION          $fmaj,$fmin,$fbuild,$frevision',1),
  ('#define VER_FILEVERSION             $maj,$min,$pat,0','#define VER_FILEVERSION             $fmaj,$fmin,$fbuild,$frevision',1),
  ('#define VER_FILEVERSION_STR         "$maj.$min.$pat.0"','#define VER_FILEVERSION_STR         "$RUNTIME_FILE_VERSION"',1)],
 'corelib.sh':[('$RT_VER.0','${RUNTIME_FILE_VERSION:?missing upstream file version}',1)],
 'libraries.sh':[('$RT_VER.0','${RUNTIME_FILE_VERSION:?missing upstream file version}',3)],
 'refpack.sh':[('$PACK_VER.0','${RUNTIME_FILE_VERSION:?missing upstream file version}',4)]}
records=[]
for name,pairs in changes.items():
 path=base/name;before=path.read_bytes();text=before.decode()
 for old,new,count in pairs:
  if text.count(new)==count:continue
  assert text.count(old)==count,(name,old,text.count(old),count)
  text=text.replace(old,new)
 path.write_text(text)
 records.append(dict(path=name,original_sha256=hashlib.sha256(before).hexdigest(),prepared_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),replacements=[dict(before=old,after=new,count=count) for old,new,count in pairs]))
(root/'file-version-adapters.json').write_text(json.dumps(records,indent=2)+'\n')
print('PASS original native/CoreLib/C#/VB/reference helpers retain product8.0.31 and use calculated file8.0.3126.42000')
