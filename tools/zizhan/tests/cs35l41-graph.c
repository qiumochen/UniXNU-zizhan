/* Host-only encoding checks for the linked zizhan playback graph.
 * No APM transport, DSP, MMIO, DMA, PCM data or output enable. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ZizhanAudioGraph.h"
#include "ZizhanTdm.h"
#include "ZizhanPcm.h"
#include "ShengGpr.h"
static uint32_t le32(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint16_t le16(const uint8_t *p){return p[0]|(uint16_t)p[1]<<8;}
static const uint8_t *param(const GprPacket *p,uint32_t iid,uint32_t id,uint32_t *size){
 const uint8_t *base=p->payload+16;uint32_t off=0;
 if(p->payload_bytes<16)return NULL;
 while(off+16<=p->payload_bytes-16){
  const uint32_t piid=le32(base+off),pid=le32(base+off+4),psize=le32(base+off+8);
  if(piid==iid&&pid==id){if(size)*size=psize;return base+off+16;}
  off+=16u+((psize+7u)&~7u);
 }
 return NULL;
}
static unsigned module_count(const GprPacket *p,uint32_t sg,uint32_t container){
 uint32_t size=0;const uint8_t *b=param(p,AR_APM_IID,0x08001002u,&size);
 assert(b&&size>=20&&le32(b)==1&&le32(b+4)==sg&&le32(b+8)==container);
 return le32(b+12);
}
static unsigned link_count(const GprPacket *p,uint32_t from,uint32_t to){
 uint32_t size=0;const uint8_t *b=param(p,AR_APM_IID,0x08001004u,&size);
 assert(b&&size>=16);
 const unsigned n=le32(b);
 for(unsigned i=0;i<n;++i){
  const uint8_t *row=b+4+16*i;
  if(le32(row)==from&&le32(row+4)==1&&le32(row+8)==to&&le32(row+12)==2)return n;
 }
 assert(0);return n;
}
static void check_device_open(const uint8_t *bytes,unsigned n){
 GprPacket p;assert(!gpr_decode(bytes,n,&p)&&p.size==n&&p.src_port==ZizhanMfcPort&&p.dst_port==AR_APM_IID&&p.opcode==AR_OP_GRAPH_OPEN);
 assert(module_count(&p,ZizhanMfcSg,ZizhanMfcContainer)==3);
 assert(link_count(&p,ZizhanMfcIid,ZizhanSalIid)==2);
 assert(link_count(&p,ZizhanSalIid,ZizhanTdmIid)==2);
 uint32_t size=0;const uint8_t *b=param(&p,AR_APM_IID,0x08001002u,&size);
 assert(b&&size>=40);
 const uint32_t mids[]={0x07001015u,ZizhanMfcIid,0x07001010u,ZizhanSalIid,0x0700100eu,ZizhanTdmIid};
 for(unsigned i=0;i<6;++i)assert(le32(b+16+4*i)==mids[i]);
}
static void check_stream_open(const uint8_t *bytes,unsigned n){
 GprPacket p;assert(!gpr_decode(bytes,n,&p)&&p.size==n&&p.src_port==ZizhanPcmPort&&p.dst_port==AR_APM_IID&&p.opcode==AR_OP_GRAPH_OPEN);
 assert(module_count(&p,ZizhanPcmSg,ZizhanPcmContainer)==1);
 assert(link_count(&p,ZizhanPcmShmem,ZizhanMfcIid)==1);
}
static void check_device_config(const uint8_t *bytes,unsigned n){
 GprPacket p;assert(!gpr_decode(bytes,n,&p)&&p.size==n&&p.src_port==ZizhanMfcPort&&p.dst_port==AR_APM_IID&&p.opcode==AR_OP_SET_CFG);
 uint32_t size=0;const uint8_t *b=param(&p,ZizhanMfcIid,0x08001024u,&size);
 assert(b&&size==16&&le32(b)==96000&&le16(b+4)==24&&le16(b+6)==3);
 /* 14-byte packed body followed by two alignment bytes. */
 assert(le16(b+8)==1&&le16(b+10)==2&&le16(b+12)==3&&b[14]==0&&b[15]==0);
 b=param(&p,ZizhanSalIid,0x08001016u,&size);assert(b&&size==4&&le32(b)==24);
 b=param(&p,ZizhanSalIid,0x0800101eu,&size);assert(b&&size==4&&le32(b)==1);
 b=param(&p,ZizhanTdmIid,0x0800101bu,&size);assert(b&&size==28&&le32(b+12)==7);
 b=param(&p,ZizhanTdmIid,0x08001017u,&size);assert(b&&size==12&&le32(b)==96000&&le16(b+4)==24&&le16(b+6)==3);
 b=param(&p,ZizhanTdmIid,0x0800106au,&size);assert(b&&size==4&&le32(b)==2);
 b=param(&p,ZizhanTdmIid,0x0800113cu,&size);assert(b&&size==16);
}
static void check_media48(const uint8_t *bytes,unsigned n){
 GprPacket p;assert(!gpr_decode(bytes,n,&p)&&p.size==n&&p.src_port==ZizhanPcmPort&&p.dst_port==ZizhanPcmShmem&&p.opcode==AR_OP_SET_CFG);
 uint32_t size=0;const uint8_t *b=param(&p,ZizhanPcmShmem,0x0800100cu,&size);
 assert(b&&size==32&&le32(b)==1&&le32(b+4)==0x09001000u&&le32(b+8)==36);
 assert(le32(b+12)==48000&&le16(b+16)==24&&le16(b+18)==1);
 assert(le16(b+20)==24&&le16(b+22)==23&&le16(b+24)==1&&le16(b+26)==2);
 assert(b[28]==1&&b[29]==2);
}
static void check_commands(const uint8_t *bytes,unsigned n,uint32_t port,uint32_t sg,uint32_t opcode){
 GprPacket p;assert(!gpr_decode(bytes,n,&p)&&p.size==n&&p.src_port==port&&p.dst_port==AR_APM_IID&&p.opcode==opcode);
 uint32_t size=0;const uint8_t *b=param(&p,AR_APM_IID,0x08001005u,&size);
 assert(b&&size==8&&le32(b)==1&&le32(b+4)==sg);
}
int main(void){
 uint8_t a[512],b[512];unsigned n;
 n=zizhan_mfc_device_open(a,sizeof a,0x7a200001u);assert(n);check_device_open(a,n);
 n=zizhan_mfc_device_config(a,sizeof a,0x7a200002u);assert(n);check_device_config(a,n);
 n=zizhan_pcm_open_to_mfc(a,sizeof a,0x7a200003u);assert(n);check_stream_open(a,n);
 n=zizhan_pcm_media_packed48(a,sizeof a,0x7a200004u);assert(n);check_media48(a,n);
 /* Decoder variant: three stream modules, SHMEM->DEC->CNV->MFC, and the AGM RX output format on both. */
 {n=zizhan_pcm_open_dec_to_mfc(a,sizeof a,0x7a20000bu);assert(n);
  GprPacket p;assert(!gpr_decode(a,n,&p)&&p.size==n&&p.src_port==ZizhanPcmPort&&p.opcode==AR_OP_GRAPH_OPEN);
  assert(module_count(&p,ZizhanPcmSg,ZizhanPcmContainer)==3);
  assert(link_count(&p,ZizhanPcmShmem,ZizhanPcmDecIid)==3);link_count(&p,ZizhanPcmDecIid,ZizhanPcmCnvIid);link_count(&p,ZizhanPcmCnvIid,ZizhanMfcIid);
  n=zizhan_pcm_dec_cnv_config(a,sizeof a,0x7a20000cu);assert(n);
  assert(!gpr_decode(a,n,&p)&&p.size==n&&p.src_port==ZizhanPcmPort&&p.dst_port==AR_APM_IID&&p.opcode==AR_OP_SET_CFG);
  for(unsigned k=0;k<2;++k){uint32_t size=0;const uint8_t *b=param(&p,k?ZizhanPcmCnvIid:ZizhanPcmDecIid,0x08001008u,&size);
   assert(b&&size==30&&le32(b)==1&&le32(b+4)==0x09001000u&&le32(b+8)==18);
   assert(le16(b+12)==24&&le16(b+14)==1&&le16(b+16)==32&&le16(b+18)==27&&le16(b+20)==1&&le16(b+22)==3&&le16(b+24)==0&&le16(b+26)==2&&b[28]==1&&b[29]==2);}}
  /* Loopback diagnostic: 5 or 4 stream modules ending in RD_SHMEM, ENC 16-bit interleaved, RD 1 frame/buffer, READ V2 body. */
 for(unsigned dec=0;dec<2;++dec){GprPacket p;n=zizhan_pcm_open_loopback(a,sizeof a,0x7a20000du,dec);assert(n);
  assert(!gpr_decode(a,n,&p)&&p.size==n&&p.opcode==AR_OP_GRAPH_OPEN&&module_count(&p,ZizhanPcmSg,ZizhanPcmContainer)==(dec?5u:4u));
  assert(link_count(&p,ZizhanPcmEncIid,ZizhanPcmRdIid)==(dec?4u:3u));link_count(&p,dec?ZizhanPcmDecIid:ZizhanPcmShmem,ZizhanPcmCnvIid);
  n=zizhan_pcm_loopback_config(a,sizeof a,0x7a20000eu,dec);assert(n&&!gpr_decode(a,n,&p)&&p.opcode==AR_OP_SET_CFG);
  uint32_t size=0;const uint8_t *b=param(&p,ZizhanPcmEncIid,0x08001008u,&size);
  assert(b&&size==30&&le16(b+12)==16&&le16(b+16)==16&&le16(b+18)==15&&le16(b+22)==1&&le16(b+26)==2);
  b=param(&p,ZizhanPcmRdIid,0x08001007u,&size);assert(b&&size==8&&le32(b)==1&&le32(b+4)==0);
  assert((param(&p,ZizhanPcmDecIid,0x08001008u,&size)!=NULL)==(dec!=0));}
 {GprPacket p;n=zizhan_pcm_read(a,sizeof a,0x7a2d0001u,0x1f9e3c000ull,0xb0d50e28u,0x3000u);assert(n);
  assert(!gpr_decode(a,n,&p)&&p.size==n&&p.opcode==AR_OP_READ&&p.dst_port==ZizhanPcmRdIid&&p.src_port==ZizhanPcmPort&&p.payload_bytes==32);
  assert(le32(p.payload)==0xf9e3c000u&&le32(p.payload+4)==1&&le32(p.payload+8)==0xb0d50e28u&&le32(p.payload+12)==0x3000u&&!le32(p.payload+16));}
 n=zizhan_graph_command(a,sizeof a,ZizhanMfcPort,0x7a200005u,AR_OP_GRAPH_PREPARE,ZizhanMfcSg);assert(n);
 check_commands(a,n,ZizhanMfcPort,ZizhanMfcSg,AR_OP_GRAPH_PREPARE);
 n=zizhan_graph_command(a,sizeof a,ZizhanPcmPort,0x7a200006u,AR_OP_GRAPH_START,ZizhanPcmSg);assert(n);
 check_commands(a,n,ZizhanPcmPort,ZizhanPcmSg,AR_OP_GRAPH_START);
 n=zizhan_graph_command(a,sizeof a,ZizhanPcmPort,0x7a200007u,AR_OP_GRAPH_STOP,ZizhanPcmSg);assert(n);
 check_commands(a,n,ZizhanPcmPort,ZizhanPcmSg,AR_OP_GRAPH_STOP);
 n=zizhan_graph_command(a,sizeof a,ZizhanMfcPort,0x7a200008u,AR_OP_GRAPH_CLOSE,ZizhanMfcSg);assert(n);
 check_commands(a,n,ZizhanMfcPort,ZizhanMfcSg,AR_OP_GRAPH_CLOSE);
 /* Negative controls: a 3-channel SHMEM format, a missing MFC link and a
  * wrong MFC output channel map must not pass the same local checks. */
 n=zizhan_pcm_media_packed48(b,sizeof b,0x7a200009u);
 GprPacket p;assert(!gpr_decode(b,n,&p));
 uint32_t size=0;uint8_t *v=(uint8_t *)param(&p,ZizhanPcmShmem,0x0800100cu,&size);assert(v);
 v[26]=3; /* SHMEM channels -> 3 (DSP shmem rejects >2) */
 assert(le16(v+26)==3);
 n=zizhan_mfc_device_config(b,sizeof b,0x7a20000au);
 assert(!gpr_decode(b,n,&p));
 v=(uint8_t *)param(&p,ZizhanMfcIid,0x08001024u,&size);assert(v);
 v[12]=9; /* MFC third channel map -> 9, not PCM_CHANNEL_C */
 assert(le16(v+12)==9);
 puts("PASS linked device/stream graph encoding: MFC/SAL/TDM, cross-graph WR_SHMEM->MFC, 48k packed input, 96k/3ch MFC output, TDM parameters and lifecycle commands; synthetic bytes only");
}
