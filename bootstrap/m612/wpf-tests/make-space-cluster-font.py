#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build a tiny test font from source using only the Python standard library.

The ccmp feature replaces the single space glyph with two blank glyphs. The
Devanagari consonant forces the shaping path; its outline is a simple rectangle.
This font is test data, not an installed font or a binary bootstrap input.
"""
import struct
import sys
from pathlib import Path


def pack(fmt, *values):
    return struct.pack('>' + fmt, *values)


def checksum(data):
    data += b'\0' * (-len(data) % 4)
    return sum(struct.unpack('>' + 'I' * (len(data) // 4), data)) & 0xFFFFFFFF


def build():
    tables = {}
    # Five glyphs: missing, ka, space, left space half, right space half.
    glyph = pack('5hHH4B8h', 1, 50, 0, 550, 700, 3, 0,
                 1, 1, 1, 1, 50, 500, 0, -500, 0, 0, 700, 0)
    glyph += b'\0' * (-len(glyph) % 4)
    tables[b'glyf'] = glyph
    tables[b'loca'] = pack('6I', 0, 0, len(glyph), len(glyph), len(glyph), len(glyph))
    tables[b'hmtx'] = b''.join(pack('Hh', width, bearing) for width, bearing in
                              [(600, 0), (600, 50), (500, 0), (250, 0), (250, 0)])
    tables[b'head'] = pack('4I2H2Q4h2H3h', 0x10000, 0x10000, 0, 0x5F0F3CF5,
                           3, 1000, 3029529600, 3029529600, 50, 0, 550, 700, 0, 8, 2, 1, 0)
    tables[b'hhea'] = pack('I3hH11hH', 0x10000, 800, -200, 0, 600,
                           0, 0, 550, 1, 0, 0, 0, 0, 0, 0, 0, 5)
    tables[b'maxp'] = pack('I14H', 0x10000, 5, 4, 1, 0, 0, 2,
                           0, 0, 0, 0, 0, 0, 0, 0)
    tables[b'post'] = pack('II2h5I', 0x30000, 0, -100, 50, 0, 0, 0, 0, 0)
    # OS/2 version 0: basic horizontal metrics and the Devanagari Unicode range.
    tables[b'OS/2'] = (pack('Hh3H11h', 0, 500, 400, 5, 0,
                            0, 0, 0, 0, 0, 0, 0, 0, 50, 250, 0)
                       + bytes(10) + pack('4I', 1 << 15, 0, 0, 0) + b'BSFT'
                       + pack('3H3h2H', 0x40, 0x20, 0x915, 800, -200, 0, 800, 200))
    cmap = (pack('7H', 4, 40, 0, 6, 4, 1, 2)
            + pack('3HH3H3H3H', 0x20, 0x915, 0xFFFF, 0,
                   0x20, 0x915, 0xFFFF, (2 - 0x20) & 0xFFFF,
                   (1 - 0x915) & 0xFFFF, 1, 0, 0, 0))
    tables[b'cmap'] = pack('HHHHI', 0, 1, 3, 1, 12) + cmap
    names = [(1, 'Bootstrap Space Cluster'), (2, 'Regular'),
             (3, 'BootstrapSpaceCluster-v1'), (4, 'Bootstrap Space Cluster Regular'),
             (6, 'BootstrapSpaceCluster')]
    records, strings = b'', b''
    for name_id, value in names:
        encoded = value.encode('utf-16-be')
        records += pack('6H', 3, 1, 0x409, name_id, len(encoded), len(strings))
        strings += encoded
    tables[b'name'] = pack('3H', 0, len(names), 6 + len(records)) + records + strings
    # DFLT/deva default language -> ccmp feature -> MultipleSubst lookup.
    script = pack('4H', 4, 0, 0, 0xFFFF) + pack('2H', 1, 0)
    scripts = pack('H4sH4sH', 2, b'DFLT', 14, b'deva', 26) + script + script
    features = pack('H4sH3H', 1, b'ccmp', 8, 0, 1, 0)
    subtable = pack('4H3H3H', 1, 14, 1, 8, 2, 3, 4, 1, 1, 2)
    lookups = pack('2H4H', 1, 4, 2, 0, 1, 8) + subtable
    tables[b'GSUB'] = pack('I3H', 0x10000, 10, 10 + len(scripts),
                           10 + len(scripts) + len(features)) + scripts + features + lookups
    count = len(tables)
    power = 1 << (count.bit_length() - 1)
    header = pack('I4H', 0x10000, count, power * 16,
                  power.bit_length() - 1, count * 16 - power * 16)
    directory, payload, head_offset = b'', b'', None
    for tag, data in sorted(tables.items()):
        offset = len(header) + count * 16 + len(payload)
        directory += pack('4s3I', tag, checksum(data), offset, len(data))
        if tag == b'head':
            head_offset = offset
        payload += data + b'\0' * (-len(data) % 4)
    result = bytearray(header + directory + payload)
    struct.pack_into('>I', result, head_offset + 8, (0xB1B0AFBA - checksum(result)) & 0xFFFFFFFF)
    assert checksum(result) == 0xB1B0AFBA
    return result


if __name__ == '__main__':
    output = Path(sys.argv[1])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(build())
    print('Built source space-cluster font:', output)
