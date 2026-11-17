#!/usr/bin/env python3
"""ACDB relationship parser controls;synthetic format fixture,not firmware."""
from pathlib import Path
import importlib.util
import struct
R=Path(__file__).resolve().parents[3]
s=importlib.util.spec_from_file_location("tdm_inputs",R/"tools/zizhan/mac/cs35l41-tdm-inputs.py");m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
def w(*v):return struct.pack("<"+"I"*len(v),*v)
sg=0xb0000002;iid=0x428f
def fixture(change=None):
    c={};pool=bytearray()
    def blob(b):at=len(pool);pool.extend(w(len(b))+b);return at
    tdm=struct.pack("<IIHHI6H",0,2,1,1,7,4,32,0,0,1,0)
    pos=blob(tdm);c["TMLU"]=w(1,sg,0xc0000004,0);c["TMDE"]=w(1,0x700100e,iid)
    c["MTKT"]=w(1,sg,0xc0000004,0);c["MTLU"]=w(1,1,3,0,0)
    c["MTDE"]=w(1,iid,0x800101b);c["MTDO"]=w(1,pos)
    ids=[0x8001017,0x8001018,0x800106a,0x800113c]
    bodies=[struct.pack("<IHHI",96000,24,3,1),w(1),struct.pack("<HH",2,0),w(0,0,4,0)]
    offsets=[blob(b) for b in bodies];c["CDDE"]=w(4,*[x for pid in ids for x in [iid,pid]]);c["CDDO"]=w(4,*offsets)
    c["CDLU"]=w(0,1,0,0,0);c["CSLU"]=w(1,sg,1,0,0);c["POOL"]=bytes(pool)
    c["SGIT"]=w(1,sg,1,24,2,2,0x700100e,iid,0x7001015,0x48ba)
    if change:change(c)
    body=b"".join(k.encode()+w(len(v))+v for k,v in c.items());return b"ACDB\0\0\0\0"+w(len(body))+body
result=m.extract(fixture());assert result["frequency_hz"]==12288000
assert result["graph"]["subgraph"]==sg and result["graph"]["mfc_instances"]==[0x48ba]
controls=[
 lambda c:c.update(MTLU=w(9,1,3,0,0)),
 lambda c:c.update(MTDO=w(1,0xfffffff0)),
 lambda c:c.update(MTDE=w(2,iid,0x800101b)),
 lambda c:c.update(TMDE=w(1,0x700100a,iid)),
 lambda c:c.update(CDDO=w(3,0,0,0)),
 lambda c:c.update(CSLU=w(1,sg,0)),
 lambda c:c.update(POOL=c["POOL"][:4]+struct.pack("<IIHHI6H",0,2,1,1,7,8,32,0,0,1,0)+c["POOL"][32:]),
 lambda c:c.pop("MTKT"),
 lambda c:c.update(SGIT=w(1,sg,1,16,2,1,0x700100e,iid)),
 lambda c:c.pop("SGIT"),
]
for change in controls:
    try:m.extract(fixture(change))
    except (ValueError,struct.error):pass
    else:raise AssertionError("malformed/ambiguous relationship accepted")
data=fixture()
for bad in [data[:-1],data[:8]+w(len(data))+data[12:],data+b"MTDE"+w(0)]:
    try:m.extract(bad)
    except ValueError:pass
    else:raise AssertionError("size invalid accepted")
print("PASS actual ACDB relationship parser:14 bounded controls,tag/module/parameter/pool bindings,TDM subgraph/MFC membership and exact native candidate;no hardware")
