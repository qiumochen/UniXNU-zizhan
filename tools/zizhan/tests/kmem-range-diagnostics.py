#!/usr/bin/env python3
"""Execute the patch's actual read-only range-gap helper over bounded maps."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
PATCH = ROOT / 'patches/xnu-ipados18/0022-zizhan-kmem-range-diagnostics.patch'

PREFIX = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef uint64_t vm_map_offset_t,vm_map_size_t;
typedef struct entry {uint64_t vme_start,vme_end;struct entry *vme_next;} *vm_map_entry_t;
typedef struct map {struct entry sentinel;struct entry *first;unsigned locked;} *vm_map_t;
static void vm_map_lock_read(vm_map_t map){assert(!map->locked);map->locked=1;}
static void vm_map_unlock_read(vm_map_t map){assert(map->locked);map->locked=0;}
#define vm_map_first_entry(map) ((map)->first)
#define vm_map_to_entry(map) (&(map)->sentinel)
'''

SUFFIX = r'''
static void check(struct map *map,uint64_t lo,uint64_t hi,uint64_t free,uint64_t largest,unsigned entries){
 uint64_t f=99,l=99;unsigned n=99;
 zizhan_vm_map_range_sizes(map,lo,hi,&f,&l,&n);
 assert(f==free&&l==largest&&n==entries);if(map)assert(!map->locked);
}
int main(void){
 struct map map={0};map.first=&map.sentinel;
 check(&map,100,200,100,100,0);check(&map,200,100,0,0,0);check(NULL,100,200,0,0,0);
 struct entry nodes[]={{50,120,0},{140,160,0},{190,250,0},{300,400,0}};
 for(unsigned i=0;i<4;i++)nodes[i].vme_next=i<3?&nodes[i+1]:&map.sentinel;map.first=nodes;
 check(&map,100,200,50,30,3);check(&map,120,140,20,20,0);
 check(&map,120,190,50,30,1);check(&map,0,500,250,100,4);
 const uint64_t high=0xfffffff000000000ULL;
 for(unsigned i=0;i<4;i++){nodes[i].vme_start+=high;nodes[i].vme_end+=high;}
 check(&map,high+100,high+200,50,30,3);
 puts("PASS actual selected-range helper: clipped entries, sparse gaps, empty/invalid and high-address maps");
 return 0;
}
'''


def main():
    added = '\n'.join(line[1:] for line in PATCH.read_text().splitlines()
                      if line.startswith('+') and not line.startswith('+++')) + '\n'
    match = re.search(r'void\nzizhan_vm_map_range_sizes\([\s\S]+?\n}\n', added)
    if not match:
        raise RuntimeError('Actual patch range helper changed; review test extraction')
    with tempfile.TemporaryDirectory(prefix='zizhan-kmem-range-') as temporary:
        source = Path(temporary) / 'range.c'
        binary = Path(temporary) / 'range'
        source.write_text(PREFIX + match[0] + SUFFIX)
        subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == '__main__':
    main()
