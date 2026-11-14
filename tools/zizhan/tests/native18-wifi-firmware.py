#!/usr/bin/env python3
"""Exercise the actual native PWF2 reader with host vnode/metadata adapters.

Reads actual pack payloads and rejects malformed directory/owner/permissions,
then checks cache identity, missing entries and the real cache bound under
ASan/UBSan. No MMIO, firmware execution, image or phone access.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

PREFIX = r'''
#include <cassert>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#define read host_posix_read
#include <unistd.h>
#undef read
struct HostVnode {int fd;};
using vnode_t=HostVnode *;
using caddr_t=char *;
#define NULLVP nullptr
#define VREG 1
#define UIO_READ 0
#define UIO_SYSSPACE 0
#define IO_NOAUTH 0
#ifndef FREAD
#define FREAD 1
#endif
struct vnode_attr {uint64_t va_data_size;unsigned va_uid,va_mode,supported;};
#define BIT_va_data_size 1u
#define BIT_va_uid 2u
#define BIT_va_mode 4u
#define VATTR_INIT(p) memset(p,0,sizeof(*(p)))
#define VATTR_WANTED(p,member) ((void)(p))
#define VATTR_IS_SUPPORTED(p,member) (((p)->supported & BIT_##member)!=0)
static const char *host_path;
static unsigned host_uid=0,host_mode=0644,host_supported=7,host_type=VREG;
static unsigned opens,closes,reads;
extern "C" {vnode_t rootvnode=nullptr;}
static void *vfs_context_kernel(){return nullptr;}
static void *vfs_context_ucred(void *){return nullptr;}
static void *vfs_context_proc(void *){return nullptr;}
static int vnode_open(const char *path,int,int,int,vnode_t *result,void *) {
 assert(!strcmp(path,"/pong/firmware/qca6490.pwf"));
 int fd=open(host_path,O_RDONLY|O_NOFOLLOW);if(fd<0)return errno;
 *result=new HostVnode{fd};opens++;return 0;
}
static int vnode_close(vnode_t node,int,void *){close(node->fd);delete node;closes++;return 0;}
static int vnode_getattr(vnode_t node,vnode_attr *attr,void *) {
 struct stat st;if(fstat(node->fd,&st))return errno;
 attr->va_data_size=st.st_size;attr->va_uid=host_uid;attr->va_mode=host_mode;attr->supported=host_supported;return 0;
}
static unsigned vnode_vtype(vnode_t){return host_type;}
static int vn_rdwr(int,vnode_t node,char *bytes,int size,off_t offset,int,int,void *,int *residual,void *) {
 ssize_t actual=pread(node->fd,bytes,(size_t)size,offset);reads++;
 if(actual<0)return errno;*residual=size-(int)actual;return 0;
}
static void *IOMallocData(size_t n){return malloc(n);}
static void IOFreeData(void *p,size_t){free(p);}
static void IOLog(const char *,...){ }
'''

SUFFIX = r'''
int main(int argc,char **argv) {
 assert(argc==6);HostVnode root{-1};
 host_path=argv[1];assert(!pong_wifi_firmware_ready()&&opens==0);rootvnode=&root;
 host_uid=501;assert(!pong_wifi_firmware_ready()&&opens==closes);host_uid=0;
 host_mode=0664;assert(!pong_wifi_firmware_ready()&&opens==closes);host_mode=0644;
 host_supported=6;assert(!pong_wifi_firmware_ready()&&opens==closes);host_supported=7;
 host_type=2;assert(!pong_wifi_firmware_ready()&&opens==closes);host_type=VREG;
 for(int i=2;i<6;i++){host_path=argv[i];assert(!pong_wifi_firmware_ready()&&opens==closes);}
 host_path=argv[1];assert(pong_wifi_firmware_ready());unsigned once=opens;assert(pong_wifi_firmware_ready()&&opens==once);
 const uint8_t *bytes=nullptr;size_t size=0;
 assert(pong_wifi_firmware_get(nullptr,&bytes,&size)==EINVAL);
 assert(pong_wifi_firmware_get("missing-firmware",&bytes,&size)==ENOENT);
 assert(pong_wifi_firmware_get("a-name-that-is-longer-than-thirty-two-bytes",&bytes,&size)==EINVAL);
 assert(count>=13);
 FILE *file=fopen(argv[1],"rb");assert(file);
 for(unsigned i=0;i<12;i++) {
  assert(!pong_wifi_firmware_get(entries[i].name,&bytes,&size)&&size==entries[i].bytes);
  auto expected=static_cast<uint8_t *>(malloc(size));assert(expected);
  assert(!fseeko(file,entries[i].offset,SEEK_SET));assert(fread(expected,1,size,file)==size);assert(!memcmp(bytes,expected,size));free(expected);
  const uint8_t *same=nullptr;size_t same_size=0;unsigned before=reads;
  assert(!pong_wifi_firmware_get(entries[i].name,&same,&same_size)&&same==bytes&&same_size==size&&reads==before);
 }
 assert(pong_wifi_firmware_get(entries[12].name,&bytes,&size)==ENOSPC);fclose(file);
 for(unsigned i=0;i<cacheCount;i++)free(cached[i].data);
 free(entries);entries=nullptr;vnode_close(pack,FREAD,nullptr);pack=nullptr;
 assert(opens==closes);puts("PASS: actual PWF2 reader metadata/directory refusals, real payloads, cache identity and bound");
 return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'pack', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Retain previous evidence; choose a new output')
    args.out.mkdir(parents=True)
    original = args.source.read_text()
    source = '\n'.join(line for line in original.splitlines() if not line.startswith('#include '))
    unit = args.out / 'actual-reader.cpp'
    unit.write_text(PREFIX + source + SUFFIX)
    binary = args.out / 'actual-reader'
    subprocess.run(['xcrun', 'clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g', str(unit), '-o', str(binary)], check=True)
    data = args.pack.read_bytes()
    if data[:4] != b'PWF2' or not 13 <= struct.unpack_from('<I', data, 4)[0] <= 1024:
        parser.error('Actual complete firmware pack required')
    negatives = []
    for name, mutate in (
        ('bad-magic', lambda b: b.__setitem__(0, b[0] ^ 1)),
        ('bad-count', lambda b: struct.pack_into('<I', b, 4, 1025)),
        ('unaligned-payload', lambda b: struct.pack_into('<I', b, 8 + 32, 1)),
        ('out-of-bounds', lambda b: struct.pack_into('<I', b, 8 + 36, 0xffffffff)),
    ):
        changed = bytearray(data)
        mutate(changed)
        path = args.out / (name + '.pwf')
        path.write_bytes(changed)
        negatives.append(path)
    result = subprocess.run([str(binary.resolve()), str(args.pack.resolve()), *[str(path.resolve()) for path in negatives]], capture_output=True, text=True)
    (args.out / 'run.log').write_text(result.stdout + result.stderr)
    report = {'result': 'PASS' if result.returncode == 0 else 'FAIL', 'returncode': result.returncode,
              'source_sha256': hashlib.sha256(args.source.read_bytes()).hexdigest(),
              'pack_sha256': hashlib.sha256(data).hexdigest(), 'source_compiled_without_kernel_includes_only': True,
              'scope': 'host vnode/metadata adapters around actual source; no firmware/guest/device execution'}
    (args.out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    if result.returncode:
        raise SystemExit('Actual native firmware reader replay failed; inspect retained log')
    print('PASS: actual native PWF2 reader under ASan/UBSan; no device qualification')


if __name__ == '__main__':
    main()
