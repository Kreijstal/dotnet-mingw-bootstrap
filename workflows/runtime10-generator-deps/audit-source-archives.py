"""Independently match selected production inputs to both pinned source archives."""
from pathlib import Path
import hashlib
import json
import sys
import tarfile

evidence, archives = (Path(p).resolve() for p in sys.argv[1:])
configs = [('runtime9', 'runtime9-source-inputs.json', 'runtime-v9.0.0.tar.gz'),
           ('roslyn414', 'roslyn414-source-inputs.json', 'roslyn-4.14.0-8edf7bcd.tar.gz')]
results = {}
for label, manifest, filename in configs:
    records = json.loads((evidence / manifest).read_text())
    archive = archives / filename
    with archive.open('rb') as stream:
        assert hashlib.file_digest(stream, 'sha256').hexdigest() == records['source_archive_sha256']
    wanted = {}
    counts = {}
    for name, record in records['projects'].items():
        assert record['production_cs_changes'] == 0
        counts[name] = len(record['original_cs'])
        for path, expected in record['original_cs'].items():
            if path in wanted:
                assert wanted[path] == expected
            wanted[path] = expected
        for key in ['resource_files_sha256', 'original_resources']:
            wanted.update(record.get(key, {}))
        if 'original_project' in record:
            wanted[record['original_project']] = record['original_project_sha256']
    verified = set()
    with tarfile.open(archive) as tar:
        assert tar.pax_headers['comment'] == records.get('runtime_commit', records.get('roslyn_commit'))
        for member in tar:
            if not member.isfile():continue
            name = member.name.partition('/')[2]
            if name not in wanted:continue
            assert hashlib.sha256(tar.extractfile(member).read()).hexdigest() == wanted[name], (label, name)
            verified.add(name)
    assert verified == set(wanted), (label, set(wanted) - verified)
    results[label] = dict(original_cs_by_project=counts, selected_archive_inputs_verified=len(verified),
                          all_selected_inputs_match_pinned_archive=True, production_cs_changes=0)
    print('PASS', label, len(verified), 'selected original source/project/resource inputs match pinned archive')
(evidence / 'source-archive-audit.json').write_text(json.dumps(results, indent=2) + '\n')
