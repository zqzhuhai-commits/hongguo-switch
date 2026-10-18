"""Check package metadata and exclude private files before distribution."""
from pathlib import Path
import hashlib
import json
import struct
import sys

root = Path(sys.argv[1]).resolve()
assert (root / 'exefs/main').read_bytes()[:4] == b'NSO0'
npdm = (root / 'exefs/main.npdm').read_bytes()
assert npdm[:4] == b'META'
aci = struct.unpack_from('<I', npdm, 0x70)[0]
assert npdm[aci:aci+4] == b'ACI0'
assert struct.unpack_from('<Q', npdm, aci+0x10)[0] == 0x0100F00D53414600
kac_offset, kac_size = struct.unpack_from('<II', npdm, aci+0x30)
kac = struct.unpack('<'+'I'*(kac_size//4), npdm[aci+kac_offset:aci+kac_offset+kac_size])
assert any((word & 0x3fff) == 0x1fff and ((word >> 14) & 7) == 1 for word in kac), 'NPDM must be an application'
nacp = (root / 'control/control.nacp').read_bytes()
assert len(nacp) == 0x4000
name = nacp[:0x200].split(b'\0', 1)[0].decode('utf-8')
assert name == '红果短剧', name
assert struct.unpack_from('<Q', nacp, 0x3038)[0] == 0x0100F00D53414600
assert nacp[0x3060:0x3070].split(b'\0', 1)[0] == b'1.1.2'
assert (root / 'control/icon_AmericanEnglish.dat').read_bytes()[:2] == b'\xff\xd8'
assert (root / 'romfs/phone_source.js').is_file(), 'New playback source missing'
for file in root.rglob('*'):
    if file.is_file():
        assert file.suffix.lower() != '.keys' and file.name not in ('keys.dat', 'keys.txt', 'prod.keys', 'title.keys'), file.name
        assert not file.is_symlink()
print(json.dumps({'name': name, 'titleId': '0100F00D53414600', 'version': '1.1.2', 'structuralChecksPassed': True, 'hardwareTested': False}, ensure_ascii=False))
