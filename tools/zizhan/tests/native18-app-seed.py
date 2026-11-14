#!/usr/bin/env python3
"""Replay the actual native bootstrap seed parser against temporary files.

Only the syscall path adapter is mocked: guest absolute paths map below a
temporary fixture. Actual parsing, O_EXCL/O_NOFOLLOW copy and source policy
execute with ASan/UBSan. Requires root for the actual owned-manifest check.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


PREFIX = r'''
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "Native18AppSeed.h"
static const char *fixture;
static const char *mapped(const char *path) {
 static char result[4096];assert(path[0]=='/');
 int n=snprintf(result,sizeof result,"%s%s",fixture,path);
 assert(n>0&&(size_t)n<sizeof result);return result;
}
static int mapped_open(const char *path,int flags,int mode) {return open(mapped(path),flags,mode);}
#define pb_open mapped_open
#define pb_lstat(path,value) lstat(mapped(path),value)
#define pb_fstat fstat
#define pb_mkdir(path,mode) mkdir(mapped(path),mode)
#define pb_chown(path,uid,gid) chown(mapped(path),uid,gid)
#define pb_chmod(path,mode) chmod(mapped(path),mode)
#define pb_unlink(path) unlink(mapped(path))
#define pb_link(source,path) symlink(source,mapped(path))
#define pb_read read
#define pb_write write
#define pb_close close
#define pb_sync fsync
#define pb_error errno
#define PONG_SYSTEM_BUILD "22G100"
static int pb_write_all(int fd,const void *bytes,size_t n) {
 const char *p=bytes;while(n){ssize_t k=write(fd,p,n);if(k<0&&errno==EINTR)continue;
 if(k<=0)return -1;p+=k;n-=(size_t)k;}return 0;
}
'''

SUFFIX = r'''
static void content(const char *path,const char *expected) {
 char buffer[64];int fd=mapped_open(path,O_RDONLY|O_NOFOLLOW,0);assert(fd>=0);
 ssize_t size=read(fd,buffer,sizeof buffer);close(fd);
 assert(size==(ssize_t)strlen(expected)&&!memcmp(buffer,expected,(size_t)size));
}
int main(int argc,char **argv) {
 assert(argc==2);fixture=argv[1];
 const struct {const char *path;int success;} tests[]={
 {"/pong/etc/var.skel",1},
 {"/pong/etc/native-apps-000.skel",1},
 {"/pong/etc/foreign.skel",0},
 {"/pong/etc/native-apps-001.skel",0},
 {"/pong/etc/native-apps-002.skel",0},
 {"/pong/etc/native-apps-bad.skel",0},
 {"/pong/etc/native-apps-003.skel",0},
 };
 for(unsigned i=0;i<sizeof tests/sizeof tests[0];++i) {
  int actual=seed_manifest(tests[i].path)==0;
  if(actual!=tests[i].success){fprintf(stderr,"case %u result %d expected %d\n",i,actual,tests[i].success);return 1;}
 }
 content("/private/var/plain","BASE");
 content("/private/var/staged_system_apps/Sample.app/new","PAYLOAD");
 content("/private/var/staged_system_apps/Sample.app/kept","ORIGINAL");
 assert(!present("/private/var/staged_system_apps/Sample.app/foreign"));
 assert(!present("/private/var/staged_system_apps/Sample.app/other"));
 assert(!present("/private/var/staged_system_apps/Sample.app/link"));
 assert(!present("/private/var/escape"));
 puts("PASS: actual parser, seven manifest cases, O_EXCL retention, O_NOFOLLOW refusal and copied bytes");
 return 0;
}
'''


def fixture(path):
    for folder in ('pong/etc', 'pong/var-seed', 'pong/native-staged-apps/Sample.app',
                   'private/var/staged_system_apps/Sample.app'):
        (path / folder).mkdir(parents=True, exist_ok=True)
    for relative, text in (
        ('pong/var-seed/plain', 'BASE'), ('pong/native-staged-apps/Sample.app/new', 'PAYLOAD'),
        ('pong/native-staged-apps/Sample.app/kept', 'NEW'),
        ('pong/native-staged-apps/Sample.app/foreign', 'FORBIDDEN'),
        ('private/var/staged_system_apps/Sample.app/kept', 'ORIGINAL')):
        (path / relative).write_text(text)
    (path / 'pong/native-staged-apps/Sample.app/link').symlink_to('new')
    app = '/private/var/staged_system_apps/Sample.app/'
    source = '/pong/native-staged-apps/Sample.app/'
    rows = {
        'var': 'f\t644\t0\t0\t/private/var/plain\t/pong/var-seed/plain\n',
        'native-apps-000': ''.join('f\t644\t0\t0\t' + app + name + '\t' + source + name + '\n' for name in ('new', 'kept')),
        'foreign': 'f\t644\t0\t0\t' + app + 'foreign\t' + source + 'foreign\n',
        'native-apps-001': 'f\t644\t0\t0\t' + app + 'other\t' + source + 'new\n',
        'native-apps-002': 'f\t644\t0\t0\t/private/var/../escape\t' + source + 'new\n',
        'native-apps-bad': 'f\t644\t0\t0\t' + app + 'new\t' + source + 'new\n',
        'native-apps-003': 'f\t644\t0\t0\t' + app + 'link\t' + source + 'link\n',
    }
    for name, text in rows.items():
        file = path / 'pong/etc' / (name + '.skel')
        file.write_text(text)
        file.chmod(0o644)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('bootstrap-source', 'control-source', 'out'):
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--catalog', type=Path, help='Also replay all actual prepared native application resources')
    args = parser.parse_args()
    if os.geteuid():
        parser.error('Actual manifest ownership replay requires root')
    if args.out.exists():
        parser.error('Retain previous evidence; choose a new output')
    args.out.mkdir(parents=True)
    header = Path(__file__).resolve().parents[3] / 'include/zizhan/Native18AppSeed.h'
    results = []
    for label, source, expected in (('adapted', args.bootstrap_source, 0), ('original-control', args.control_source, 1)):
        directory = args.out / label
        directory.mkdir()
        original = source.read_text()
        body = original[original.index('static int owned('):original.index('static void prepare_data(')]
        unit = directory / 'actual-parser.c'
        unit.write_text(PREFIX + body + SUFFIX)
        binary = directory / 'actual-parser'
        subprocess.run(['xcrun', 'clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g', '-I' + str(header.parent),
                        str(unit), '-o', str(binary)], check=True)
        fixture(directory / 'fixture')
        result = subprocess.run([str(binary.resolve()), str((directory / 'fixture').resolve())], capture_output=True, text=True)
        (directory / 'run.log').write_text(result.stdout + result.stderr)
        results.append({'name': label, 'returncode': result.returncode, 'expected': expected, 'pass': result.returncode == expected})
    if args.catalog:
        directory = args.out / 'actual-catalog'
        directory.mkdir()
        original = args.bootstrap_source.read_text()
        body = original[original.index('static int owned('):original.index('static void prepare_data(')]
        unit = directory / 'actual-parser.c'
        unit.write_text(PREFIX + body + r'''
int main(int argc,char **argv) {
 assert(argc==2);fixture=argv[1];char count[16];
 assert(text("/pong/etc/native-apps.count",count,sizeof count)>0);
 unsigned parts=decimal(count,10);assert(parts>0&&parts<=128);
 for(unsigned i=0;i<parts;i++) {
  char path[128];snprintf(path,sizeof path,"/pong/etc/native-apps-%03u.skel",i);
  if(seed_manifest(path)){fprintf(stderr,"actual catalog seed failed: %s\n",path);return 1;}
 }
 return 0;
}
''')
        binary = directory / 'actual-parser'
        subprocess.run(['xcrun', 'clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g', '-I' + str(header.parent),
                        str(unit), '-o', str(binary)], check=True)
        virtual_root = directory / 'fixture'
        (virtual_root / 'private/var').mkdir(parents=True)
        (virtual_root / 'pong').symlink_to((args.catalog / 'overlay/pong').resolve())
        result = subprocess.run([str(binary.resolve()), str(virtual_root.resolve())], capture_output=True, text=True)
        (directory / 'run.log').write_text(result.stdout + result.stderr)
        matches, total = 0, 0
        rows = json.loads((args.catalog / 'files.json').read_text())
        for row in rows:
            prefix = 'pong/native-staged-apps/'
            if not row['path'].startswith(prefix):
                continue
            target = virtual_root / 'private/var/staged_system_apps' / row['path'][len(prefix):]
            if 'symlink' in row:
                valid = target.is_symlink() and os.readlink(target) == row['symlink']
            elif row.get('directory'):
                valid = target.is_dir() and not target.is_symlink() and target.stat().st_mode & 0o777 == row['mode']
            else:
                valid = target.is_file() and not target.is_symlink() and hashlib.sha256(target.read_bytes()).hexdigest() == row['sha256'] and target.stat().st_mode & 0o777 == row['mode']
                if valid:
                    total += target.stat().st_size
            valid = valid and target.lstat().st_uid == 0 and target.lstat().st_gid == 0
            if not valid:
                raise SystemExit('Actual native resource copy differs: ' + row['path'])
            matches += 1
        results.append({'name': 'actual-catalog', 'returncode': result.returncode, 'expected': 0,
                        'pass': result.returncode == 0, 'verified_entries': matches, 'verified_bytes': total,
                        'scope': 'actual parser copies into a temporary host directory; no guest APFS/migration/device qualification'})
    (args.out / 'results.json').write_text(json.dumps({'result': 'PASS' if all(row['pass'] for row in results) else 'FAIL',
                                                      'results': results, 'device_access': False}, indent=2) + '\n')
    if not all(row['pass'] for row in results):
        raise SystemExit('Actual seed-parser replay failed; inspect retained logs')
    print('PASS: adapted real parser and unadapted negative control under ASan/UBSan')


if __name__ == '__main__':
    main()
