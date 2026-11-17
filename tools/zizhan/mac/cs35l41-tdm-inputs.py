#!/usr/bin/env python3
"""Bounded offline ACDB table walk for this phone's actual TDM sink inputs.

No DSP/Android access. Selects only an unambiguous RX tag/channel3/24-bit
candidate via TMLU/TMDE,MTKT/MTLU/MTDE/MTDO and CSLU/CDLU/CDDE/CDDO/POOL.
File identity/provenance must be supplied separately;parsing is not origin
authentication. Proprietary ACDB and the detailed report stay private.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

R=Path(__file__).resolve().parents[3]
def extent(data,offset,size):
    if offset<0 or size<0 or offset>len(data) or size>len(data)-offset:
        raise ValueError("ACDB extent outside chunk")
    return data[offset:offset+size]
def words(data,offset,count):
    if offset%4 or not 0<=count<=16384:raise ValueError("unaligned/large table")
    return struct.unpack("<"+"I"*count,extent(data,offset,4*count))
def count(data,offset,maximum):
    n=words(data,offset,1)[0]
    if n>maximum:raise ValueError("unbounded entry count")
    return n
def chunks(data):
    if len(data)>16*1024*1024 or extent(data,0,8)!=b"ACDB\0\0\0\0" or words(data,8,1)[0]!=len(data)-12:
        raise ValueError("ACDB header/size")
    out={};at=12
    while at<len(data):
        header=extent(data,at,8);tag=header[:4].decode("ascii");size=struct.unpack_from("<I",header,4)[0]
        if tag in out:raise ValueError("duplicate chunk")
        out[tag]=extent(data,at+8,size);at+=8+size
    return out
def definitions(d,offset):
    n=count(d,offset,512);return [words(d,offset+4+i*8,2) for i in range(n)]
def lookup(d,offset,extra):
    nk,nr=words(d,offset,2)
    if nk>8 or nr>2048:raise ValueError("unbounded lookup")
    extent(d,offset+8,nr*(nk+extra)*4)
    for i in range(nr):
        row=words(d,offset+8+i*(nk+extra)*4,nk+extra)
        yield row[:nk],row[nk:]
def blob(c,offset):
    n=count(c["POOL"],offset,1024*1024);return extent(c["POOL"],offset+4,n)
def subgraph_modules(c,subgraph,module_iid):
    if "SGIT" not in c:raise ValueError("missing subgraph module map")
    n=count(c["SGIT"],0,4096);at=4;found=None
    for _ in range(n):
        sg,pc,size=words(c["SGIT"],at,3);body=extent(c["SGIT"],at+12,size);at+=12+size
        if sg!=subgraph:continue
        if found is not None:raise ValueError("duplicate subgraph")
        modules=[];p=0
        for _ in range(pc):
            pd,mc=words(body,p,2);p+=8
            if mc>256:raise ValueError("unbounded module count")
            for _ in range(mc):
                mid,miid=words(body,p,2);p+=8;modules.append((mid,miid))
        if p!=len(body):raise ValueError("subgraph module body length")
        if not any(miid==module_iid for mid,miid in modules):raise ValueError("module instance not in subgraph")
        found=modules
    if found is None:raise ValueError("subgraph missing")
    if not any(mid==0x07001015 for mid,miid in found):raise ValueError("TDM subgraph has no MFC")
    return found
def extract(data):
    c=chunks(data);sinks=set();tags=[];cal=[]
    for key in ("TMLU","TMDE","MTKT","MTLU","MTDE","MTDO","CSLU","CDLU","CDDE","CDDO","POOL"):
        if key not in c:raise ValueError("missing "+key)
    n=count(c["TMLU"],0,4096)
    if len(c["TMLU"])!=4+n*12:raise ValueError("TMLU length")
    for i in range(n):
        sg,tag,de=words(c["TMLU"],4+i*12,3)
        for mid,iid in definitions(c["TMDE"],de):
            if tag==0xc0000004 and mid==0x0700100e:sinks.add((sg,iid))
    n=count(c["MTKT"],0,4096)
    if len(c["MTKT"])!=4+n*12:raise ValueError("MTKT length")
    for i in range(n):
        sg,tag,lu=words(c["MTKT"],4+i*12,3)
        if tag!=0xc0000004:continue
        for values,(de,do) in lookup(c["MTLU"],lu,2):
            defs=definitions(c["MTDE"],de);num=count(c["MTDO"],do,512)
            if len(defs)!=num:raise ValueError("tag definitions/data differ")
            offsets=words(c["MTDO"],do+4,num)
            for (iid,pid),po in zip(defs,offsets):
                if (sg,iid) in sinks and pid==0x0800101b:
                    b=blob(c,po)
                    if len(b)!=28:raise ValueError("TDM body length")
                    fields=struct.unpack("<IIHHI6H",b)
                    tags.append((sg,iid,values,fields,b))
    n=count(c["CSLU"],0,4096);at=4
    for _ in range(n):
        sg,nt=words(c["CSLU"],at,2);at+=8
        if nt>16:raise ValueError("too many calibration selectors")
        for _ in range(nt):
            cid,lu=words(c["CSLU"],at,2);at+=8
            for values,(de,do,prop) in lookup(c["CDLU"],lu,3):
                defs=definitions(c["CDDE"],de);num=count(c["CDDO"],do,512)
                if len(defs)!=num:raise ValueError("cal definitions/data differ")
                offsets=words(c["CDDO"],do+4,num)
                for (iid,pid),po in zip(defs,offsets):
                    if (sg,iid) in sinks and pid in (0x08001017,0x08001018,0x0800106a,0x0800113c):
                        cal.append((sg,iid,pid,blob(c,po)))
    if at!=len(c["CSLU"]):raise ValueError("CSLU length")
    candidates=[]
    for sg,iid,values,f,b in tags:
        if values!=(3,):continue
        payloads={}
        for cs,ci,pid,body in cal:
            if (cs,ci)!=(sg,iid):continue
            if pid in payloads and payloads[pid]!=body:raise ValueError("ambiguous calibration")
            payloads[pid]=body
        if len(payloads)!=4:raise ValueError("incomplete endpoint calibration")
        mf=payloads[0x08001017]
        if len(mf)!=12:raise ValueError("hardware format length")
        rate,bits,channels,fmt=struct.unpack("<IHHI",mf)
        if bits!=24:continue
        if (rate,channels,fmt)!=(96000,3,1) or f!=(0,2,1,1,7,4,32,0,0,1,0):raise ValueError("unqualified native TDM contract")
        if payloads[0x08001018]!=struct.pack("<I",1) or payloads[0x0800106a]!=struct.pack("<HH",2,0) or payloads[0x0800113c]!=struct.pack("<4I",0,0,4,0):raise ValueError("unqualified frame/lane/clock")
        modules=subgraph_modules(c,sg,iid)
        candidates.append(dict(sg=sg,iid=iid,module=0x0700100e,rate=rate,bits=bits,channels=channels,
            tdm=list(f),frequency_hz=rate*f[5]*f[6],payloads={hex(k):v.hex() for k,v in payloads.items()},tdm_body=b.hex()))
        candidates[-1]["graph"]=dict(subgraph=sg,tdm_instance=iid,
            mfc_instances=[miid for mid,miid in modules if mid==0x07001015],
            modules=[dict(module=mid,instance=miid) for mid,miid in modules])
    if len(candidates)!=1:raise ValueError("need one exact24-bit/channel3 RX candidate")
    return candidates[0]
def main():
    a=argparse.ArgumentParser(description=__doc__);a.add_argument("--acdb",type=Path,required=True);a.add_argument("--output",type=Path,required=True);args=a.parse_args()
    if args.output.exists() or R==args.output.resolve() or R in args.output.resolve().parents:a.error("fresh private output outside repository required")
    data=args.acdb.read_bytes();result=dict(status="SOURCE_INPUTS_ONLY__NOT_DEVICE_QUALIFIED",sha256=hashlib.sha256(data).hexdigest(),bytes=len(data),candidate=extract(data),phone_operations=False)
    args.output.write_text(json.dumps(result,indent=2)+"\n");print(result["status"])
if __name__=="__main__":main()
