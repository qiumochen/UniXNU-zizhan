#!/usr/bin/env python3
"""Actual QMP/SMEM owner helpers and vnode firmware adapter; no phone operations."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
DRIVERS = ROOT / "drivers/PongPower"
firmware = (DRIVERS / "PongFirmware.c").read_text()
firmware = "\n".join(line for line in firmware.splitlines() if not line.startswith("#include"))
harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include "ZizhanDspFirmwarePolicy.h"
#ifndef EFTYPE
#define EFTYPE 79
#endif
typedef void *vnode_t;
typedef void *vfs_context_t;
typedef char *caddr_t;
#define NULLVP NULL
#define FREAD 1
#define UIO_READ 1
#define UIO_SYSSPACE 2
#define IO_NOAUTH 3
struct vnode_attr {uint64_t va_data_size;};
#define VATTR_INIT(p) memset(p,0,sizeof(*(p)))
#define VATTR_WANTED(p,field) ((void)(p))
#define VATTR_IS_SUPPORTED(p,field) supported
static int supported=1,opens,closes,reads,open_error,attr_error,read_error,short_read;
static uint64_t file_size=9494904;
static vfs_context_t vfs_context_kernel(void){return (void*)1;}
static void *vfs_context_ucred(vfs_context_t p){return p;}
static void *vfs_context_proc(vfs_context_t p){return p;}
static int vnode_open(const char *path,int flags,int a,int b,vnode_t *out,vfs_context_t ctx){
 assert(zizhan_dsp_firmware_path(path)&&flags==FREAD&&!a&&!b&&ctx);++opens;
 if(open_error){return open_error;}*out=(void*)2;return 0;
}
static int vnode_close(vnode_t p,int flags,vfs_context_t ctx){assert(p&&flags==FREAD&&ctx);++closes;return 0;}
static int vnode_getattr(vnode_t p,struct vnode_attr *a,vfs_context_t ctx){assert(p&&ctx);a->va_data_size=file_size;return attr_error;}
static int vn_rdwr(int op,vnode_t p,caddr_t to,int n,off_t at,int space,int auth,void *cred,int *resid,void *proc){
 assert(op==UIO_READ&&p&&to&&n>=0&&at>=0&&space==UIO_SYSSPACE&&auth==IO_NOAUTH&&cred&&proc);
 ++reads;*resid=short_read;return read_error;
}
/* ACTUAL_FIRMWARE */
int main(void){
 void *handle=(void*)9;uint64_t size=9;char data[16];
 const char *bad[]={NULL,"","/pong/firmware/../slpi.mbn","/pong/firmware/slpi.mbn/",
   "/pong/firmware/slpi.mbn.extra","/tmp/slpi.mbn","/pong/firmware/adsp_dtb.mbn"};
 for(unsigned i=0;i<sizeof bad/sizeof *bad;++i){
  assert(sheng_fw_open(bad[i],&handle,&size)==EINVAL&&!handle&&!size);
 }
 assert(!opens);
 assert(sheng_fw_open("/pong/firmware/slpi.mbn",NULL,&size)==EINVAL&&!opens);
 assert(!sheng_fw_open("/pong/firmware/slpi.mbn",&handle,&size)&&size==9494904&&handle);
 sheng_fw_close(handle);assert(closes==1);
 assert(!sheng_fw_open("/pong/firmware/adsp.mbn",&handle,&size));sheng_fw_close(handle);
 open_error=ENOENT;
 assert(sheng_fw_open("/pong/firmware/slpi.mbn",&handle,&size)==ENOENT&&!handle&&!size);
 open_error=0;
 for(unsigned i=0;i<4;++i){
  file_size=i==0?0:i==1?64ull*1024*1024+1:10;supported=i!=2;attr_error=i==3?EIO:0;
  int before=closes;
  assert(sheng_fw_open("/pong/firmware/slpi.mbn",&handle,&size)!=0&&!handle&&!size);
  assert(closes==before+1);
 }
 supported=1;attr_error=0;file_size=64ull*1024*1024;
 assert(!sheng_fw_open("/pong/firmware/slpi.mbn",&handle,&size));
 assert(sheng_fw_read(handle,UINT64_MAX,data,1)==EINVAL&&!reads);
 assert(sheng_fw_read(handle,64ull*1024*1024,data,1)==EINVAL&&!reads);
 assert(sheng_fw_read(handle,0,data,1024*1024+1)==EINVAL&&!reads);
 assert(sheng_fw_read(handle,0,NULL,1)==EINVAL&&!reads);
 assert(sheng_fw_read(NULL,0,data,1)==EINVAL&&!reads);
 assert(!sheng_fw_read(handle,64ull*1024*1024-16,data,16)&&reads==1);
 short_read=1;assert(sheng_fw_read(handle,0,data,16)==EIO);short_read=0;
 read_error=EACCES;assert(sheng_fw_read(handle,0,data,16)==EACCES);
 sheng_fw_close(handle);
}
'''

with tempfile.TemporaryDirectory(prefix="zizhan-slpi-resources-") as temp:
    temp = Path(temp)
    common = ["-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-I" + str(DRIVERS)]
    objects = []
    for name in ("QcomQmp", "QcomSmem"):
        obj = temp / (name + ".o")
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", *common,
                        "-c", str(DRIVERS / (name + ".c")), "-o", str(obj)], check=True, timeout=30)
        objects.append(str(obj))
    binary = temp / "resources"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", *common,
                    str(ROOT / "tools/zizhan/tests/slpi-shared-resources.cpp"), *objects,
                    "-o", str(binary)], check=True, timeout=30)
    subprocess.run([str(binary)], check=True, timeout=10)
    source = temp / "firmware.c"
    source.write_text("#include <sys/types.h>\n" + harness.replace("/* ACTUAL_FIRMWARE */", firmware))
    binary = temp / "firmware"
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", *common,
                    str(source), "-o", str(binary)], check=True, timeout=30)
    subprocess.run([str(binary)], check=True, timeout=10)
print("PASS SLPI prerequisites: actual shared QMP/SMEM helpers and firmware vnode adapter under ASan/UBSan; no DSP start")
