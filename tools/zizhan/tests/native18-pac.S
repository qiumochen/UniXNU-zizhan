/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Bounded Android EL0 architectural PAuth probe. No runtime, MMIO, key writes,
 * files, network, invalid-pointer dereference or invalid-target branch.
 * Only write(1) and exit are used; Linux owns this process's keys.
 */
.text
.p2align 2
.global _start
.type _start,%function
_start:
    /* Locate AT_HWCAP through the actual Linux initial stack. */
    ldr x1,[sp]
    cmp x1,#1024
    b.hs .Lskip
    add x19,sp,#8
    add x19,x19,x1,lsl #3
    add x19,x19,#8
    mov x2,#4096
.Lenv:
    ldr x1,[x19],#8
    cbz x1,.Laux_start
    subs x2,x2,#1
    b.ne .Lenv
    b .Lskip
.Laux_start:
    mov x2,#256
.Laux:
    ldp x1,x22,[x19],#16
    cbz x1,.Lskip
    cmp x1,#16
    b.eq .Lcaps
    subs x2,x2,#1
    b.ne .Laux
    b .Lskip
.Lcaps:
    mov x9,#0xc0000000  /* Linux arm64 HWCAP_PACA | HWCAP_PACG */
    and x8,x22,x9
    cmp x8,x9
    b.ne .Lskip
#ifdef PAC_RECORD_CPU
    /* Observe the actual executing CPU; online/affinity dispatch alone is
     * insufficient evidence. This temporary stack frame precedes PAC work. */
    sub sp,sp,#16
    mov x0,sp
    mov x1,#0
    mov x2,#0
    mov x8,#168
    svc #0
    mov x23,#8
    cbnz x0,.Lfail
    ldr w10,[sp]
    cmp w10,#7
    b.hi .Lfail
    add w10,w10,#48
    mov w9,#67
    strb w9,[sp,#8]
    strb w10,[sp,#9]
    mov w9,#10
    strb w9,[sp,#10]
    mov x0,#1
    add x1,sp,#8
    mov x2,#3
    mov x8,#64
    svc #0
    add sp,sp,#16
#endif
    adr x9,.Lreturn_a
.macro STAGE number
    mov x0,#1
    adr x1,.Lstages
    add x1,x1,#\number
    mov x2,#1
    mov x8,#64
    svc #0
.endm

    /* Every key must change at least one signature and reject at least one
     * wrong modifier; all sixteen correct-modifier authentications match.
     * Failed authentications are inspected as integers, never dereferenced.
     */
.macro CHECK sign,auth,stage
    STAGE \stage
    mov x23,#\stage
    mov x24,#16
    mov x25,#0
    mov x26,#0
    mov x11,#0x1234
.Lround\@:
    mov x10,x9
    \sign x10,x11
    eor x12,x10,x9
    orr x25,x25,x12
    mov x13,x10
    \auth x13,x11
    cmp x13,x9
    b.ne .Lfail
#ifndef PAC_CORRECT_ONLY
    add x14,x11,#1
    \auth x10,x14
    eor x12,x10,x9
    orr x26,x26,x12
#endif
    add x11,x11,#7
    subs x24,x24,#1
    b.ne .Lround\@
    cbz x25,.Lfail
#ifndef PAC_CORRECT_ONLY
    cbz x26,.Lfail
#endif
.endm
    CHECK pacia,autia,1
    CHECK pacib,autib,2
    CHECK pacda,autda,3
    CHECK pacdb,autdb,4

    mov x23,#5
    STAGE 5
    mov x15,#0
    mov x16,#0
    pacga x15,x9,x11
    add x14,x11,#1
    pacga x16,x9,x14
    cmp x15,x16
    b.eq .Lfail

    mov x23,#6
    STAGE 6
    adr x10,.Lreturn_a
    pacia x10,x11
    blraa x10,x11
    cmp w0,#42
    b.ne .Lfail
    mov x23,#7
    STAGE 7
    adr x10,.Lreturn_b
    pacib x10,x11
    blrab x10,x11
    cmp w0,#43
    b.ne .Lfail

    mov x0,#1
    adr x1,.Lpass_text
    mov x2,#(.Lpass_end-.Lpass_text)
    mov x8,#64
    svc #0
    mov x0,#0
    mov x8,#93
    svc #0
.Lfail:
    mov x0,#1
    adr x1,.Lfail_text
    mov x2,#(.Lfail_end-.Lfail_text)
    mov x8,#64
    svc #0
    mov x0,x23
    mov x8,#93
    svc #0
.Lskip:
    mov x0,#1
    adr x1,.Lskip_text
    mov x2,#(.Lskip_end-.Lskip_text)
    mov x8,#64
    svc #0
    mov x0,#77
    mov x8,#93
    svc #0
.Lreturn_a:
    paciasp
    mov w0,#42
    retaa
.Lreturn_b:
    pacibsp
    mov w0,#43
    retab
.Lpass_text:
    .ascii "\nPASS: Android EL0 PACA IA/IB/DA/DB, PACGA and authenticated branch/return\n"
.Lpass_end:
.Lfail_text:
    .ascii "FAIL: Android EL0 PAC probe; exit status identifies stage\n"
.Lfail_end:
.Lskip_text:
    .ascii "SKIP: Android EL0 PACA/PACG capabilities unavailable\n"
.Lskip_end:
.Lstages:
    .ascii "01234567"
.size _start,.-_start
