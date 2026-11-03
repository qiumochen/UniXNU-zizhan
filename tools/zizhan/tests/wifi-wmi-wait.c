/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "lx_os.h"
#include <assert.h>
static u64 clock_ms;
static int credit, attempts, transfers, refill;
u64 lx_now_ms(void) { return clock_ms; }
int lx_pump(void) { if(refill) { credit=1; refill=0; } return 0; }
void lx_idle(unsigned int ms) { clock_ms+=ms; }
static int send_cmd(void) {
 attempts++;
 if(!credit) return -EAGAIN;
 credit--; transfers++; return 0;
}
static void reset(int available) { clock_ms=0; credit=available; attempts=transfers=refill=0; }
int main(void) {
 int ret; long result;
 reset(1);
 result=wait_event_timeout(0, ({ret=send_cmd();ret!=-EAGAIN;}), 3);
 assert(!result && ret==-EAGAIN && transfers==1 && attempts==2);
 reset(1);
 result=wait_event_timeout_once(0, ({ret=send_cmd();ret!=-EAGAIN;}), 3);
 assert(result>0 && ret==0 && transfers==1 && attempts==1);
 reset(0);refill=1;
 result=wait_event_timeout_once(0, ({ret=send_cmd();ret!=-EAGAIN;}), 3);
 assert(result>0 && ret==0 && transfers==1 && attempts==2);
 reset(0);
 result=wait_event_timeout_once(0, ({ret=send_cmd();ret!=-EAGAIN;}), 3);
 assert(!result && ret==-EAGAIN && transfers==0 && clock_ms==3);
 puts("PASS: original false -EAGAIN reproduced; success sends once; credit refill and real timeout preserved");
}
