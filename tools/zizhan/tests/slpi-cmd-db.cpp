/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ZizhanCmdDbCapture.h"
static uint8_t db[0x20000];
static const char *const names[] = {"xo.lvl","lcx.lvl","lmx.lvl","CE0","SN3","SN0","SH0","MC0","ACV"};
static void w16(unsigned at, unsigned v) { db[at] = v; db[at + 1] = v >> 8; }
static void w32(unsigned at, uint32_t v) { w16(at, v); w16(at + 2, v >> 16); }
static unsigned entry(unsigned n) { return 144 + 24 * n; }
static unsigned aux(unsigned n) { return 512 + 16 * n; }
static void fixture(void) {
    memset(db, 0, sizeof db); w32(0, 1); w32(4, 0x0c0330db);
    w16(8, 3); w16(10, 0); w16(12, 512 - 144); w16(14, 3);
    w16(24, 5); w16(26, 3 * 24); w16(28, 512 - 144); w16(30, 6);
    const unsigned vcd[] = {2,1,2,0,0,0};
    for (unsigned n = 0; n < 9; ++n) {
        memcpy(db + entry(n), names[n], strlen(names[n]));
        w32(entry(n) + 16, ((n < 3 ? 3u : 5u) << 16) + 0x100 + 4 * n);
        w16(entry(n) + 22, 16 * n);
        if (n && n < 3) {
            w16(entry(n) + 20, 12);
            w16(aux(n) + 2, 64); w16(aux(n) + 4, 128);
            w16(aux(n) + 6, 384); w16(aux(n) + 8, 512);
        } else if (n >= 3) {
            w16(entry(n) + 20, 8); w32(aux(n), 1000); w16(aux(n) + 4, 16);
            db[aux(n) + 6] = vcd[n - 3];
        }
    }
}

static uint8_t first[0x20000], second[0x20000];
struct Reader {
 unsigned reads=0; bool mutate=false;
 uint32_t read32(unsigned at) {
  assert(!(at&3)&&at<sizeof db);++reads;
  uint32_t v=uint32_t(db[at])|(uint32_t(db[at+1])<<8)|(uint32_t(db[at+2])<<16)|(uint32_t(db[at+3])<<24);
  if(mutate&&reads>sizeof db/4&&at==64)v^=1;
  return v;
 }
};
int main(){
 using namespace zizhan_cmd_db;
 for(unsigned test=0;test<8;++test){
  fixture();Reader reader;Receipt r;uint64_t clock=0;
  auto now=[&](){clock+=test==4?1000000:0;return clock;};
  unsigned enabled=test==0?0:test==1?2:1;
  if(test==3)reader.mutate=true;
  if(test==5)db[4]=0;
  uint8_t *a=test==6?nullptr:first;uint8_t *b=test==7?first:second;
  Result result=capture(enabled,reader,now,a,b,Bytes,r);
  if(test<2){assert(result==Off&&!reader.reads&&!r.copies);}
  else if(test==2){assert(result==Ok&&r.plan.count==9&&r.copies==2&&r.words==65536&&reader.reads==65536);assert(!memcmp(first,db,Bytes));}
  else if(test==3)assert(result==Changed&&r.words==65536&&!r.plan.count);
  else if(test==4)assert(result==Deadline&&reader.reads<65536&&!r.plan.count);
  else if(test==5)assert(result==Invalid&&r.parser_result&&!r.plan.count);
  else assert(result==Argument&&!reader.reads);
 }
 fixture();Reader reader;Receipt r;assert(capture(1,reader,[]{return 0ull;},first,second,Bytes,r)==Ok&&reader.reads==65536);
 puts("PASS production cmd-db capture:two stable bounded readonly copies,actual parser,changed/invalid/deadline/frozen/off/arguments;no votes");
}
