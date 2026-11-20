/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <IOKit/IOService.h>
#include <libkern/OSKextLib.h>
#include <mach/kmod.h>
#include <pexpert/pexpert.h>
#include "ZizhanCamProbe.h"
#include "ZizhanModemProbe.h"
extern kmod_info_t g_kernel_kmod_info;
extern "C" {
void pong_rpmh_init(void);
void zizhan_mpss_start(void);
void zizhan_slpi_start(void);
void sheng_adsp_probe(void);
void sheng_audio_probe(void);
void sheng_battery_start(void);
void zizhan_se15_probe(void);
void zizhan_se15_prepare_probe(void);
void zizhan_cmd_db_probe(void);
void zizhan_standby_rpmh_start(void);
void zizhan_audio_prm_probe(void);
void zizhan_audio_service_probe(void);
void PongBatteryStaticInit(void);
volatile int32_t native_battery_stats[16];
void pong_power_start_optional(void) {
 pong_rpmh_init();
 zizhan_se15_probe();
 zizhan_se15_prepare_probe();
 zizhan_cmd_db_probe();
 zizhan_standby_rpmh_start();  // default off; POWER-05 zizhan_standby_cx=<level> / zizhan_rpmh_dump=1
 zizhan_modem_probe::start();  // default off; 1 = read-only firmware/EFS reachability (MODEM-01 stage 1a)
 zizhan_mpss_start();          // default off; zizhan_modem=2 with pong_adsp=0: boot the modem once, watch it, stop it (stage 1b)
 zizhan_slpi_start();          // default off; zizhan_slpi=1 with pong_adsp=0: boot the sensor DSP once, list its QMI services (SENS-01)
 zizhan_cam_probe::start();   // default off; 1 read-only inventory, 2 attended front-camera chip id
 unsigned enabled=0;PE_parse_boot_argn("pong_adsp",&enabled,sizeof(enabled));
 if(!enabled)return;
 sheng_adsp_probe();   // pong_adsp=1: map and survey only (zizhan bring-up); 2: boot the DSP and its clients
 if(enabled!=2)return;
 // DSP channel registration is ready synchronously; remote opens follow
 // only after firmware and PAS prerequisites complete on its worker.
 void *h=OSMetaClass::preModLoad(g_kernel_kmod_info.name);
 if(h){PongBatteryStaticInit();if(!OSMetaClass::postModLoad(h))sheng_battery_start();}
 sheng_audio_probe();
 zizhan_audio_prm_probe();
 zizhan_audio_service_probe();   // default off; zizhan_audio_service=1 (I1: service/memory only, no graph)
}
}
