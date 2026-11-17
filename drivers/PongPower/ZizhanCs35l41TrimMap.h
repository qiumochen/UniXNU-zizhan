/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_CS35L41_TRIM_MAP_H
#define ZIZHAN_CS35L41_TRIM_MAP_H
#include <stdint.h>
/* Register facts: actual-phone OTP_ID8 map has99 rows identical to fixed
 * Linux v6.6 cs35l41-lib.c otp_map_1,plus zero-width reserved terminator.
 * No chip calibration values. Only currently observed ID8 is supported.
 * Reserved bits consume source but never produce writes. */
typedef struct {uint32_t reg;uint8_t shift,bits;} ZizhanAmpTrimField;
typedef struct {uint32_t reg,mask,value;} ZizhanAmpTrimUpdate;
static const ZizhanAmpTrimField zizhan_amp_trim8[100] = {
    {0x00002030,0,4},
    {0x00002030,7,1},
    {0x0000208c,24,6},
    {0x00002090,14,4},
    {0x00002090,10,4},
    {0x0000300c,11,4},
    {0x0000394c,23,2},
    {0x00003950,0,7},
    {0x00003950,8,7},
    {0x00003950,16,8},
    {0x00003950,24,8},
    {0x00003954,0,7},
    {0x00003954,8,7},
    {0x00003954,16,8},
    {0x00003954,24,8},
    {0x00003958,0,7},
    {0x00003958,8,7},
    {0x00003958,16,8},
    {0x00003958,24,8},
    {0x0000395c,0,7},
    {0x0000395c,8,7},
    {0x0000395c,16,8},
    {0x0000395c,24,8},
    {0x0000416c,0,8},
    {0x00004160,0,7},
    {0x0000416c,8,8},
    {0x00004160,16,10},
    {0x0000416c,16,12},
    {0x0000416c,28,1},
    {0x00004170,0,6},
    {0x00004170,6,1},
    {0x00004170,8,6},
    {0x00004170,14,1},
    {0x00004170,16,9},
    {0x00004360,0,5},
    {0x00004360,6,9},
    {0x00004448,0,8},
    {0x00004448,8,8},
    {0x00004448,16,8},
    {0x00004448,24,8},
    {0x0000444c,0,3},
    {0x00006e30,0,5},
    {0x00006e30,8,5},
    {0x00006e30,16,5},
    {0x00006e30,24,5},
    {0x00006e34,0,5},
    {0x00006e34,8,5},
    {0x00006e34,16,5},
    {0x00006e34,24,5},
    {0x00006e38,0,5},
    {0x00006e38,8,5},
    {0x00006e38,16,5},
    {0x00006e38,24,5},
    {0x00006e3c,0,5},
    {0x00006e3c,8,5},
    {0x00006e3c,16,5},
    {0x00006e3c,24,5},
    {0x00006e40,0,5},
    {0x00006e40,8,5},
    {0x00006e40,16,5},
    {0x00006e40,24,5},
    {0x00006e44,0,5},
    {0x00006e48,0,10},
    {0x00006e48,10,10},
    {0x00006e48,20,10},
    {0x00006e4c,0,10},
    {0x00006e4c,10,10},
    {0x00006e4c,20,10},
    {0x00006e50,0,10},
    {0x00006e50,10,10},
    {0x00006e50,20,10},
    {0x00006e54,0,10},
    {0x00006e54,10,10},
    {0x00006e54,20,10},
    {0x00006e58,0,10},
    {0x00006e58,10,10},
    {0x00006e58,20,10},
    {0x00006e5c,0,10},
    {0x00006e5c,10,10},
    {0x00006e5c,20,10},
    {0x00006e60,0,10},
    {0x00006e60,10,10},
    {0x00006e60,20,10},
    {0x00006e64,0,10},
    {0x00007418,7,5},
    {0x0000741c,0,5},
    {0x0000741c,11,4},
    {0x0000741c,19,4},
    {0x00007434,17,1},
    {0x00007434,18,7},
    {0x00007068,0,9},
    {0x0000410c,7,1},
    {0x0000400c,0,7},
    {0x00000000,0,1},
    {0x00017040,0,8},
    {0x00017040,8,8},
    {0x00017040,16,8},
    {0x00017040,24,8},
    {0x00017044,0,24},
    {0x00000000,0,0},
};
static int zizhan_amp_decode_trim8(uint32_t id,const uint32_t words[32],ZizhanAmpTrimUpdate out[98]) {
    if(id!=8||!words||!out)return 0;
    unsigned cursor=80,count=0;
    for(unsigned i=0;i<100;++i){
        const ZizhanAmpTrimField f=zizhan_amp_trim8[i];
        if(f.bits>24||f.shift+f.bits>32||cursor+f.bits>1024)return 0;
        uint32_t value=0;
        for(unsigned b=0;b<f.bits;++b)value|=((words[(cursor+b)/32]>>((cursor+b)%32))&1u)<<b;
        cursor+=f.bits;
        if(!f.reg)continue;
        if(!f.bits||count>=98)return 0;
        const uint32_t mask=((1u<<f.bits)-1u)<<f.shift;
        for(unsigned p=0;p<count;++p)if(out[p].reg==f.reg&&(out[p].mask&mask))return 0;
        out[count++]=(ZizhanAmpTrimUpdate){f.reg,mask,value<<f.shift};
    }
    return cursor==768&&count==98;
}
static int zizhan_amp_trim_register(uint32_t reg){
    if(!reg)return 0;
    for(unsigned i=0;i<100;++i)if(zizhan_amp_trim8[i].reg==reg)return 1;
    return 0;
}
#endif
