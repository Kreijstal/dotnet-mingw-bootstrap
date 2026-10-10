#!/usr/bin/env python3
"""s12globaljson.py <global.json> <sdk-version> [MsbuildSdk=version ...]

Point a repo's global.json at the SDK that builds it and at our MSBuild SDK packages:
  - "sdk": the building SDK's version (the repos pin the Microsoft SDK they were built with, some
    with rollForward=disable), allowPrerelease, rollForward latestFeature;
  - "msbuild-sdks": the given versions (e.g. the one Arcade SDK we build from source, where every
    repo pins a different 8.0 Arcade build); other entries are kept;
  - "tools" is dropped (it only tells eng/common scripts which SDK to download).
The original is kept as global.json.orig.
"""
import json, os, re, sys

path, sdkver = sys.argv[1], sys.argv[2]
src = open(path, encoding='utf-8-sig').read()
if not os.path.exists(path + '.orig'):
    open(path + '.orig', 'w', encoding='utf-8').write(src)
# global.json allows // comments (NuGet.Client has some)
nocomment = re.sub(r'^\s*//.*$', '', src, flags=re.M)
j = json.loads(nocomment)
j['sdk'] = {'version': sdkver, 'allowPrerelease': True, 'rollForward': 'latestFeature'}
j.pop('tools', None)
ms = j.get('msbuild-sdks', {})
for a in sys.argv[3:]:
    k, v = a.split('=', 1)
    ms[k] = v
if ms:
    j['msbuild-sdks'] = ms
open(path, 'w', encoding='utf-8', newline='\n').write(json.dumps(j, indent=2) + '\n')
