"""Reproduce the historical device patch; normal source builds retain the layout check.
The device patch skips the calibration-layout guard and can retain an obsolete
mm/px scale after ROI/camera movement. It does not validate that scale.
"""
from pathlib import Path
import argparse, struct, hashlib
def symbol(data,name):
    assert data[:6]==b'\x7fELF\x01\x01'
    shoff=struct.unpack_from('<I',data,32)[0]
    size,count=struct.unpack_from('<HH',data,46)
    sections=[struct.unpack_from('<10I',data,shoff+i*size) for i in range(count)]
    for sec in sections:
        if sec[1]!=2:continue
        strings=sections[sec[6]]; table=data[strings[4]:strings[4]+strings[5]]
        for off in range(sec[4],sec[4]+sec[5],sec[9]):
            n,value,nbytes,info,other,index=struct.unpack_from('<IIIBBH',data,off)
            end=table.find(b'\0',n)
            if table[n:end].decode()==name:
                target=sections[index]
                return target[4]+(value&~1)-target[3],nbytes
    raise RuntimeError('symbol missing')

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('input');p.add_argument('output')
p.add_argument('--acknowledge-unbound-calibration', action='store_true')
a=p.parse_args()
if not a.acknowledge_unbound_calibration:
    p.error('Historical compatibility requires --acknowledge-unbound-calibration')
b=bytearray(Path(a.input).read_bytes())
off,size=symbol(b,'_ZN2vm17analyzeMultiPointERKNS_17MultiPointCaptureERKNS_13MeasureConfigEd')
assert size==0xc7c, 'Compiler output differs: do not patch a different instruction layout'
at=off+0x5d8
assert b[at-8:at].hex()=='d5f84ca0baf1000f', 'Instruction anchor differs'
assert b[at:at+2]==bytes.fromhex('05d0'), 'Expected original conditional branch'
b[at:at+2]=bytes.fromhex('0de0')
dest=Path(a.output);dest.parent.mkdir(parents=True, exist_ok=True);dest.write_bytes(b)
print('Historical layout bypass applied; metric calibration remains unqualified.')
print(hashlib.sha256(b).hexdigest())
