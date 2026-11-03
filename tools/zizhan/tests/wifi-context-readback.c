/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <stdio.h>
#include "ArmSmmu4KContext.h"
int main(void) {
 ArmSmmu4KConfig region={32,36,0x86b8a9000ULL,32769};
 ArmSmmu4KResult tables={.root_physical=0x86b8a9000ULL,.mair=0xff44,.table_pages=67,.leaf_pages=32769,.start_level=1};
 ArmSmmu4KContextConfig c={.lower=&region,.lower_tables=&tables,.id0=1u<<30,.id2=(1u<<12)|(1u<<8)|(1u<<4),.asid=0x1c01,.coherent_walk=1,.polled_faults=1};
 /* g272 readback while SCTLR was still disabled; validation substitutes the intended SCTLR. */
 ArmSmmu4KContextImage captured={.cbar=0x1f300,.cba2r=1,.tcr=0x80803520,.tcr2=0x38071,.mair0=0xff44,.sctlr=0x1027,.ttbr0=0x1c0100086b8a9000ULL,.ttbr1=0x1c01000000000000ULL};
 assert(ArmSmmu4KValidateContextReadback(&c,&captured)==ArmSmmu4KStateMismatch);
 c.ignore_top_byte=1;
 assert(ArmSmmu4KValidateContextReadback(&c,&captured)==ArmSmmu4KSuccess);
 captured.sctlr=0x27; /* g273 reaches enabled SCTLR with ASIDPNE clear. */
 assert(ArmSmmu4KValidateContextReadback(&c,&captured)==ArmSmmu4KStateMismatch);
 c.shared_asid=1;
 assert(ArmSmmu4KValidateContextReadback(&c,&captured)==ArmSmmu4KSuccess);
 captured.tcr2^=0x10; /* ASID width remains strictly checked. */
 assert(ArmSmmu4KValidateContextReadback(&c,&captured)==ArmSmmu4KStateMismatch);
 puts("PASS: g272/g273 captured mismatches reproduced; explicit TBI/shared-ASID match; incorrect ASID width rejected");
}
