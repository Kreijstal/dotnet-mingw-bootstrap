"""Route original framework build helpers to the actual serviced/CoreCLR sources."""
from pathlib import Path
import hashlib,json,sys
root=Path(sys.argv[1]).resolve();base=root/'bootstrap/dotnet-bootstrap-3cbf21078968e2290a54ab093392b4dfeb1c6f1f/bootstrap/m612';records=[]
changes={
 'refpack.sh':[( 'RT=$M612/src/runtime-$RT_VER\n','RT=$M612/src/runtime-$RT_VER-native\n'),(
     '  local dflag=(); [ "$kind" = shim ] && dflag=(--default-items)',
     '  local dflag=(); [[ $kind == shim || $kind == shimref ]] && dflag=(--default-items)'),(
     '  while IFS=$\'\\t\' read -r n kind p deps; do\n    if [ "$kind" = stub ]; then',
     '  while IFS=$\'\\t\' read -r n kind p deps; do\n    [ -z "${ONLY:-}" ] || [[ " $ONLY " == *" $n "* ]] || continue\n    if [ "$kind" = stub ]; then')],
 'tools/libplan.py':[("corelib_proj = R + '/src/mono/System.Private.CoreLib/System.Private.CoreLib.csproj'","runtime_flavor = os.environ.get('RUNTIME_FLAVOR', 'Mono')\ncorelib_proj = R + ('/src/coreclr/System.Private.CoreLib/System.Private.CoreLib.csproj' if runtime_flavor == 'CoreCLR' else '/src/mono/System.Private.CoreLib/System.Private.CoreLib.csproj')"),("'Configuration': 'Release', 'RuntimeFlavor': 'Mono', 'IsSourceProject': 'true'","'Configuration': 'Release', 'RuntimeFlavor': runtime_flavor, 'IsSourceProject': 'true'"),(
     "    for kind in ('Descriptors', 'Substitutions', 'LinkAttributes'):\n", "    assembly_kind = kind\n    for kind in ('Descriptors', 'Substitutions', 'LinkAttributes'):\n"),(
     "    plan[n] = (kind, tf); deps[n] = dl", "    plan[n] = (assembly_kind, tf); deps[n] = dl")],
 'libraries.sh':[( '  [ ${#failed[@]} -eq 0 ] || echo "FAILED (${#failed[@]}): ${failed[*]}"','  [ ${#failed[@]} -eq 0 ] || { echo "FAILED (${#failed[@]}): ${failed[*]}"; return 1; }'),('  local rc=0; mono "$VBC" -nologo -noconfig @"$(winpath "$rsp")" > "$B/obj/$n.log" 2>&1 || rc=$?', '  local runner=(mono); [[ $VBC != *.dll ]] || runner=("$CSC_DOTNET")\n  local rc=0; "${runner[@]}" "$VBC" -nologo -noconfig @"$(winpath "$rsp")" > "$B/obj/$n.log" 2>&1 || rc=$?'),('System.Text.RegularExpressions.Generator) echo System.Text.RegularExpressions.Generator System.Buffers ;;','System.Text.RegularExpressions.Generator) echo System.Text.RegularExpressions.Generator ;;')]
}
changes['refpack.sh'] += [
 ('"$L/$n/Directory.Build.props" "$L/$n/src/$n.csproj" 2>/dev/null | head -1 | cut -d\'>\' -f2 || true)\n  key=${key:-Open}',
  '"$L/$n/Directory.Build.props" "$L/$n/src/$n.csproj" "$p" 2>/dev/null | head -1 | cut -d\'>\' -f2 || true)\n  key=${key:-Open}'),
 ('"${dargs[@]}" "${refs[@]}" "$o/AssemblyInfo.g.cs" "${items[@]}" || die "$n failed ($B/obj/$n.log)"',
  '"${dargs[@]}" "${refs[@]}" "$o/AssemblyInfo.g.cs" "${items[@]}" || die "$n failed ($B/obj/$n.log)"\n  /usr/bin/python3 "${RUNTIME_CLEAR_FORWARDER_SCRIPT:?}" "$RUNTIME_PREPARED_ROOT" "$O/$n.dll"')]
changes['libraries.sh'].append(
 ('  cp "$B/obj/$n/$n.dll" "$O/$n.dll"',
  '  /usr/bin/python3 "${RUNTIME_CLEAR_FORWARDER_SCRIPT:?}" "$RUNTIME_PREPARED_ROOT" "$B/obj/$n/$n.dll"\n  cp "$B/obj/$n/$n.dll" "$O/$n.dll"'))
for name,pairs in changes.items():
 file=base/name;text=file.read_text()
 # Accept the already prepared form so new helper corrections can be applied
 # without replacing the source tree or invalidating completed compiler outputs.
 for a,b in pairs:
  if text.count(b)==1:continue
  assert text.count(a)==1,(name,a)
  text=text.replace(a,b)
 original=text
 for a,b in reversed(pairs):
  assert original.count(b)==1,(name,b)
  original=original.replace(b,a)
 before=original.encode()
 file.write_text(text);records.append(dict(path=name,original_sha256=hashlib.sha256(before).hexdigest(),prepared_sha256=hashlib.sha256(file.read_bytes()).hexdigest(),replacements=[dict(before=a,after=b) for a,b in pairs]))
(root/'framework-build-adapters.json').write_text(json.dumps(records,indent=2)+'\n')
print('PASS original build helpers select serviced sources/CoreCLR, retain assembly kinds in the dependency plan, fail on every compile error, and use installed source VB compiler')
