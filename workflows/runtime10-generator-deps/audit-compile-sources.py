"""Require compiler caches to contain the exact private source-built packages."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import zipfile

root = Path(sys.argv[1]).resolve()
feeds = [root / 'roslyn-deps9-1/feed', root / 'roslyn414-1/feed']
packages = {p.name.lower(): p for feed in feeds for p in feed.glob('*.nupkg')}
assert len(packages) == 10
runtime_names = list(json.loads((root / 'roslyn-deps9-1/source-inputs.json').read_text())['projects'])
records = []
for out, names in [(root / 'roslyn-deps9-1', runtime_names),
                   (root / 'roslyn414-1', ['Microsoft.CodeAnalysis', 'Microsoft.CodeAnalysis.CSharp'])]:
    for name in names:
        assets = json.loads((out / name / 'obj/project.assets.json').read_text())
        for library, entry in assets['libraries'].items():
            if entry['type'] != 'package':
                continue
            ident, version = library.split('/')
            filename = ident.lower() + '.' + version + '.nupkg'
            assert filename in packages, (name, library)
            locations = []
            for folder in assets['packageFolders']:
                base = Path(subprocess.check_output(['cygpath', '-u', folder], text=True).strip()) / entry['path']
                cached = base / filename
                if cached.is_file():
                    locations.append((cached, base))
            assert len(locations) == 1, (name, library, locations)
            cached, base = locations[0]
            expected = packages[filename]
            assert cached.read_bytes() == expected.read_bytes(), ('Different compiler input package', name, library, cached)
            with zipfile.ZipFile(expected) as archive:
                dlls = [path for path in archive.namelist() if path.endswith('.dll')]
                for path in dlls:
                    assert (base / path).read_bytes() == archive.read(path), (name, library, path)
            records.append(dict(project=name, package=library, cache_archive=str(cached),
                                source_archive_sha256=hashlib.sha256(expected.read_bytes()).hexdigest(),
                                all_cached_dlls_match_source_package=True, dlls_checked=len(dlls)))
(root / 'compile-source-package-audit.json').write_text(json.dumps(records, indent=2) + '\n')
print('PASS actual compiler caches:', len(records), 'source package references and every cached DLL match private source archives')
