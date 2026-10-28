"""Compare the selected production sources with a pristine pinned runtime tree."""
from pathlib import Path
import hashlib
import json
import posixpath
import sys
import tarfile
import xml.etree.ElementTree as E

evidence, pristine, archive = (Path(p).resolve() for p in sys.argv[1:])
with archive.open('rb') as stream:
    archive_sha256 = hashlib.file_digest(stream, 'sha256').hexdigest()
assert archive_sha256 == '1692acae5e3adfd625452fbc9db4fd4e982c10e38189358ae245c74dd5514d7a'
status = json.loads((evidence / 'build-status.json').read_text())
assert status['complete_declared_reference_graph'] and len(status['projects']) == 186
original_cs = {}
generated_stubs = 0
for name, project in status['projects'].items():
    for path, expected in project['source_cs'].items():
        if len(path) > 1 and path[1] == ':':
            assert project['kind'] == 'stub', (name, path)
            generated_stubs += 1
            continue
        data = (pristine / path).read_bytes()
        assert hashlib.sha256(data).hexdigest() == expected, ('Original source differs', name, path)
        original_cs[path] = expected
projects = json.loads((evidence / 'source-projects.json').read_text())
for path, expected in projects.items():
    assert hashlib.sha256((pristine / path).read_bytes()).hexdigest() == expected, path
roots = json.loads((evidence / 'source-roots.json').read_text())
assert hashlib.sha256((pristine / 'src/libraries/NetCoreAppLibrary.props').read_bytes()).hexdigest() == roots['netcoreapp_library_props_sha256']
clones = {p.name: p for p in (evidence / 'project-inputs').glob('*.proj')}
checked = set()
def normalized(element):
    return (element.tag, sorted(element.attrib.items()), (element.text or '').strip(), [normalized(c) for c in element])
for relative in projects:
    original_path = roots['msys_root'] + '/r/' + relative
    native_parent = roots['native_root'] + '/r/' + posixpath.dirname(relative)
    for strip_imports in [False, True]:
        filename = hashlib.sha256((original_path + str(strip_imports)).encode()).hexdigest()[:16] + '.proj'
        if filename not in clones:
            continue
        body = (pristine / relative).read_text(encoding='utf-8-sig')
        body = body.replace('$(MSBuildThisFileDirectory)', native_parent + '/')
        body = body.replace('$(MSBuildProjectDirectory)', native_parent)
        tree = E.fromstring(body)
        tree.attrib.pop('Sdk', None)
        for parent in tree.iter():
            for item in list(parent):
                if item.tag == 'Import' and strip_imports:
                    parent.remove(item)
                    continue
                for attribute in ['Include', 'Update', 'Remove', 'Project']:
                    if attribute not in item.attrib or item.tag not in {'Compile', 'ProjectReference', 'Import', 'EmbeddedResource', 'None', 'Content'}:
                        continue
                    value = item.get(attribute).replace('\\', '/')
                    if '$(' not in value and not value.startswith('/') and not (len(value) > 1 and value[1] == ':'):
                        item.set(attribute, posixpath.normpath(native_parent + '/' + value))
        assert normalized(tree) == normalized(E.parse(clones[filename]).getroot()), ('Unexpected private project edit', relative, strip_imports)
        checked.add(filename)
assert checked == set(clones), ('Unverified private clones', set(clones) - checked)
archive_inputs = dict(original_cs, **projects)
archive_inputs['src/libraries/NetCoreAppLibrary.props'] = roots['netcoreapp_library_props_sha256']
verified_archive_inputs = set()
with tarfile.open(archive) as tar:
    for member in tar:
        if not member.isfile():
            continue
        relative = member.name.partition('/')[2]
        if relative not in archive_inputs:
            continue
        assert hashlib.sha256(tar.extractfile(member).read()).hexdigest() == archive_inputs[relative], relative
        verified_archive_inputs.add(relative)
assert verified_archive_inputs == set(archive_inputs)
assert generated_stubs == 19
result = dict(pristine_runtime_version='10.0.5',
              pristine_runtime_commit='081d220c0a773ffb7c6bea6b48727833576a65ef',
              original_cs_inputs=len(original_cs), original_cs_sha256=original_cs,
              original_project_and_property_inputs=len(projects),
              original_project_and_property_sha256=projects,
              original_stub_target_generated_sources=generated_stubs,
              private_project_clones_verified_against_pristine=len(checked),
              original_runtime_archive_sha256=archive_sha256,
              original_runtime_archive_selected_inputs_verified=len(verified_archive_inputs),
              all_selected_original_inputs_match_pristine_source=True,
              production_cs_changes=0, full_dependency_goal_complete=False)
(evidence / 'pristine-source-audit.json').write_text(json.dumps(result, indent=2) + '\n')
print('PASS', len(original_cs), 'original C# inputs and', len(projects), 'project/property inputs match pristine runtime10 source;19 original-target generated stubs')
