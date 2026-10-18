from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parent
record=json.loads((root/'verification.json').read_text())
data=b''.join((root/name).read_bytes() for name in record['parts'])
assert len(data)==record['bytes'] and hashlib.sha256(data).hexdigest()==record['sha256']
(root/'Hongguo-Switch-1.1.2.nsp').write_bytes(data)
print('Approved NSP assembled; SHA256 matches user-tested package')
