/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_AMP_OUTPUT_PLAN_H
#define ZIZHAN_AMP_OUTPUT_PLAN_H
#include <stdint.h>
/* This phone's saved DT:1000nH/15uF/4000mA;original module set_pdata
 * row0/cap bucket0 -> K1=24/K2=24/slope75/ind0/IPK40. Native mixer
 * register/shift/value tables establish the other fields. All are volatile
 * configuration, not AMP/BST/GLOBAL/ASP enable, OTP or calibration data.
 * Native digital volume index0 maps to2678 in6000[13:3];this is minimum
 * digital volume. Output enables MUST remain off independently of volume. */
enum {ZizhanOutputFields=14};
typedef struct {uint32_t reg,mask,top,bottom;} ZizhanOutputField;
static const ZizhanOutputField zizhan_output_fields[ZizhanOutputFields]={
 {0x3810,0xffff,0x2424,0x2424}, {0x3814,0xff03,0x7500,0x7500},
 {0x3808,0x7f,0x40,0x40}, {0x4220,3,3,3}, {0x480c,3,3,3},
 {0x4820,0x707,0x200,0x401}, /* RX1 top0/bottom1;RX2 top2/bottom4. */
 {0x4c00,0x7f,0x32,0x32}, /* DSPTX1 -> amplifier PCM;still disabled. */
 {0x4c40,0x7f,8,8}, {0x4c44,0x7f,9,8},
 {0x6c04,0x3e0,0xa0,0xa0}, /* Native handset control code5,max20. */
 {0x6000,0x3ff8,0x2678,0x2678},
 {0x6000,7,4,4}, /* Original enum:4ms soft ramp. */
 {0x3804,1,1,1}, {0x3800,0xff,0,0}
};
#endif
