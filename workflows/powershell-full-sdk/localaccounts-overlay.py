"""Apply explicit analyzer adaptations to the pinned LocalAccounts sources."""
from pathlib import Path
import difflib
import hashlib
import json
import re
import sys
import tarfile

ARCHIVE_SHA256 = '3dac352752acd64a0502c1f002ecf65eca177d2919bea3bcefbf8856afedd9eb'
PREFIX = 'src/Microsoft.PowerShell.LocalAccounts/LocalAccounts/'


def adapted_sources(original):
    """Return exact expected bytes without removing any original source file."""
    result = dict(original)

    def change(name, before, after, count=1):
        path = PREFIX + name
        assert result[path].count(before.encode()) == count, (path, before)
        result[path] = result[path].replace(before.encode(), after.encode())

    spacing = 0
    endings = 0
    for path in sorted(result):
        if path.startswith(PREFIX + 'Commands/'):
            spacing += result[path].count(b';}')
            result[path] = result[path].replace(b';}', b'; }')
        if path.startswith(PREFIX) and result[path] != result[path].rstrip(b'\n') + b'\n':
            result[path] = result[path].rstrip(b'\n') + b'\n'
            endings += 1
    assert (spacing, endings) == (47, 15), (spacing, endings)

    change('Extensions.cs', 'sidBinary.Length-4', 'sidBinary.Length - 4')
    change('Extensions.cs', '(long) (((long)sidBinary[2])', '(long)(((long)sidBinary[2])')
    change('Native.cs', '    struct LARGE_INTEGER\n', '    internal struct LARGE_INTEGER\n')
    change('SamApi.cs', '        UNICODE_STRING AdminComment;', '        private UNICODE_STRING AdminComment;')
    change('NtStatus.cs', '\n\n\n        /////////////////////////////////////////////////////////////////////////',
           '\n\n        /////////////////////////////////////////////////////////////////////////')
    change('Sam.cs', '(int) SamApi.USER_NORMAL_ACCOUNT', '(int)SamApi.USER_NORMAL_ACCOUNT')
    change('Sam.cs', 'for (int i=0;', 'for (int i = 0;')
    change('Sam.cs', 'if (   properties != UserProperties.None', 'if (properties != UserProperties.None')
    for name in ['userHandle', 'securityObject', 'sidBytes']:
        change('Sam.cs', 'IntPtr.Zero != ' + name, name + ' != IntPtr.Zero')
    change('Sam.cs', '            if (context == null)\n                context = this.context;',
           '            context ??= this.context;')
    change('Sam.cs', 'private OperatingSystem GetOperatingSystem()',
           'private static OperatingSystem GetOperatingSystem()')
    change('Sam.cs', 'private PrincipalSource? GetPrincipalSource(SecurityIdentifier sid)',
           'private static PrincipalSource? GetPrincipalSource(SecurityIdentifier sid)')
    change('Sam.cs', 'private PrincipalSource? GetPrincipalSource(AccountInfo info)',
           'private static PrincipalSource? GetPrincipalSource(AccountInfo info)')
    change('Sam.cs', 'private LocalPrincipal MakeLocalPrincipalObject(AccountInfo info)',
           'private static LocalPrincipal MakeLocalPrincipalObject(AccountInfo info)')
    change('StringUtil.cs', 'internal class StringUtil', 'internal static class StringUtil')
    change('StringUtil.cs', '''        /// <summary>
        /// Private constructor to present auto-generation of a default constructor with greater accessibility.
        /// </summary>
        private StringUtil()
        {
        }

''', '')
    path = PREFIX + 'Exceptions.cs'
    text = result[path].decode()
    pattern = r'^(        )(protected \w+\(SerializationInfo info, StreamingContext ctx\))'
    text, count = re.subn(pattern, r'\1[Obsolete("Formatter-based serialization is obsolete.", DiagnosticId = "SYSLIB0051")]\n\1\2', text, flags=re.M)
    assert count == 17, count
    result[path] = text.encode()
    return result


def main():
    source = Path(sys.argv[1]).resolve()
    root = source.parent
    archive = root / 'powershell-7.4.20.tar.gz'
    sha = lambda data: hashlib.sha256(data).hexdigest()
    assert sha(archive.read_bytes()) == ARCHIVE_SHA256
    with tarfile.open(archive) as tar:
        original = {p.name.removeprefix('PowerShell-7.4.20/'): tar.extractfile(p).read()
                    for p in tar if p.isfile() and p.name.startswith('PowerShell-7.4.20/' + PREFIX)
                    and p.name.endswith('.cs')}
    expected = adapted_sources(original)
    changed = {}
    diff = []
    for name in sorted(original):
        path = source / name
        current = path.read_bytes()
        # A revised overlay can be reapplied to its own previous adaptation;
        # reconstruct expected bytes from the pinned archive every time.
        if current != original[name]:
            previous = json.loads((root / 'localaccounts-source-overlay.json').read_text())
            assert sha(current) == previous['changed_sources'][name]['adapted_sha256'], ('Unrecorded source edit', name)
        if original[name] != expected[name]:
            changed[name] = dict(original_sha256=sha(original[name]), adapted_sha256=sha(expected[name]))
            diff.extend(difflib.unified_diff(original[name].decode().splitlines(keepends=True),
                                           expected[name].decode().splitlines(keepends=True),
                                           fromfile='a/' + name, tofile='b/' + name))
    for name in changed:
        (source / name).write_bytes(expected[name])
    (root / 'localaccounts-source-overlay.json').write_text(json.dumps(dict(
        archive_sha256=ARCHIVE_SHA256, helper_sha256=sha(Path(__file__).read_bytes()),
        changed_sources=changed, removed_original_sources=[],
        legacy_serialization='17 protected constructors retained and annotated SYSLIB0051 obsolete',
        analyzers_disabled=False), indent=2) + '\n')
    (root / 'localaccounts-source-overlay.diff').write_text(''.join(diff))
    print('PASS exact LocalAccounts analyzer adaptations:', len(changed), 'files; all original sources retained')


if __name__ == '__main__':
    main()
