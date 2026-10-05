#!/usr/bin/env python3
"""Check this project's unencrypted, single-content CIA before publication.

Field layouts follow Project_CTR's cia_hdr, ncch_hdr and exefs_hdr. This
checks package integrity and required assets, not console launch behavior.
"""
import hashlib
from pathlib import Path
import struct
import sys


def require(condition, message):
    if not condition:
        raise ValueError(message)


def verify(path):
    require(path.stat().st_size <= 32 * 1024 * 1024, 'CIA exceeds package size limit')
    data = path.read_bytes()
    require(len(data) >= 0x2020, 'Truncated CIA header')
    header, _, _, cert, ticket, tmd, footer, content = struct.unpack_from('<IHHIIIIQ', data)
    require(header == 0x2020, 'Unexpected CIA header')
    align = lambda size: (size + 63) & ~63
    offset = align(header) + align(cert) + align(ticket) + align(tmd)
    require(offset + content + footer <= len(data), 'Truncated CIA content/footer')
    ncch = data[offset:offset + content]
    require(len(ncch) >= 0xA00 and ncch[0x100:0x104] == b'NCCH', 'Missing executable NCCH')
    u32 = lambda pos: struct.unpack_from('<I', ncch, pos)[0]
    require(u32(0x104) * 0x200 == len(ncch), 'Unexpected content count or NCCH size')
    require(ncch[0x18F] & 4, 'Verifier requires unencrypted homebrew NCCH')

    def region(pos, label):
        start, size = u32(pos) * 0x200, u32(pos + 4) * 0x200
        require(start >= 0xA00 and size > 0 and start + size <= len(ncch), f'Missing or invalid {label}')
        return ncch[start:start + size]

    def check_hash(body, expected, label):
        require(hashlib.sha256(body).digest() == expected, f'{label} hash mismatch')

    logo = region(0x198, 'HOME Menu launch logo')
    check_hash(logo, ncch[0x130:0x150], 'Launch logo')
    check_hash(ncch[0x200:0x200 + u32(0x180)], ncch[0x160:0x180], 'Extended header')
    exefs = region(0x1A0, 'ExeFS')
    check_hash(exefs[:u32(0x1A8) * 0x200], ncch[0x1C0:0x1E0], 'ExeFS header')
    files = {}
    for i in range(10):
        entry = i * 16
        name = exefs[entry:entry + 8].rstrip(b'\0').decode('ascii')
        if not name:
            continue
        start, size = struct.unpack_from('<II', exefs, entry + 8)
        start += 0x200
        require(size > 0 and start + size <= len(exefs), f'Invalid ExeFS file: {name}')
        require(name not in files, f'Duplicate ExeFS file: {name}')
        files[name] = exefs[start:start + size]
        hash_pos = 0xC0 + (9 - i) * 32
        check_hash(files[name], exefs[hash_pos:hash_pos + 32], name)
    require({'.code', 'icon', 'banner'} <= files.keys(), 'Missing code, icon or banner')
    require(files['icon'][:4] == b'SMDH', 'Invalid HOME Menu icon')
    romfs = region(0x1B0, 'RomFS')
    require(romfs[:4] == b'IVFC', 'Invalid RomFS header')
    check_hash(romfs[:u32(0x1B8) * 0x200], ncch[0x1E0:0x200], 'RomFS header')
    print(f'{path.name}: required launch assets and package hashes verified')


if __name__ == '__main__':
    try:
        verify(Path(sys.argv[1]))
    except (ValueError, OSError, IndexError, struct.error) as error:
        sys.exit(f'CIA validation failed: {error}')
