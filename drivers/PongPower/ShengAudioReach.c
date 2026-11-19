/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "ShengAudioReach.h"
#include "ShengGpr.h"
#include "ZizhanTdm.h"
#include "ZizhanPcm.h"
#include "ZizhanAudioGraph.h"
#include "ZizhanAudioMemory.h"

#define PARAM_HEADER 16u
#define CMD_HEADER 16u
#define PARAM_SG_CONFIG 0x08001001u
#define PARAM_CONTAINER_CONFIG 0x08001000u
#define PARAM_MODULE_LIST 0x08001002u
#define PARAM_MODULE_PROP 0x08001003u
#define PARAM_MODULE_CONN 0x08001004u
#define PARAM_SG_LIST 0x08001005u
#define PROP_SG_PERF 0x0800100Eu
#define PROP_SG_DIRECTION 0x0800100Fu
#define PROP_SG_SCENARIO 0x08001010u
#define PROP_CONT_CAPS 0x08001011u
#define PROP_CONT_POS 0x08001012u
#define PROP_CONT_STACK 0x08001013u
#define PROP_CONT_DOMAIN 0x08001014u
#define PROP_PORT_INFO 0x08001015u
#define PARAM_I2S_INTF 0x08001019u
#define PARAM_HW_EP_MF 0x08001017u
#define PARAM_FRAME_SIZE_FACTOR 0x08001018u
#define PARAM_PCM_OUTPUT 0x08001008u
#define PARAM_MEDIA_FORMAT 0x0800100Cu
#define PARAM_CODEC_DMA_INTF 0x08001063u
#define PARAM_HW_EP_POWER_MODE 0x08001176u
#define CONTAINER_GC 0x0B001001u
#define MEDIA_PCM 0x09001000u
#define PERF_LOW_LATENCY 2u
#define DIR_TX 1u
#define DIR_RX 2u
#define SCENARIO_PLAYBACK 1u
#define SCENARIO_RECORD 2u
#define POS_STREAM 1u
#define POS_GLOBAL_DEV 4u
#define DOMAIN_ADSP 2u
#define STACK 8192u
#define OUT_PORT 1u
#define IN_PORT 2u
#define LPAIF 0u /* MI2S-LPAIF-RX-TERTIARY: LPAIF=0; RX is direction, not LPAIF_WSA=2 */
#define LPAIF_RXTX 1u
#define I2S_TERTIARY 2u /* tertiary interface, actual Pong topology */
#define I2S_SD0 1u /* SD0, stereo TFA9873 slots */
#define WS_INTERNAL 1u
#define CODEC_TX3 4u
#define INTERLEAVED 1u
#define DEINTERLEAVED_UNPACKED 3u

typedef struct {uint8_t *p; uint32_t n, cap; int bad;} W;

static void put8(W *w, uint32_t v) {if (w->n + 1 > w->cap) {w->bad = 1; return;} w->p[w->n++] = (uint8_t)v;}
static void put16(W *w, uint32_t v) {put8(w, v); put8(w, v >> 8);}
static void put32(W *w, uint32_t v) {put16(w, v); put16(w, v >> 16);}
static void pad_to(W *w, uint32_t end) {while (!w->bad && w->n < end) put8(w, 0);}
static uint32_t align8(uint32_t v) {return (v + 7u) & ~7u;}
static uint32_t le32(const uint8_t *p) {return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;}
static void set32(W *w, uint32_t at, uint32_t v) {if (at + 4 <= w->n) {w->p[at] = (uint8_t)v; w->p[at + 1] = (uint8_t)(v >> 8); w->p[at + 2] = (uint8_t)(v >> 16); w->p[at + 3] = (uint8_t)(v >> 24);}}

/* GPR header with the packet size patched in at the end (finish). */
static void begin(W *w, uint8_t *out, uint32_t capacity, uint32_t port, uint32_t dst, uint32_t token, uint32_t opcode)
{
    w->p = out; w->n = 0; w->cap = capacity < GPR_MAX_PACKET ? capacity : GPR_MAX_PACKET; w->bad = 0;
    put32(w, 0); put32(w, GPR_DOMAIN_ADSP | GPR_DOMAIN_APPS << 8); put32(w, port); put32(w, dst); put32(w, token); put32(w, opcode);
}
static uint32_t finish(W *w)
{
    if (w->bad) return 0;
    set32(w, 0, (GPR_VERSION & 0xfu) | (GPR_HEADER_BYTES / 4) << 4 | w->n << 8);
    return w->n;
}
/* The APM command header: in-band payload of the given size follows. */
static void cmd_header(W *w, uint32_t payload) {put32(w, 0); put32(w, 0); put32(w, 0); put32(w, payload);}
static uint32_t cmd_payload_start(W *w) {uint32_t at = w->n; cmd_header(w, 0); return at;}
static void cmd_payload_end(W *w, uint32_t at) {set32(w, at + 12, w->n - at - CMD_HEADER);}
/* One parameter block: header, body written by the caller, then padding to 8 bytes; param_size counts body and padding. */
static uint32_t param_begin(W *w, uint32_t iid, uint32_t id) {uint32_t at = w->n; put32(w, iid); put32(w, id); put32(w, 0); put32(w, 0); return at;}
static void param_end(W *w, uint32_t at) {pad_to(w, at + align8(w->n - at)); set32(w, at + 8, w->n - at - PARAM_HEADER);}

typedef struct {uint32_t id, perf, direction, scenario;} Sg;
typedef struct {uint32_t id, position;} Container;
typedef struct {uint32_t module, iid, inputs, outputs;} Module;
typedef struct {uint32_t from, to;} Link;

/* One subgraph with one container (all the board topology uses). */
static uint32_t graph_open(uint8_t *out, uint32_t capacity, uint32_t port, uint32_t token, const Sg *sg, const Container *c,
                           const Module *m, unsigned modules, const Link *l, unsigned links)
{
    W w;
    begin(&w, out, capacity, port, AR_APM_IID, token, AR_OP_GRAPH_OPEN);
    const uint32_t cmd = cmd_payload_start(&w);
    uint32_t at = param_begin(&w, AR_APM_IID, PARAM_SG_CONFIG);
    put32(&w, 1);
    put32(&w, sg->id); put32(&w, 3);
    put32(&w, PROP_SG_PERF); put32(&w, 4); put32(&w, sg->perf);
    put32(&w, PROP_SG_DIRECTION); put32(&w, 4); put32(&w, sg->direction);
    put32(&w, PROP_SG_SCENARIO); put32(&w, 4); put32(&w, sg->scenario);
    param_end(&w, at);
    at = param_begin(&w, AR_APM_IID, PARAM_CONTAINER_CONFIG);
    put32(&w, 1);
    put32(&w, c->id); put32(&w, 4);
    put32(&w, PROP_CONT_CAPS); put32(&w, 8); put32(&w, 1); put32(&w, CONTAINER_GC);
    put32(&w, PROP_CONT_POS); put32(&w, 4); put32(&w, c->position);
    put32(&w, PROP_CONT_STACK); put32(&w, 4); put32(&w, STACK);
    put32(&w, PROP_CONT_DOMAIN); put32(&w, 4); put32(&w, DOMAIN_ADSP);
    param_end(&w, at);
    at = param_begin(&w, AR_APM_IID, PARAM_MODULE_LIST);
    put32(&w, 1);
    put32(&w, sg->id); put32(&w, c->id); put32(&w, modules);
    for (unsigned i = 0; i < modules; ++i) {put32(&w, m[i].module); put32(&w, m[i].iid);}
    param_end(&w, at);
    at = param_begin(&w, AR_APM_IID, PARAM_MODULE_PROP);
    put32(&w, modules);
    for (unsigned i = 0; i < modules; ++i) {
        put32(&w, m[i].iid); put32(&w, 1); put32(&w, PROP_PORT_INFO); put32(&w, 8); put32(&w, m[i].inputs); put32(&w, m[i].outputs);
    }
    param_end(&w, at);
    at = param_begin(&w, AR_APM_IID, PARAM_MODULE_CONN);
    put32(&w, links);
    for (unsigned i = 0; i < links; ++i) {put32(&w, l[i].from); put32(&w, OUT_PORT); put32(&w, l[i].to); put32(&w, IN_PORT);}
    param_end(&w, at);
    cmd_payload_end(&w, cmd);
    return finish(&w);
}

uint32_t ar_open_speaker_device(uint8_t *out, uint32_t capacity, uint32_t port, uint32_t token)
{
    static const Sg sg = {AR_SG_SPEAKER, PERF_LOW_LATENCY, DIR_TX, SCENARIO_PLAYBACK};
    static const Container c = {AR_SG_SPEAKER, POS_STREAM};
    static const Module m[] = {{AR_MOD_I2S_SINK, AR_IID_SPEAKER_I2S, 1, 0}};
    return graph_open(out, capacity, port, token, &sg, &c, m, 1, 0, 0);
}

uint32_t ar_open_playback_stream(uint8_t *out, uint32_t capacity, uint32_t port, uint32_t token)
{
    static const Sg sg = {AR_SG_PLAY, PERF_LOW_LATENCY, DIR_RX, SCENARIO_PLAYBACK};
    static const Container c = {AR_SG_PLAY, POS_STREAM};
    static const Module m[] = {{AR_MOD_WR_SHMEM, AR_IID_PLAY_SHMEM, 0, 1}, {AR_MOD_PCM_DEC, AR_IID_PLAY_DEC, 1, 1},
                               {AR_MOD_PCM_CNV, AR_IID_PLAY_CNV, 1, 1}};
    static const Link l[] = {{AR_IID_PLAY_SHMEM, AR_IID_PLAY_DEC}, {AR_IID_PLAY_DEC, AR_IID_PLAY_CNV}, {AR_IID_PLAY_CNV, AR_IID_SPEAKER_I2S}};
    return graph_open(out, capacity, port, token, &sg, &c, m, 3, l, 3);
}

uint32_t ar_open_mic_device(uint8_t *out, uint32_t capacity, uint32_t port, uint32_t token)
{
    static const Sg sg = {AR_SG_MIC, PERF_LOW_LATENCY, DIR_TX, SCENARIO_RECORD};
    static const Container c = {AR_SG_MIC, POS_GLOBAL_DEV};
    static const Module m[] = {{AR_MOD_CODEC_DMA_SOURCE, AR_IID_MIC_DMA, 0, 1}};
    return graph_open(out, capacity, port, token, &sg, &c, m, 1, 0, 0);
}

uint32_t ar_open_capture_stream(uint8_t *out, uint32_t capacity, uint32_t port, uint32_t token)
{
    static const Sg sg = {AR_SG_CAPTURE, PERF_LOW_LATENCY, DIR_TX, SCENARIO_RECORD};
    static const Container c = {AR_SG_CAPTURE, POS_GLOBAL_DEV};
    static const Module m[] = {{AR_MOD_PCM_CNV, AR_IID_CAP_CNV, 1, 1}, {AR_MOD_PCM_ENC, AR_IID_CAP_ENC, 1, 1},
                               {AR_MOD_RD_SHMEM, AR_IID_CAP_SHMEM, 1, 0}};
    static const Link l[] = {{AR_IID_MIC_DMA, AR_IID_CAP_CNV}, {AR_IID_CAP_CNV, AR_IID_CAP_ENC}, {AR_IID_CAP_ENC, AR_IID_CAP_SHMEM}};
    return graph_open(out, capacity, port, token, &sg, &c, m, 3, l, 3);
}

static void hw_ep_mf(W *w, uint32_t iid, const ArFormat *f)
{
    const uint32_t at = param_begin(w, iid, PARAM_HW_EP_MF);
    put32(w, f->rate); put16(w, f->bits); put16(w, f->channels); put32(w, 1);
    param_end(w, at);
}
static void frame_size_factor(W *w, uint32_t iid)
{
    const uint32_t at = param_begin(w, iid, PARAM_FRAME_SIZE_FACTOR);
    put32(w, 1);
    param_end(w, at);
}
static void channel_map(W *w, unsigned channels) {for (unsigned i = 0; i < channels; ++i) put8(w, i + 1);}   /* FL = 1, FR = 2 */

uint32_t ar_config_speaker_device(uint8_t *out, uint32_t capacity, uint32_t port, uint32_t token, const ArFormat *f)
{
    W w;
    begin(&w, out, capacity, port, AR_APM_IID, token, AR_OP_SET_CFG);
    const uint32_t cmd = cmd_payload_start(&w);
    const uint32_t at = param_begin(&w, AR_IID_SPEAKER_I2S, PARAM_I2S_INTF);
    put32(&w, LPAIF); put32(&w, I2S_TERTIARY); put16(&w, I2S_SD0); put16(&w, WS_INTERNAL);
    param_end(&w, at);
    hw_ep_mf(&w, AR_IID_SPEAKER_I2S, f);
    frame_size_factor(&w, AR_IID_SPEAKER_I2S);
    cmd_payload_end(&w, cmd);
    return finish(&w);
}

/* zizhan-only TDM clock qualification. Actual Forte ACDB: sink0700100e,
 * tertiary0/2/internal-sync,4x32,mask7,delay1,lane mask2,96k/24/3.
 * No PCM stream or data pad is connected by this endpoint-only graph. */
uint32_t zizhan_tdm_open(uint8_t *out,uint32_t capacity,uint32_t token){
 const Sg sg={ZizhanTdmSg,PERF_LOW_LATENCY,DIR_RX,SCENARIO_PLAYBACK};
 const Container c={ZizhanTdmContainer,POS_GLOBAL_DEV};
 const Module m={0x0700100eu,ZizhanTdmIid,1,0};
 return graph_open(out,capacity,ZizhanTdmPort,token,&sg,&c,&m,1,0,0);
}
/* Module parameters use exact body size,with separate eight-byte packet
 * alignment,as actual AGM headerstores show. Do not count pad as data. */
static void zizhan_tdm_param_end(W *w,uint32_t at){
 set32(w,at+8,w->n-at-PARAM_HEADER);pad_to(w,at+align8(w->n-at));
}
static void zizhan_tdm_params(W *w){
 uint32_t at=param_begin(w,ZizhanTdmIid,0x0800101bu);
 put32(w,0);put32(w,2);put16(w,1);put16(w,1);put32(w,7);
 put16(w,4);put16(w,32);put16(w,0);put16(w,0);put16(w,1);put16(w,0);
 zizhan_tdm_param_end(w,at);
 at=param_begin(w,ZizhanTdmIid,PARAM_HW_EP_MF);
 put32(w,96000);put16(w,24);put16(w,3);put32(w,1);zizhan_tdm_param_end(w,at);
 at=param_begin(w,ZizhanTdmIid,PARAM_FRAME_SIZE_FACTOR);put32(w,1);zizhan_tdm_param_end(w,at);
 at=param_begin(w,ZizhanTdmIid,0x0800106au);put16(w,2);put16(w,0);zizhan_tdm_param_end(w,at);
 at=param_begin(w,ZizhanTdmIid,0x0800113cu);put32(w,0);put32(w,0);put32(w,4);put32(w,0);zizhan_tdm_param_end(w,at);
}
uint32_t zizhan_tdm_config(uint8_t *out,uint32_t capacity,uint32_t token){
 W w;begin(&w,out,capacity,ZizhanTdmPort,AR_APM_IID,token,AR_OP_SET_CFG);
 const uint32_t cmd=cmd_payload_start(&w);zizhan_tdm_params(&w);
 cmd_payload_end(&w,cmd);return finish(&w);
}

/* Native-format shared-memory stream wired directly to the qualified TDM
 * sink. No decoder,conversion/resampling assumption,buffer map or data. */
uint32_t zizhan_pcm_open(uint8_t *out,uint32_t capacity,uint32_t token){
 const Sg sg={ZizhanPcmSg,PERF_LOW_LATENCY,DIR_RX,SCENARIO_PLAYBACK};
 const Container c={ZizhanPcmContainer,POS_STREAM};
 const Module m={AR_MOD_WR_SHMEM,ZizhanPcmShmem,0,1};
 const Link l={ZizhanPcmShmem,ZizhanTdmIid};
 return graph_open(out,capacity,ZizhanPcmPort,token,&sg,&c,&m,1,&l,1);
}
uint32_t zizhan_pcm_joint_command(uint8_t *out,uint32_t capacity,uint32_t token,uint32_t opcode){
 if(opcode!=AR_OP_GRAPH_STOP&&opcode!=AR_OP_GRAPH_CLOSE)return 0;
 W w;begin(&w,out,capacity,ZizhanPcmPort,AR_APM_IID,token,opcode);
 const uint32_t cmd=cmd_payload_start(&w),at=param_begin(&w,AR_APM_IID,PARAM_SG_LIST);
 put32(&w,2);put32(&w,ZizhanPcmSg);put32(&w,ZizhanTdmSg);
 zizhan_tdm_param_end(&w,at);cmd_payload_end(&w,cmd);return finish(&w);
}
uint32_t zizhan_pcm_media(uint8_t *out,uint32_t capacity,uint32_t token){
 W w;begin(&w,out,capacity,ZizhanPcmPort,ZizhanPcmShmem,token,AR_OP_SET_CFG);
 const uint32_t cmd=cmd_payload_start(&w),at=param_begin(&w,ZizhanPcmShmem,PARAM_MEDIA_FORMAT);
 // Fixed originalTDM96k/24logical/3channels. OfficialpackedPCM header:
 // sample_rate32,bit_width16,alignment8,interleaved8,bps16,q16,endian16,ch16.
 // Unpacked24 in32,MSB aligned/Q31. Exactpayload19,not paddedlength20.
 put32(&w,1);put32(&w,MEDIA_PCM);put32(&w,36);
 put32(&w,96000);put16(&w,24);put16(&w,2);
 put16(&w,32);put16(&w,31);put16(&w,1);put16(&w,3);
 put8(&w,1);put8(&w,2);put8(&w,3);
 set32(&w,at+8,align8(12u+16u+3u));pad_to(&w,at+PARAM_HEADER+align8(12u+16u+3u));
 cmd_payload_end(&w,cmd);return finish(&w);
}

/* MFC parameter bodies use the public APM rule: param_size is the body
 * rounded to four bytes, then the packet is padded to eight. The 3-channel
 * output body is 4+2+2+6=14 bytes, so param_size=16. */
static void zizhan_mfc_param_end(W *w,uint32_t at){
 set32(w,at+8,16);pad_to(w,at+32);
}
uint32_t zizhan_mfc_device_open(uint8_t *out,uint32_t capacity,uint32_t token){
 const Sg sg={ZizhanMfcSg,PERF_LOW_LATENCY,DIR_RX,SCENARIO_PLAYBACK};
 const Container c={ZizhanMfcContainer,POS_GLOBAL_DEV};
 const Module m[]={{0x07001015u,ZizhanMfcIid,1,1},{0x07001010u,ZizhanSalIid,1,1},
                   {0x0700100eu,ZizhanTdmIid,1,0}};
 const Link l[]={{ZizhanMfcIid,ZizhanSalIid},{ZizhanSalIid,ZizhanTdmIid}};
 return graph_open(out,capacity,ZizhanMfcPort,token,&sg,&c,m,3,l,2);
}
uint32_t zizhan_mfc_device_config(uint8_t *out,uint32_t capacity,uint32_t token){
 W w;begin(&w,out,capacity,ZizhanMfcPort,AR_APM_IID,token,AR_OP_SET_CFG);
 const uint32_t cmd=cmd_payload_start(&w);
 uint32_t at=param_begin(&w,ZizhanMfcIid,0x08001024u);
 put32(&w,96000);put16(&w,24);put16(&w,3);put16(&w,1);put16(&w,2);put16(&w,3);
 zizhan_mfc_param_end(&w,at);
 at=param_begin(&w,ZizhanSalIid,0x08001016u);put32(&w,24);zizhan_tdm_param_end(&w,at);
 at=param_begin(&w,ZizhanSalIid,0x0800101eu);put32(&w,1);zizhan_tdm_param_end(&w,at);
 zizhan_tdm_params(&w);
 cmd_payload_end(&w,cmd);return finish(&w);
}
uint32_t zizhan_pcm_open_to_mfc(uint8_t *out,uint32_t capacity,uint32_t token){
 const Sg sg={ZizhanPcmSg,PERF_LOW_LATENCY,DIR_RX,SCENARIO_PLAYBACK};
 const Container c={ZizhanPcmContainer,POS_STREAM};
 const Module m={AR_MOD_WR_SHMEM,ZizhanPcmShmem,0,1};
 const Link l={ZizhanPcmShmem,ZizhanMfcIid};
 return graph_open(out,capacity,ZizhanPcmPort,token,&sg,&c,&m,1,&l,1);
}
uint32_t zizhan_pcm_open_dec_to_mfc(uint8_t *out,uint32_t capacity,uint32_t token){
 const Sg sg={ZizhanPcmSg,PERF_LOW_LATENCY,DIR_RX,SCENARIO_PLAYBACK};
 const Container c={ZizhanPcmContainer,POS_STREAM};
 const Module m[]={{AR_MOD_WR_SHMEM,ZizhanPcmShmem,0,1},{AR_MOD_PCM_DEC,ZizhanPcmDecIid,1,1},{AR_MOD_PCM_CNV,ZizhanPcmCnvIid,1,1}};
 const Link l[]={{ZizhanPcmShmem,ZizhanPcmDecIid},{ZizhanPcmDecIid,ZizhanPcmCnvIid},{ZizhanPcmCnvIid,ZizhanMfcIid}};
 return graph_open(out,capacity,ZizhanPcmPort,token,&sg,&c,m,3,l,3);
}
/* AGM configure_output_media_format for RX: FIXED_POINT/PCM, bit_width 24 -> 32 bits per sample,
 * Q27, LSB aligned, little endian, deinterleaved unpacked, 2 channels FL/FR. payload_size 16+2. */
static void zizhan_pcm_out24(W *w,uint32_t iid){
 const uint32_t at=param_begin(w,iid,PARAM_PCM_OUTPUT);
 put32(w,1);put32(w,MEDIA_PCM);put32(w,16u+2u);
 put16(w,24);put16(w,1);put16(w,32);put16(w,27);put16(w,1);put16(w,DEINTERLEAVED_UNPACKED);put16(w,0);put16(w,2);
 put8(w,1);put8(w,2);
 zizhan_tdm_param_end(w,at);
}
uint32_t zizhan_pcm_dec_cnv_config(uint8_t *out,uint32_t capacity,uint32_t token){
 W w;begin(&w,out,capacity,ZizhanPcmPort,AR_APM_IID,token,AR_OP_SET_CFG);
 const uint32_t cmd=cmd_payload_start(&w);
 zizhan_pcm_out24(&w,ZizhanPcmDecIid);zizhan_pcm_out24(&w,ZizhanPcmCnvIid);
 cmd_payload_end(&w,cmd);return finish(&w);
}
uint32_t zizhan_pcm_open_loopback(uint8_t *out,uint32_t capacity,uint32_t token,unsigned dec){
 const Sg sg={ZizhanPcmSg,PERF_LOW_LATENCY,DIR_RX,SCENARIO_PLAYBACK};
 const Container c={ZizhanPcmContainer,POS_STREAM};
 const Module m[]={{AR_MOD_WR_SHMEM,ZizhanPcmShmem,0,1},{AR_MOD_PCM_CNV,ZizhanPcmCnvIid,1,1},
                   {AR_MOD_PCM_ENC,ZizhanPcmEncIid,1,1},{AR_MOD_RD_SHMEM,ZizhanPcmRdIid,1,0},{AR_MOD_PCM_DEC,ZizhanPcmDecIid,1,1}};
 const Link with[]={{ZizhanPcmShmem,ZizhanPcmDecIid},{ZizhanPcmDecIid,ZizhanPcmCnvIid},{ZizhanPcmCnvIid,ZizhanPcmEncIid},{ZizhanPcmEncIid,ZizhanPcmRdIid}};
 const Link without[]={{ZizhanPcmShmem,ZizhanPcmCnvIid},{ZizhanPcmCnvIid,ZizhanPcmEncIid},{ZizhanPcmEncIid,ZizhanPcmRdIid}};
 return dec?graph_open(out,capacity,ZizhanPcmPort,token,&sg,&c,m,5,with,4):graph_open(out,capacity,ZizhanPcmPort,token,&sg,&c,m,4,without,3);
}
static void zizhan_pcm_out16i(W *w,uint32_t iid,unsigned channels){
 const uint32_t at=param_begin(w,iid,PARAM_PCM_OUTPUT);
 put32(w,1);put32(w,MEDIA_PCM);put32(w,16u+channels);
 put16(w,16);put16(w,1);put16(w,16);put16(w,15);put16(w,1);put16(w,INTERLEAVED);put16(w,0);put16(w,(uint16_t)channels);
 for(unsigned i=0;i<channels;++i){put8(w,(uint8_t)(i+1));}
 zizhan_tdm_param_end(w,at);
}
static void zizhan_pcm_out24(W *w,uint32_t iid);
uint32_t zizhan_pcm_loopback_config(uint8_t *out,uint32_t capacity,uint32_t token,unsigned dec){
 W w;begin(&w,out,capacity,ZizhanPcmPort,AR_APM_IID,token,AR_OP_SET_CFG);
 const uint32_t cmd=cmd_payload_start(&w);
 if(dec){zizhan_pcm_out24(&w,ZizhanPcmDecIid);}
 zizhan_pcm_out24(&w,ZizhanPcmCnvIid);zizhan_pcm_out16i(&w,ZizhanPcmEncIid,2u);
 const uint32_t at=param_begin(&w,ZizhanPcmRdIid,0x08001007u);put32(&w,1);put32(&w,0);zizhan_tdm_param_end(&w,at);
 cmd_payload_end(&w,cmd);return finish(&w);
}
static void zizhan_mfc_param_end(W *w,uint32_t at);
uint32_t zizhan_mfc_loop_device_open(uint8_t *out,uint32_t capacity,uint32_t token){
 const Sg sg={ZizhanMfcSg,PERF_LOW_LATENCY,DIR_RX,SCENARIO_PLAYBACK};
 const Container c={ZizhanMfcContainer,POS_GLOBAL_DEV};
 const Module m[]={{0x07001015u,ZizhanMfcIid,1,1},{0x07001010u,ZizhanSalIid,1,1},
                   {AR_MOD_PCM_ENC,ZizhanPcmEncIid,1,1},{AR_MOD_RD_SHMEM,ZizhanPcmRdIid,1,0}};
 const Link l[]={{ZizhanMfcIid,ZizhanSalIid},{ZizhanSalIid,ZizhanPcmEncIid},{ZizhanPcmEncIid,ZizhanPcmRdIid}};
 return graph_open(out,capacity,ZizhanMfcPort,token,&sg,&c,m,4,l,3);
}
uint32_t zizhan_mfc_loop_device_config(uint8_t *out,uint32_t capacity,uint32_t token){
 W w;begin(&w,out,capacity,ZizhanMfcPort,AR_APM_IID,token,AR_OP_SET_CFG);
 const uint32_t cmd=cmd_payload_start(&w);
 uint32_t at=param_begin(&w,ZizhanMfcIid,0x08001024u);
 put32(&w,96000);put16(&w,24);put16(&w,3);put16(&w,1);put16(&w,2);put16(&w,3);
 zizhan_mfc_param_end(&w,at);
 at=param_begin(&w,ZizhanSalIid,0x08001016u);put32(&w,24);zizhan_tdm_param_end(&w,at);
 at=param_begin(&w,ZizhanSalIid,0x0800101eu);put32(&w,1);zizhan_tdm_param_end(&w,at);
 zizhan_pcm_out16i(&w,ZizhanPcmEncIid,3u);
 at=param_begin(&w,ZizhanPcmRdIid,0x08001007u);put32(&w,1);put32(&w,0);zizhan_tdm_param_end(&w,at);
 cmd_payload_end(&w,cmd);return finish(&w);
}
uint32_t zizhan_cap_open(uint8_t *out,uint32_t capacity,uint32_t token){
 const Sg sg={ZizhanCapSg,PERF_LOW_LATENCY,DIR_TX,SCENARIO_RECORD};
 const Container c={ZizhanCapContainer,POS_GLOBAL_DEV};
 const Module m[]={{0x0700100fu,ZizhanCapTdmIid,0,1},{AR_MOD_PCM_CNV,ZizhanCapCnvIid,1,1},{AR_MOD_PCM_ENC,ZizhanCapEncIid,1,1},{AR_MOD_RD_SHMEM,ZizhanCapRdIid,1,0}};
 const Link l[]={{ZizhanCapTdmIid,ZizhanCapCnvIid},{ZizhanCapCnvIid,ZizhanCapEncIid},{ZizhanCapEncIid,ZizhanCapRdIid}};
 return graph_open(out,capacity,ZizhanCapPort,token,&sg,&c,m,4,l,3);
}
uint32_t zizhan_cap_config(uint8_t *out,uint32_t capacity,uint32_t token){
 W w;begin(&w,out,capacity,ZizhanCapPort,AR_APM_IID,token,AR_OP_SET_CFG);
 const uint32_t cmd=cmd_payload_start(&w);
 /* ACDB TX source (b0000007/0x40a5, tag c0000005 kv 2): tertiary, internal sync, data_out 1, slots 0+1 (mask 3),
  * 4x32, delay 1; lane mask 1 (data0/GPIO122). Hardware endpoint 96k/16-bit/2ch. */
 uint32_t at=param_begin(&w,ZizhanCapTdmIid,0x0800101bu);
 put32(&w,0);put32(&w,2);put16(&w,1);put16(&w,1);put32(&w,3);
 put16(&w,4);put16(&w,32);put16(&w,0);put16(&w,0);put16(&w,1);put16(&w,0);
 zizhan_tdm_param_end(&w,at);
 at=param_begin(&w,ZizhanCapTdmIid,PARAM_HW_EP_MF);put32(&w,96000);put16(&w,16);put16(&w,2);put32(&w,1);zizhan_tdm_param_end(&w,at);
 at=param_begin(&w,ZizhanCapTdmIid,PARAM_FRAME_SIZE_FACTOR);put32(&w,1);zizhan_tdm_param_end(&w,at);
 at=param_begin(&w,ZizhanCapTdmIid,0x0800106au);put16(&w,1);put16(&w,0);zizhan_tdm_param_end(&w,at);
 at=param_begin(&w,ZizhanCapTdmIid,0x0800113cu);put32(&w,0);put32(&w,0);put32(&w,4);put32(&w,0);zizhan_tdm_param_end(&w,at);
 /* ACDB 0x0800108a {2,0x30,0x31}: rejected (status 17) next to the old 4-channel format; sent with the ACDB 2ch. */
 at=param_begin(&w,ZizhanCapTdmIid,0x0800108au);put16(&w,2);put16(&w,0x30);put16(&w,0x31);zizhan_tdm_param_end(&w,at);
 {const uint32_t cv=param_begin(&w,ZizhanCapCnvIid,PARAM_PCM_OUTPUT);
  put32(&w,1);put32(&w,MEDIA_PCM);put32(&w,16u+2u);
  put16(&w,24);put16(&w,1);put16(&w,32);put16(&w,27);put16(&w,1);put16(&w,DEINTERLEAVED_UNPACKED);put16(&w,0);put16(&w,2);
  put8(&w,1);put8(&w,2);zizhan_tdm_param_end(&w,cv);}
 zizhan_pcm_out16i(&w,ZizhanCapEncIid,2u);
 at=param_begin(&w,ZizhanCapRdIid,0x08001007u);put32(&w,1);put32(&w,0);zizhan_tdm_param_end(&w,at);
 cmd_payload_end(&w,cmd);return finish(&w);
}
uint32_t zizhan_cap_read(uint8_t *out,uint32_t capacity,uint32_t token,uint64_t address,uint32_t handle,uint32_t bytes){
 W w;begin(&w,out,capacity,ZizhanCapPort,ZizhanCapRdIid,token,AR_OP_READ);
 put32(&w,(uint32_t)address);put32(&w,(uint32_t)(address>>32));put32(&w,handle);put32(&w,bytes);
 put32(&w,0);put32(&w,0);put32(&w,0);put32(&w,0);
 return finish(&w);
}
uint32_t zizhan_pcm_read(uint8_t *out,uint32_t capacity,uint32_t token,uint64_t address,uint32_t handle,uint32_t bytes){
 W w;begin(&w,out,capacity,ZizhanPcmPort,ZizhanPcmRdIid,token,AR_OP_READ);
 put32(&w,(uint32_t)address);put32(&w,(uint32_t)(address>>32));put32(&w,handle);put32(&w,bytes);
 put32(&w,0);put32(&w,0);put32(&w,0);put32(&w,0);
 return finish(&w);
}
uint32_t zizhan_pcm_media_packed48(uint8_t *out,uint32_t capacity,uint32_t token){
 W w;begin(&w,out,capacity,ZizhanPcmPort,ZizhanPcmShmem,token,AR_OP_SET_CFG);
 const uint32_t cmd=cmd_payload_start(&w),at=param_begin(&w,ZizhanPcmShmem,PARAM_MEDIA_FORMAT);
 // Original Android speaker HAL: 48k/2ch/24-bit packed/LSB/Q23/little-endian.
 put32(&w,1);put32(&w,MEDIA_PCM);put32(&w,36);
 put32(&w,48000);put16(&w,24);put16(&w,1);
 put16(&w,24);put16(&w,23);put16(&w,1);put16(&w,2);put8(&w,1);put8(&w,2);
 set32(&w,at+8,align8(12u+16u+2u));pad_to(&w,at+PARAM_HEADER+align8(12u+16u+2u));
 cmd_payload_end(&w,cmd);return finish(&w);
}
uint32_t zizhan_graph_command(uint8_t *out,uint32_t capacity,uint32_t port,uint32_t token,
                              uint32_t opcode,uint32_t sg){
 return ar_graph_command(out,capacity,port,token,opcode,sg);
}

uint32_t ar_config_mic_device(uint8_t *out, uint32_t capacity, uint32_t port, uint32_t token, const ArFormat *f, uint32_t channel_mask)
{
    W w;
    begin(&w, out, capacity, port, AR_APM_IID, token, AR_OP_SET_CFG);
    const uint32_t cmd = cmd_payload_start(&w);
    hw_ep_mf(&w, AR_IID_MIC_DMA, f);
    frame_size_factor(&w, AR_IID_MIC_DMA);
    uint32_t at = param_begin(&w, AR_IID_MIC_DMA, PARAM_CODEC_DMA_INTF);
    put32(&w, LPAIF_RXTX); put32(&w, CODEC_TX3); put32(&w, channel_mask);
    param_end(&w, at);
    at = param_begin(&w, AR_IID_MIC_DMA, PARAM_HW_EP_POWER_MODE);
    put32(&w, 0);
    param_end(&w, at);
    cmd_payload_end(&w, cmd);
    return finish(&w);
}

/* PCM output format of a decoder, converter or encoder. */
static void pcm_output(W *w, uint32_t iid, const ArFormat *f, uint32_t interleaving)
{
    const uint32_t at = param_begin(w, iid, PARAM_PCM_OUTPUT);
    put32(w, 1); put32(w, MEDIA_PCM); put32(w, (16u + f->channels + 3u) & ~3u);
    put16(w, f->bits); put16(w, 1); put16(w, f->bits); put16(w, f->bits - 1u); put16(w, 1); put16(w, interleaving); put16(w, 0);
    put16(w, f->channels);
    channel_map(w, f->channels);
    param_end(w, at);
}

uint32_t ar_config_playback_stream(uint8_t *out, uint32_t capacity, uint32_t port, uint32_t token, const ArFormat *f)
{
    W w;
    begin(&w, out, capacity, port, AR_APM_IID, token, AR_OP_SET_CFG);
    const uint32_t cmd = cmd_payload_start(&w);
    pcm_output(&w, AR_IID_PLAY_DEC, f, INTERLEAVED);
    pcm_output(&w, AR_IID_PLAY_CNV, f, DEINTERLEAVED_UNPACKED);
    cmd_payload_end(&w, cmd);
    return finish(&w);
}

uint32_t ar_config_capture_stream(uint8_t *out, uint32_t capacity, uint32_t port, uint32_t token, const ArFormat *f)
{
    W w;
    begin(&w, out, capacity, port, AR_APM_IID, token, AR_OP_SET_CFG);
    const uint32_t cmd = cmd_payload_start(&w);
    pcm_output(&w, AR_IID_CAP_CNV, f, INTERLEAVED);
    pcm_output(&w, AR_IID_CAP_ENC, f, INTERLEAVED);
    cmd_payload_end(&w, cmd);
    return finish(&w);
}

/* The input media format of the playback shared memory endpoint, sent to the endpoint itself. */
uint32_t ar_config_playback_shmem(uint8_t *out, uint32_t capacity, uint32_t port, uint32_t token, const ArFormat *f)
{
    W w;
    begin(&w, out, capacity, port, AR_IID_PLAY_SHMEM, token, AR_OP_SET_CFG);
    const uint32_t cmd = cmd_payload_start(&w);
    const uint32_t at = param_begin(&w, AR_IID_PLAY_SHMEM, PARAM_MEDIA_FORMAT);
    const uint32_t body = align8(12u + 16u + f->channels);
    put32(&w, 1); put32(&w, MEDIA_PCM); put32(&w, body + PARAM_HEADER - 12u);
    put32(&w, f->rate); put16(&w, f->bits); put16(&w, 1); put16(&w, f->bits); put16(&w, f->bits - 1u); put16(&w, 1);
    put16(&w, f->channels);
    channel_map(&w, f->channels);
    param_end(&w, at);
    cmd_payload_end(&w, cmd);
    return finish(&w);
}

uint32_t ar_graph_command(uint8_t *out, uint32_t capacity, uint32_t port, uint32_t token, uint32_t opcode, uint32_t sg)
{
    W w;
    begin(&w, out, capacity, port, AR_APM_IID, token, opcode);
    const uint32_t cmd = cmd_payload_start(&w);
    const uint32_t at = param_begin(&w, AR_APM_IID, PARAM_SG_LIST);
    put32(&w, 1); put32(&w, sg);
    param_end(&w, at);
    cmd_payload_end(&w, cmd);
    return finish(&w);
}

/* As the DSP's own client builds it: the region follows the GPR header directly; the command header space trails the packet. */
uint32_t ar_map(uint8_t *out, uint32_t capacity, uint32_t port, uint32_t token, uint64_t address, uint32_t bytes)
{
    W w;
    begin(&w, out, capacity, port, AR_APM_IID, token, AR_OP_MEM_MAP);
    put16(&w, 3); put16(&w, 1); put32(&w, 0);                      /* shared 8-byte-aligned 4 KiB pool, one region, no flags */
    put32(&w, (uint32_t)address); put32(&w, (uint32_t)(address >> 32)); put32(&w, (bytes + 4095u) & ~4095u);
    pad_to(&w, w.n + CMD_HEADER);
    return finish(&w);
}

uint32_t ar_unmap(uint8_t *out, uint32_t capacity, uint32_t port, uint32_t token, uint32_t handle)
{
    W w;
    begin(&w, out, capacity, port, AR_APM_IID, token, AR_OP_MEM_UNMAP);
    put32(&w, handle);
    pad_to(&w, w.n + CMD_HEADER);
    return finish(&w);
}

uint32_t ar_write(uint8_t *out, uint32_t capacity, uint32_t port, uint32_t token, uint64_t address, uint32_t handle, uint32_t bytes)
{
    W w;
    begin(&w, out, capacity, port, AR_IID_PLAY_SHMEM, token, AR_OP_WRITE);
    put32(&w, (uint32_t)address); put32(&w, (uint32_t)(address >> 32)); put32(&w, handle); put32(&w, bytes);
    put32(&w, 0); put32(&w, 0); put32(&w, 0);                      /* no time stamp, no flags */
    put32(&w, 0); put32(&w, 0); put32(&w, 0); put32(&w, 0);         /* no metadata */
    return finish(&w);
}

uint32_t ar_read(uint8_t *out, uint32_t capacity, uint32_t port, uint32_t token, uint64_t address, uint32_t handle, uint32_t bytes)
{
    W w;
    begin(&w, out, capacity, port, AR_IID_CAP_SHMEM, token, AR_OP_READ);
    put32(&w, (uint32_t)address); put32(&w, (uint32_t)(address >> 32)); put32(&w, handle); put32(&w, bytes);
    put32(&w, 0); put32(&w, 0); put32(&w, 0); put32(&w, 0);
    return finish(&w);
}

uint32_t ar_prm_clock(uint8_t *out, uint32_t capacity, uint32_t port, uint32_t token, uint32_t clock_id, uint32_t hz)
{
    return ar_prm_clock_attr(out, capacity, port, token, clock_id, hz, 1);
}

uint32_t ar_prm_clock_attr(uint8_t *out, uint32_t capacity, uint32_t port, uint32_t token, uint32_t clock_id, uint32_t hz, uint32_t attribute)
{
    W w;
    begin(&w, out, capacity, port, AR_PRM_IID, token, hz ? AR_OP_PRM_REQUEST : AR_OP_PRM_RELEASE);
    const uint32_t cmd = cmd_payload_start(&w);
    const uint32_t at = param_begin(&w, AR_PRM_IID, 0x0800102Cu);    /* audio hardware clock */
    put32(&w, 1); put32(&w, clock_id);
    if (hz) {put32(&w, hz); put32(&w, attribute); put32(&w, 0);}    /* frequency, attribute (1 "couple no"), default root */
    set32(&w, at + 8, w.n - at - PARAM_HEADER);                      /* exact size: the reference client does not pad these */
    cmd_payload_end(&w, cmd);
    return finish(&w);
}

uint32_t ar_prm_vote(uint8_t *out, uint32_t capacity, uint32_t port, uint32_t token, uint32_t block, int enable)
{
    W w;
    begin(&w, out, capacity, port, AR_PRM_IID, token, enable ? AR_OP_PRM_REQUEST : AR_OP_PRM_RELEASE);
    const uint32_t cmd = cmd_payload_start(&w);
    const uint32_t at = param_begin(&w, AR_PRM_IID, 0x08001032u);    /* hardware core */
    put32(&w, block);
    set32(&w, at + 8, w.n - at - PARAM_HEADER);
    cmd_payload_end(&w, cmd);
    return finish(&w);
}

int ar_parse(const uint8_t *in, uint32_t length, ArAnswer *a)
{
    GprPacket p;
    for (unsigned i = 0; i < sizeof *a; ++i) ((uint8_t *)a)[i] = 0;
    if (gpr_decode(in, length, &p) != GprOk) return ArBad;
    a->opcode = p.opcode; a->token = p.token; a->src_port = p.src_port;
    const uint8_t *d = p.payload;
    switch (p.opcode) {
    case GPR_OP_BASIC_RESULT:
        if (p.payload_bytes < 8) return ArBad;
        a->result_opcode = le32(d); a->status = le32(d + 4);
        return ArBasicResult;
    case AR_OP_PRM_REQUEST_DONE:
    case AR_OP_PRM_RELEASE_DONE:
        if (p.payload_bytes < 4) return ArBad;
        a->status = le32(d + (p.payload_bytes >= 8 ? 4 : 0));
        return ArPrmDone;
    case GPR_OP_APM_SPF_STATE:
        if (p.payload_bytes < 4) return ArBad;
        a->status = le32(d);
        return ArFrameworkState;
    case AR_OP_MEM_MAP_DONE:
        if (p.payload_bytes < 4) return ArBad;
        a->handle = le32(d);
        return ArMapped;
    case AR_OP_WRITE_DONE:
        if (p.payload_bytes < 16) return ArBad;
        a->address = le32(d) | (uint64_t)le32(d + 4) << 32; a->handle = le32(d + 8); a->status = le32(d + 12);
        return ArWriteDone;
    case AR_OP_READ_DONE:
        if (p.payload_bytes < 20) return ArBad;
        a->status = le32(d); a->address = le32(d + 4) | (uint64_t)le32(d + 8) << 32; a->handle = le32(d + 12); a->data_bytes = le32(d + 16);
        return ArReadDone;
    default:
        return ArUnknown;
    }
}

/* Native APM map/unmap have NO command header/trailer. Linux Audioreach
 * alloc_apm_pkt(...false),actual parameter bytes20/4 directly after GPR. */
uint32_t zizhan_memory_map_packet(uint8_t *out,uint32_t capacity,uint32_t token,uint64_t address){
 W w;begin(&w,out,capacity,ZizhanMemoryPort,AR_APM_IID,token,AR_OP_MEM_MAP);
 put16(&w,3);put16(&w,1);put32(&w,0);put32(&w,(uint32_t)address);put32(&w,(uint32_t)(address>>32));put32(&w,65536);return finish(&w);
}
uint32_t zizhan_memory_unmap_packet(uint8_t *out,uint32_t capacity,uint32_t token,uint32_t handle){
 W w;begin(&w,out,capacity,ZizhanMemoryPort,AR_APM_IID,token,AR_OP_MEM_UNMAP);put32(&w,handle);return finish(&w);
}
