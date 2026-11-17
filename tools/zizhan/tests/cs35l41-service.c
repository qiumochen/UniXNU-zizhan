/* Host-only service Ready/watermark/underrun gate; synthetic replies only. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ZizhanAudioService.h"
static unsigned reply(ZizhanWriteState *s,uint8_t out[512]){
 uint8_t body[32]={0};zizhan_write_put32(body,(uint32_t)s->address);
 zizhan_write_put32(body+4,(uint32_t)(s->address>>32));zizhan_write_put32(body+8,s->handle);
 GprPacket p={0};p.src_domain=2;p.dst_domain=3;p.src_port=ZizhanPcmShmem;p.dst_port=ZizhanPcmPort;
 p.token=s->token;p.opcode=AR_OP_WRITE_DONE;return gpr_encode(out,512,&p,body,32);
}
static ZizhanAudioControl control(void){
 ZizhanAudioControl c={0};c.magic=ZIZHAN_AUDIO_MAGIC;c.version=ZIZHAN_AUDIO_VERSION;
 c.rate=ZIZHAN_AUDIO_RATE;c.out_channels=ZIZHAN_AUDIO_OUT_CHANNELS;c.in_channels=ZIZHAN_AUDIO_IN_CHANNELS;
 c.period_frames=ZIZHAN_AUDIO_PERIOD_FRAMES;c.ring_frames=ZIZHAN_AUDIO_RING_FRAMES;
 c.lead_frames=ZIZHAN_AUDIO_LEAD_FRAMES;c.ticks_per_frame_q32=1;return c;
}
int main(void){
 static int16_t ring[ZIZHAN_AUDIO_RING_FRAMES*2];uint8_t slot[ZizhanRingPcmBytes],tx[512],rx[512];
 ZizhanAudioControl c=control();ZizhanAudioService s={0},bad={0};
 for(unsigned i=0;i<ZIZHAN_AUDIO_RING_FRAMES;++i){ring[2*i]=(int16_t)i;ring[2*i+1]=(int16_t)-i;}
 c.magic=0;assert(!zizhan_service_init(&bad,&c,0x1f0000000ull,7,2,0,100));c=control();
 assert(zizhan_service_init(&s,&c,0x1f0000000ull,7,2,0,100));
 assert(!zizhan_service_publish_ready(&s)&&!(c.state&ZizhanAudioOutputReady));
 assert(!zizhan_service_graph_started(&s));
 assert(zizhan_service_map(&s,1)&&zizhan_service_graph_started(&s));
 assert(!zizhan_service_publish_ready(&s));
 zizhan_audio_publish_written(&c,479);assert(!zizhan_service_publish_ready(&s));
 zizhan_audio_publish_written(&c,480);assert(zizhan_service_publish_ready(&s)&&(c.state&ZizhanAudioOutputReady));
 assert(zizhan_service_stage(&s,ring,0,slot)&&zizhan_service_commit(&s,0,200,tx)==68);
 unsigned n=reply(&s.stream.batch.write,rx);assert(zizhan_service_receive(&s,rx,n,201)==1);
 assert(s.stream.next_frame==480);
 assert(!zizhan_service_publish_ready(&s));
 zizhan_audio_publish_written(&c,960);
 assert(zizhan_service_stage(&s,ring,1,slot)&&zizhan_service_commit(&s,1,300,tx)==68);
 zizhan_service_stop(&s);assert(!(c.state&(ZizhanAudioOutputReady|ZizhanAudioOutputRunning))&&s.phase==ZizhanServiceStopping);
 n=reply(&s.stream.batch.write,rx);assert(zizhan_service_receive(&s,rx,n,301)==1);
 assert(zizhan_service_done(&s));
 ZizhanAudioService u={0};ZizhanAudioControl d=control();assert(zizhan_service_init(&u,&d,0x1f0000000ull,8,2,0,100));
 assert(zizhan_service_map(&u,1)&&zizhan_service_graph_started(&u));
 zizhan_audio_publish_written(&d,480);assert(zizhan_service_publish_ready(&u));
 assert(zizhan_service_stage(&u,ring,0,slot)&&zizhan_service_commit(&u,0,200,tx)==68);
 zizhan_audio_publish_written(&d,480);
 assert(!zizhan_service_stage(&u,ring,1,slot));
 d.written_seq=1;assert(!zizhan_service_stage(&u,ring,0,slot)&&u.phase==ZizhanServiceFailed);
 puts("PASS zizhan service gate: Ready only after control/map/graph/producer period, stable watermark, underrun and stop/drain; no hardware");
}
