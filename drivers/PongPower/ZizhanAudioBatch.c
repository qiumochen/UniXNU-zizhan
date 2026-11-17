/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "ZizhanAudioBatch.h"
#define BATCH_MAGIC 0x5a424154u
static void fail(ZizhanAudioBatch *s,unsigned reason){
 s->reason=reason;s->uncertain=s->write.uncertain;s->phase=ZizhanBatchFailed;
}
int zizhan_batch_init(ZizhanAudioBatch *s,uint64_t base,uint32_t handle,unsigned bytes,unsigned count,uint64_t now){
 return zizhan_batch_init_ex(s,base,handle,bytes,ZizhanWriteBytes,960,count,now);
}
int zizhan_batch_init_ex(ZizhanAudioBatch *s,uint64_t base,uint32_t handle,unsigned extent,
                         unsigned bytes,unsigned frames,unsigned count,uint64_t now){
 if(!s||s->magic){return 0;}memset(s,0,sizeof *s);s->magic=BATCH_MAGIC;s->phase=ZizhanBatchFailed;
 if(!handle||extent!=65536||!bytes||bytes>extent||(bytes&3u)||!frames||bytes%frames||
    !count||count>4||(base>>32)!=1||!(uint32_t)base||
    (base&4095)||(uint32_t)base>0xffff0000u){s->reason=1;return 0;}
 const unsigned stride=0x3000u;
 if((count-1)*stride+bytes>extent||
    (uint64_t)(uint32_t)base+(count-1)*stride>0xffff0000u||
    (uint64_t)(uint32_t)base+(count-1)*stride+bytes>0x100000000ull){s->reason=1;return 0;}
 s->base=base;s->handle=handle;s->total=count;s->began_us=now;
 s->bytes=bytes;s->frames=frames;s->stride=stride;s->phase=ZizhanBatchReady;return 1;
}
int zizhan_batch_prepared(ZizhanAudioBatch *s,unsigned slot,unsigned bytes){
 if(!s||s->magic!=BATCH_MAGIC||s->phase!=ZizhanBatchReady||s->stopping||
    slot!=s->submitted||slot>=s->total||bytes!=s->bytes)return 0;
 s->prepared|=1u<<slot;return 1;
}
void zizhan_batch_tick(ZizhanAudioBatch *s,uint64_t now){
 if(!s||s->magic!=BATCH_MAGIC||s->phase==ZizhanBatchDone||s->phase==ZizhanBatchFailed)return;
 if(now<s->began_us||now-s->began_us>=500000||
    (s->phase==ZizhanBatchWaiting&&(now<s->write.began_us||now-s->write.began_us>=100000))){
  if(s->phase==ZizhanBatchWaiting){s->write.uncertain=1;}fail(s,4);
 }
}
unsigned zizhan_batch_next(ZizhanAudioBatch *s,uint64_t now,uint8_t out[512]){
 if(!s||!out||s->magic!=BATCH_MAGIC){return 0;}zizhan_batch_tick(s,now);
 if(s->phase!=ZizhanBatchReady||s->stopping||s->submitted>=s->total||!(s->prepared&(1u<<s->submitted)))return 0;
 memset(&s->write,0,sizeof s->write);s->write.address=s->base+s->stride*s->submitted;
 s->write.handle=s->handle;s->write.sequence=s->submitted;s->write.bytes=s->bytes;
 const unsigned n=zizhan_write_next(&s->write,now,out);
 if(!n){fail(s,1);return 0;}
 ++s->submitted;s->uncertain=1;s->phase=ZizhanBatchWaiting;return n;
}
int zizhan_batch_receive(ZizhanAudioBatch *s,const uint8_t *data,unsigned bytes,uint64_t now){
 if(!s||!data||s->magic!=BATCH_MAGIC){return 0;}zizhan_batch_tick(s,now);
 if(s->phase!=ZizhanBatchWaiting)return 0;
 const int result=zizhan_write_receive(&s->write,data,bytes);
 if(result<0){fail(s,s->write.reason);return -1;}
 if(result==1){++s->completed;s->confirmed_frames+=s->frames;s->uncertain=0;
  s->phase=(s->stopping||s->completed==s->total)?ZizhanBatchDone:ZizhanBatchReady;}
 return result;
}
void zizhan_batch_stop(ZizhanAudioBatch *s){
 if(!s||s->magic!=BATCH_MAGIC){return;}s->stopping=1;
 if(s->phase==ZizhanBatchReady)s->phase=ZizhanBatchDone;
}
void zizhan_batch_send_failed(ZizhanAudioBatch *s){
 if(s&&s->magic==BATCH_MAGIC&&s->phase==ZizhanBatchWaiting){s->write.uncertain=1;fail(s,5);}
}
int zizhan_batch_buffers_returned(const ZizhanAudioBatch *s){
 return s&&s->magic==BATCH_MAGIC&&s->phase==ZizhanBatchDone&&
        !s->uncertain&&s->submitted==s->completed;
}
