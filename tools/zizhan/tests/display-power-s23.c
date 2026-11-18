#include "ZizhanDisplayPowerExecutor.h"
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond,name) do{ \
 if(cond)printf("PASS %s\n",name); \
 else{printf("FAIL %s\n",name);++failures;} \
}while(0)

struct Fake {
 uint32_t level;
 int get_fail, set_fail, dsi_fail_always;
 unsigned set_calls, dsi_calls, dsi_fail_at;
 uint32_t set_levels[8];
 struct { uint8_t cmds[8]; uint16_t waits[8]; unsigned count; int low; } seq[8];
};

static int fake_get(uint32_t *level, void *context) {
 struct Fake *f = context;
 if (f->get_fail) return -1;
 *level = f->level;
 return 0;
}

static int fake_set(uint32_t level, uint32_t timeout_ms, void *context) {
 struct Fake *f = context;
 (void)timeout_ms;
 if (f->set_fail) return -1;
 if (f->set_calls < 8) f->set_levels[f->set_calls] = level;
 ++f->set_calls;
 f->level = level;
 return 0;
}

static int fake_dsi(const uint8_t *dcs, const uint16_t *waits, uint32_t count,
                    int low, void *context) {
 struct Fake *f = context;
 ++f->dsi_calls;
 if (f->dsi_fail_always || (f->dsi_fail_at && f->dsi_calls == f->dsi_fail_at))
  return -1;
 if (f->dsi_calls <= 8) {
  const unsigned at = f->dsi_calls - 1;
  f->seq[at].count = count > 8 ? 8 : count;
  for (unsigned i = 0; i < f->seq[at].count; ++i) {
   f->seq[at].cmds[i] = dcs[i];
   f->seq[at].waits[i] = waits[i];
  }
  f->seq[at].low = low;
 }
 return 0;
}

static void reset_fake(struct Fake *f) {
 memset(f, 0, sizeof *f);
 f->level = 512;
}

static int run(ZizhanDisplayPowerRuntime *rt, struct Fake *f, uint64_t in0,
               ZizhanDisplayPowerReport *report) {
 const ZizhanDisplayPowerOps ops = {fake_get, fake_set, fake_dsi};
 const uint64_t in[1] = {in0};
 return zizhan_display_power_execute(rt, &ops, f, 1, in, 0, 1000000, report);
}

int main(void) {
 CHECK(zizhan_display_power_mode(0) == 0, "mode default off");
 CHECK(zizhan_display_power_mode(1) == 1, "mode enabled");
 CHECK(zizhan_display_power_mode(2) == -1, "mode unsupported rejected");

 ZizhanDisplayPowerRuntime rt;
 struct Fake f;
 ZizhanDisplayPowerReport r;

 reset_fake(&f);
 zizhan_display_power_runtime_init(&rt);
 CHECK(run(&rt, &f, 0, &r) == ZizhanDisplayPowerOk, "off succeeds");
 CHECK(rt.state.stable == ZizhanDisplayStateOff && rt.state.state == ZizhanDisplayStateOff,
       "off stable off");
 CHECK(r.saved_level == 512 && f.set_calls == 1 && f.set_levels[0] == 0 && f.level == 0,
       "off brightness 0 saved 512");
 CHECK(f.dsi_calls == 1 && f.seq[0].count == 2 && f.seq[0].cmds[0] == 0x28 &&
       f.seq[0].cmds[1] == 0x10 && f.seq[0].waits[0] == 5 &&
       f.seq[0].waits[1] == 120 && f.seq[0].low == 1,
       "off DCS 0x28/0x10 waits 5/120 LP");

 CHECK(run(&rt, &f, 1, &r) == ZizhanDisplayPowerOk, "on succeeds");
 CHECK(rt.state.stable == ZizhanDisplayStateOn && rt.state.state == ZizhanDisplayStateOn,
       "on stable on");
 CHECK(f.dsi_calls == 2 && f.seq[1].count == 2 && f.seq[1].cmds[0] == 0x11 &&
       f.seq[1].cmds[1] == 0x29 && f.seq[1].waits[0] == 120 &&
       f.seq[1].waits[1] == 120 && f.seq[1].low == 1,
       "on DCS 0x11/0x29 waits 120/120 LP");
 CHECK(f.set_calls == 2 && f.set_levels[1] == 512 && f.level == 512,
       "on restores saved brightness");

 const unsigned dsi_before = f.dsi_calls, set_before = f.set_calls;
 CHECK(run(&rt, &f, 1, &r) == ZizhanDisplayPowerNoOp, "duplicate on no-op");
 CHECK(f.dsi_calls == dsi_before && f.set_calls == set_before, "duplicate on no hardware");
 CHECK(run(&rt, &f, 0, &r) == ZizhanDisplayPowerOk, "second off succeeds");
 CHECK(run(&rt, &f, 0, &r) == ZizhanDisplayPowerNoOp, "duplicate off no-op");

 zizhan_display_power_runtime_init(&rt);
 reset_fake(&f);
 CHECK(run(&rt, &f, 2, &r) == ZizhanDisplayPowerRejected, "unknown in0 rejected");
 CHECK(f.dsi_calls == 0 && f.set_calls == 0 && rt.state.stable == ZizhanDisplayStateOn,
       "unknown in0 no hardware");
 CHECK(rt.state.rejected_unknown == 1, "unknown in0 counted");

 zizhan_display_power_runtime_init(&rt);
 reset_fake(&f);
 f.dsi_fail_at = 1;
 CHECK(run(&rt, &f, 0, &r) == ZizhanDisplayPowerFailed, "off DSI failure fails");
 CHECK(r.dsi_failed == 1 && r.restored == 1, "off failure restores");
 CHECK(f.dsi_calls == 2 && f.seq[1].cmds[0] == 0x11 && f.seq[1].cmds[1] == 0x29,
       "off failure runs on sequence");
 CHECK(f.set_calls == 2 && f.set_levels[1] == 512 && f.level == 512,
       "off failure restores brightness");
 CHECK(rt.state.stable == ZizhanDisplayStateOn, "off failure keeps stable on");

 zizhan_display_power_runtime_init(&rt);
 reset_fake(&f);
 f.dsi_fail_always = 1;
 CHECK(run(&rt, &f, 0, &r) == ZizhanDisplayPowerFailed, "off + restore failure fails");
 CHECK(r.dsi_failed == 1 && r.restored == 0 && f.level == 0,
       "restore failure leaves brightness 0");
 CHECK(rt.state.stable == ZizhanDisplayStateOn, "restore failure still stable on");

 zizhan_display_power_runtime_init(&rt);
 reset_fake(&f);
 f.get_fail = 1;
 CHECK(run(&rt, &f, 0, &r) == ZizhanDisplayPowerFailed, "backlight get failure fails");
 CHECK(f.dsi_calls == 0 && f.set_calls == 0, "get failure no DSI/set");
 CHECK(rt.state.stable == ZizhanDisplayStateOn, "get failure stable on");

 zizhan_display_power_runtime_init(&rt);
 reset_fake(&f);
 f.set_fail = 1;
 CHECK(run(&rt, &f, 0, &r) == ZizhanDisplayPowerFailed, "brightness 0 failure fails");
 CHECK(f.dsi_calls == 0, "brightness failure no DSI");
 CHECK(rt.state.stable == ZizhanDisplayStateOn, "brightness failure stable on");

 zizhan_display_power_runtime_init(&rt);
 reset_fake(&f);
 CHECK(run(&rt, &f, 0, &r) == ZizhanDisplayPowerOk, "on-failure setup off");
 f.dsi_fail_at = f.dsi_calls + 1;
 CHECK(run(&rt, &f, 1, &r) == ZizhanDisplayPowerFailed, "on DSI failure fails");
 CHECK(f.level == 0 && rt.state.stable == ZizhanDisplayStateOff,
       "on failure leaves off and brightness 0");

 zizhan_display_power_runtime_init(&rt);
 reset_fake(&f);
 zizhan_display_set_busy(&rt.state, 1);
 CHECK(run(&rt, &f, 0, &r) == ZizhanDisplayPowerRejected, "busy request rejected");
 CHECK(f.dsi_calls == 0 && f.set_calls == 0, "busy request no hardware");

 printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
 return failures ? 1 : 0;
}
