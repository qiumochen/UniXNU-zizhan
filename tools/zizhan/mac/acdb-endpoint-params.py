#!/usr/bin/env python3
"""List ACDB parameters of a hardware endpoint module (host only, read-only).

usage: acdb-endpoint-params.py ACDB [TAG_HEX default c0000005] [MODULE_HEX default 0700100f]
Prints tagged and static calibration parameters of every instance and the module list of
its subgraph, reusing the strict chunk parsers in cs35l41-tdm-inputs.py.
"""
import sys,struct,importlib.util
from pathlib import Path
spec=importlib.util.spec_from_file_location("t",Path(__file__).with_name("cs35l41-tdm-inputs.py"));t=importlib.util.module_from_spec(spec);spec.loader.exec_module(t)
data=open(sys.argv[1],"rb").read();c=t.chunks(data)
want_tag=int(sys.argv[2],16) if len(sys.argv)>2 else 0xc0000005
want_mid=int(sys.argv[3],16) if len(sys.argv)>3 else 0x0700100f;srcs=set()
n=t.count(c["TMLU"],0,4096)
for i in range(n):
    sg,tag,de=t.words(c["TMLU"],4+i*12,3)
    for mid,iid in t.definitions(c["TMDE"],de):
        if tag==want_tag and mid==want_mid:srcs.add((sg,iid))
print("sources",[(hex(a),hex(b)) for a,b in sorted(srcs)])
n=t.count(c["MTKT"],0,4096)
for i in range(n):
    sg,tag,lu=t.words(c["MTKT"],4+i*12,3)
    if tag!=want_tag:continue
    for values,(de,do) in t.lookup(c["MTLU"],lu,2):
        defs=t.definitions(c["MTDE"],de);num=t.count(c["MTDO"],do,512);offsets=t.words(c["MTDO"],do+4,num)
        for (iid,pid),po in zip(defs,offsets):
            if (sg,iid) in srcs:print("tagged",hex(sg),hex(iid),"kv",values,hex(pid),t.blob(c,po).hex())
n=t.count(c["CSLU"],0,4096);at=4
for _ in range(n):
    sg,nt=t.words(c["CSLU"],at,2);at+=8
    for _ in range(nt):
        cid,lu=t.words(c["CSLU"],at,2);at+=8
        for values,(de,do,prop) in t.lookup(c["CDLU"],lu,3):
            defs=t.definitions(c["CDDE"],de);num=t.count(c["CDDO"],do,512);offsets=t.words(c["CDDO"],do+4,num)
            for (iid,pid),po in zip(defs,offsets):
                if (sg,iid) in srcs:print("cal",hex(sg),hex(iid),"ckv",values,hex(pid),t.blob(c,po).hex())
for sg,iid in sorted(srcs):
    try:
        n2=t.count(c["SGIT"],0,4096);at=4
        for _ in range(n2):
            s,pc,size=t.words(c["SGIT"],at,3);body=t.extent(c["SGIT"],at+12,size);at+=12+size
            if s!=sg:continue
            p=0;mods=[]
            for _ in range(pc):
                pd,mc=t.words(body,p,2);p+=8
                for _ in range(mc):
                    mid,miid=t.words(body,p,2);p+=8;mods.append((hex(mid),hex(miid)))
            print("modules",hex(sg),mods)
    except Exception as e:print("sgit",e)
