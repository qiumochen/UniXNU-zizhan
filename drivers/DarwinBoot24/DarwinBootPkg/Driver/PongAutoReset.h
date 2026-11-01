// SPDX-License-Identifier: MIT
// zizhan unattended bring-up (boot-arg pong_autoreset=<s>): before the kernel runs, write "bootloader" into the PMIC
// reboot-reason cell and arm the Gunyah watchdog (60 s bite). The kernel's platform expert re-arms both and pings, but a
// panic or hang before it gets there (g16: kalloc_type qsort overflowed the boot stack) used to sit on screen until
// someone pressed keys; now the bite lands in fastboot with the ramoops console intact. The firmware maps the SPMI
// arbiter one to one (the AOSS register region), so the kernel's register code is used unchanged.
#ifndef PONG_AUTO_RESET_H
#define PONG_AUTO_RESET_H
#define BR_NO_STDINT
typedef UINT8  uint8_t;
typedef UINT32 uint32_t;
typedef UINT64 uint64_t;
#include "../../../ArmDTPlatform/BootResetCore.h"

#define PONG_SPMI_CORE    0x0C400000
#define PONG_SPMI_OBSRVR  0x0C440000
#define PONG_SPMI_CHNLS   0x0C500000
#define PONG_SPMI_CNFG    0x0C42D000

STATIC
UINT64
PongSmc (
  IN UINT64  Function,
  IN UINT64  Arg1,
  IN UINT64  Arg2
  )
{
  register UINT64 X0 __asm__("x0") = Function, X1 __asm__("x1") = Arg1, X2 __asm__("x2") = Arg2, X3 __asm__("x3") = 0;
  __asm__ volatile("smc #0" : "+r"(X0), "+r"(X1), "+r"(X2), "+r"(X3) :: "memory", "x4", "x5", "x6", "x7", "x8", "x9", "x10",
                   "x11", "x12", "x13", "x14", "x15", "x16", "x17");
  return X0;
}

// TRUE when the command line asks for pong_autoreset with a nonzero deadline.
STATIC
BOOLEAN
PongAutoResetWanted (
  IN CONST CHAR8  *CommandLine
  )
{
  CONST CHAR8  *Arg = AsciiStrStr (CommandLine, "pong_autoreset=");
  return Arg != NULL && AsciiStrDecimalToUintn (Arg + sizeof ("pong_autoreset=") - 1) != 0;
}

// Same steps and result codes as the kernel's markBootloader. 0 = written and read back.
STATIC
INT32
PongMarkBootloader (
  OUT UINT8  *Before,
  OUT UINT8  *After
  )
{
  struct br_arbiter  Arbiter = { (volatile UINT32 *)(UINTN)PONG_SPMI_CORE, (volatile UINT32 *)(UINTN)PONG_SPMI_CNFG, 0 };
  struct br_found    Found;

  *Before = *After = 0xff;
  if (br_find (&Arbiter, &Found)) { return 2; }
  if (Found.owner != Arbiter.ee) { return 5; }
  return br_set_bootloader ((volatile UINT32 *)(UINTN)PONG_SPMI_OBSRVR, (volatile UINT32 *)(UINTN)PONG_SPMI_CHNLS, &Arbiter,
                            Found.apid, Before, After);
}

// Called after ExitBootServices, when no firmware driver pets or disables the watchdog any more.
STATIC
VOID
PongArmWatchdog (
  VOID
  )
{
  PongSmc (0x86000005, 2, 0);            // control: disable
  PongSmc (0x86000008, 60000, 60000);    // set-time: bark and bite in ms
  PongSmc (0x86000005, 3, 0);            // control: enable
}
#endif
