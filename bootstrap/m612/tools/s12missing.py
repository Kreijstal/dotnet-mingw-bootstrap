#!/usr/bin/env python3
"""s12missing.py <restore-log> <tree> <sbrp-src>

Read NU1101/NU1102/NU1103 errors of a restore and work out which packages (id/version) are missing.
NU1101 gives no version, so the requested ranges are read from the project.assets.json files under
<tree> (restore writes them even when it fails).  Prints the missing id/version pairs that
source-build-reference-packages has (lower case, as s12-sbrp.sh REFS wants them); the others go to
stderr as "NOT-IN-SBRP id range (projects)".
"""
import json, os, re, sys

log, tree, sbrp = sys.argv[1:4]
text = open(log, encoding='utf-8', errors='replace').read()
ids = {}
for m in re.finditer(r'error NU110[123]: Unable to find package (\S+?)(?:\. No packages| with version \(([^)]*)\))', text):
    ids.setdefault(m.group(1), set())
    if m.group(2):
        ids[m.group(1)].add(m.group(2).strip())
# NU1603 (warning as error in some repos): "X depends on Y (>= V) but Y V was not found"
for m in re.finditer(r'NU1603: (?:Warning As Error: )?.*? depends on (\S+) \(>= ([^)]+)\) but', text):
    ids.setdefault(m.group(1), set()).add(m.group(2).strip())
# A newer seed can satisfy a >= range while being incompatible with the TFM
# or causing a downgrade of another central version. Build the exact direct
# references from SBRP instead of treating that newer selection as sufficient.
exact_references = bool(re.search(r'error NU(?:1202|1109):', text))
refs = os.path.join(sbrp, 'src', 'referencePackages', 'src')
text_only = os.path.join(sbrp, 'src', 'textOnlyPackages', 'src')
# requested ranges from the assets files
for dp, dn, fn in os.walk(tree):
    dn[:] = [d for d in dn if d not in ('.git', 'node_modules')]
    if 'project.assets.json' in fn:
        try:
            j = json.load(open(os.path.join(dp, 'project.assets.json'), encoding='utf-8-sig'))
        except Exception:
            continue
        for fw in j.get('project', {}).get('frameworks', {}).values():
            for name, d in fw.get('dependencies', {}).items():
                if exact_references and 'version' in d:
                    version = re.sub(r'[\[\]()\s]', '', d['version']).split(',')[0].lstrip('>=')
                    if (os.path.isdir(os.path.join(refs, name.lower(), version)) or
                            os.path.isdir(os.path.join(text_only, name.lower(), version))):
                        ids.setdefault(name, set()).add(d['version'])
                for i in ids:
                    if i.lower() == name.lower() and 'version' in d:
                        ids[i].add(d['version'])
refs = os.path.join(sbrp, 'src', 'referencePackages', 'src')
text_only = os.path.join(sbrp, 'src', 'textOnlyPackages', 'src')
for i in sorted(ids):
    if not ids[i]:
        # a transitive NU1101 (no version anywhere): every version SBRP has of it
        d = os.path.join(refs, i.lower())
        if os.path.isdir(d):
            for v in sorted(os.listdir(d)):
                print('%s/%s' % (i.lower(), v))
            continue
    for r in sorted(ids[i]) or ['?']:
        v = re.sub(r'[\[\]()\s]', '', r).split(',')[0].lstrip('>=')
        if v and (os.path.isdir(os.path.join(refs, i.lower(), v)) or os.path.isdir(os.path.join(text_only, i.lower(), v))):
            print('%s/%s' % (i.lower(), v))
        else:
            print('NOT-IN-SBRP %s %s' % (i, r), file=sys.stderr)
