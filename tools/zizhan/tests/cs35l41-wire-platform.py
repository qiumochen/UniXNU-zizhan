#!/usr/bin/env python3
"""Execute actual SE15 wire owner with explicit chip-proof test inputs.

Only the chip-transaction functions are replaced: those production cores
run separately under the complete GENI model. This checks actual dispatch
of native full-core initialization, GPIO ownership, callback and reset /
pin restoration, including the mode35 regression that real hardware caught.
"""
import ast
import os
from pathlib import Path
import subprocess
import tempfile
R=Path(__file__).resolve().parents[3]
tree=ast.parse((R/'tools/zizhan/tests/cs35l41-setup.py').read_text())
harness=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='harness' for t in n.targets))
harness=harness[:harness.index('/* ACTUAL_SOURCE */')]

harness=harness.replace('bool writable=base==0x162000', 'bool writable=base==0xf179000||base==0xf17b000||base==0xf17c000||base==0x162000')
harness=harness.replace('static unsigned mode=1,', 'static unsigned attended=0;\nstatic unsigned mode=1,').replace('if(!strcmp(key,"zizhan_se15_prepare"))', 'if(!strcmp(key,"zizhan_audio_attended"))v=attended;else if(!strcmp(key,"zizhan_se15_prepare"))')
source='\n'.join(l for l in (R/'drivers/PongPower/ZizhanSe15Prepare.cpp').read_text().splitlines() if not l.startswith('#include'))
for code in ('__asm__ volatile("dmb oshld" ::: "memory");','__asm__ volatile("dsb oshst" ::: "memory");'):
    source=source.replace(code,'')
store='*reinterpret_cast<volatile uint32_t *>(va + at) = value;'
assert source.count(store)==1
source=source.replace(store,'model_store(va,at,value);')
stubs=r'''
static unsigned native_calls,legacy_calls,core_calls,callbacks;
static bool old_core,claim_fail,restore_fail,callback_fail,output_fail;static unsigned output_calls;
static void model_store(volatile uint8_t *va,unsigned at,uint32_t value){
 uint64_t base=0;for(auto &p:pages)if(reinterpret_cast<volatile uint8_t *>(p.second.data())==va)base=p.first;assert(base);
 if(base==0xf17c000){assert(at==0);if((value==0xc4&&claim_fail)||(value==1&&restore_fail))return;}
 if(base==0xf101000&&at==4)value|=(value&2)?1u:0u;
 *reinterpret_cast<volatile uint32_t *>(va+at)=value;
}
extern "C" int model_off(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,ZizhanAmpSafeOff *o){o->status=ZizhanAmpPass;return 0;}
extern "C" int model_config(unsigned,unsigned,const GeniI2c *,ZizhanAmpBusConfig *o){memset(o,0,sizeof *o);return 0;}
extern "C" int model_snapshot(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,const ZizhanAmpSafeOff *,ZizhanAmpReadiness *,ZizhanAmpSnapshot *o){o->status=ZizhanAmpPass;return 0;}
extern "C" int model_b2(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,const ZizhanAmpSnapshot *,ZizhanAmpB2Patch *o){o->status=ZizhanAmpPass;return 0;}
extern "C" int model_trim(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,const ZizhanAmpSnapshot *,const ZizhanAmpB2Patch *,ZizhanAmpTrim *o){o->status=ZizhanAmpPass;return 0;}
extern "C" int model_native(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,const ZizhanAmpSnapshot *,const ZizhanAmpTrim *,const uint8_t *,unsigned,ZizhanAmpHash,ZizhanAmpRam *o){++native_calls;o->status=ZizhanAmpPass;o->native_core_init=old_core?0:1;return 0;}
extern "C" int model_legacy(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,const ZizhanAmpSnapshot *,const ZizhanAmpTrim *,const uint8_t *,unsigned,ZizhanAmpHash,ZizhanAmpRam *o){++legacy_calls;o->status=ZizhanAmpPass;return 0;}
extern "C" int model_coeff(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,const ZizhanAmpRam *ram,const uint8_t *,unsigned,const uint8_t *,unsigned,ZizhanAmpHash,ZizhanAmpCoefficients *o){o->status=ZizhanAmpPass;for(unsigned c=0;c<2;++c){o->after_state[c][1]=0x3300;o->after_state[c][2]=ram->native_core_init?0:0x100;}return 0;}
extern "C" int model_core(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,const ZizhanAmpCoefficients *coeff,const ZizhanAmpSpeakerCalibration *,const ZizhanAudioTransfer *transfer,ZizhanAmpRun *o){
 ++core_calls;assert(native_calls==1&&!legacy_calls&&coeff->after_state[0][2]==0&&coeff->after_state[1][2]==0);
 assert(pages[0xf179000][0]==0x284&&pages[0xf17b000][0]==0x2c4&&pages[0xf17c000][0]==0xc4&&pages[0xf17c000][1]==0&&pages[0xf101000][1]==3);
 o->status=ZizhanAmpPass;o->completed=2;o->mailbox_completed[0]=o->mailbox_completed[1]=2;for(unsigned c=0;c<2;++c)o->pulse[c].complete=o->pulse[c].patch_off=o->pulse[c].disabled=o->pulse[c].data_ack=1;for(unsigned c=0;c<2;++c)for(unsigned i=0;i<2;++i)o->iv_reads[c][i]=3;
 for(unsigned c=0;c<2;++c){++o->transfer_attempts[c];if(transfer->once(transfer->context,c)!=1){o->status=ZizhanAmpFail;o->completed=c;return 1;}++o->transfer_acks[c];}
 return 0;
}
#define zizhan_cs35l41_safe_off model_off
#define zizhan_cs35l41_bus_config model_config
#define zizhan_cs35l41_owned_snapshot model_snapshot
#define zizhan_cs35l41_b2_patch model_b2
#define zizhan_cs35l41_trim model_trim
#define zizhan_cs35l41_ram_native_init model_native
#define zizhan_cs35l41_ram_burst model_legacy
#define zizhan_cs35l41_coefficients model_coeff
extern "C" int model_output(unsigned,unsigned,GeniI2c *,const ZizhanAmpClock *,const ZizhanAmpCoefficients *coeff,ZizhanAmpOutputSetup *o){++output_calls;assert(native_calls==1&&coeff->after_state[0][2]==0&&coeff->after_state[1][2]==0);o->status=output_fail?ZizhanAmpFail:ZizhanAmpPass;return output_fail?1:0;}
#define zizhan_cs35l41_output_setup model_output
#define zizhan_cs35l41_native_core_transfer model_core
#define zizhan_cs35l41_native_mailbox_transfer model_core
#define zizhan_cs35l41_native_iv_transfer model_core
#define zizhan_cs35l41_native_pulse_transfer model_core
'''
main=r'''
int main(int argc,char **argv){assert(argc==2);std::string test=argv[1];mode=(test=="pulse"||test=="unattended-pulse")?39:test=="iv"?38:test=="mailbox"?37:(test=="output-pass"||test=="output-fail")?36:35;output_fail=test=="output-fail";attended=test=="pulse";
 for(uint64_t base:{0x162000,0x12e000,0x133000,0xf138000,0xf139000,0xf101000,0xf179000,0xf17b000,0xf17c000})pages[base].resize(1024);
 pages[0x880000].resize(4096);pages[0xf138000][0]=pages[0xf139000][0]=pages[0xf101000][0]=pages[0xf179000][0]=pages[0xf17b000][0]=pages[0xf17c000][0]=1;
 pages[0x880000][0x68/4]=3<<8;pages[0x880000][0xe24/4]=pages[0x880000][0xe28/4]=(32u<<24)|(16u<<16);
 if(test=="old-core")old_core=true;else if(test=="foreign-pin")pages[0xf17c000][0]=4;else if(test=="claim")claim_fail=true;else if(test=="restore")restore_fail=true;else if(test=="callback")callback_fail=true;else assert(test=="pass"||test=="missing"||test=="output-pass"||test=="output-fail"||test=="mailbox"||test=="iv"||test=="pulse"||test=="unattended-pulse");
 ZizhanPrmState lease={};ZizhanAudioSilentReceipt receipt={};lease.phase=ZizhanPrmClockReleaseReady;lease.native_wire=lease.native_framework=lease.clock_mode=1;lease.framework_ready=lease.lease_held=lease.clock_held=lease.request_ack=lease.clock_request_ack=1;lease.sent=lease.acknowledged=3;
 const ZizhanAudioTransfer transfer={nullptr,[](void *,unsigned chip){assert(chip==callbacks++&&pages[0xf17c000][0]==0xc4&&pages[0xf101000][1]==3);return callback_fail?0:1;},nullptr};
 if(mode==39)zizhan_amp_native_pulse_run(&lease,&receipt,&transfer);else if(mode==38)zizhan_amp_native_iv_run(&lease,&receipt,&transfer);else if(mode==37)zizhan_amp_native_mailbox_run(&lease,&receipt,&transfer);else if(mode==36)zizhan_amp_native_output_off_run(&lease,&receipt,&transfer);else zizhan_amp_native_data_run(&lease,&receipt,test=="missing"?nullptr:&transfer);
 if(test=="unattended-pulse"){assert(!maps&&!native_calls&&!core_calls&&!callbacks&&receipt.release_safe);return 0;}
 if(test=="missing"){assert(!maps&&!native_calls&&!core_calls&&!callbacks&&receipt.release_safe);return 0;}
 assert(native_calls==1&&!legacy_calls&&receipt.reset_asserted&&pages[0xf101000][1]==0);
 if(test=="pulse")assert(receipt.pulse_verified&&receipt.iv_verified&&receipt.mailbox_verified&&receipt.output_configured);
 if(test=="iv")assert(receipt.iv_verified&&receipt.mailbox_verified&&receipt.output_configured);
 if(test=="mailbox")assert(receipt.mailbox_verified&&receipt.output_configured);
 if(test=="output-pass")assert(output_calls==1&&receipt.output_configured);
 if(test=="output-fail"){assert(output_calls==1&&!core_calls&&!callbacks&&!receipt.amp_pass&&!receipt.output_configured&&receipt.release_safe);return 0;}
 if(test=="pass"||test=="output-pass"||test=="mailbox"||test=="iv"||test=="pulse")assert(core_calls==1&&callbacks==2&&receipt.amp_pass&&receipt.release_safe&&receipt.data_connected&&receipt.data_restored&&pages[0xf17c000][0]==1&&pages[0xf179000][0]==1&&pages[0xf17b000][0]==1);
 else if(test=="old-core"||test=="foreign-pin"||test=="claim")assert(!core_calls&&!callbacks&&!receipt.amp_pass&&receipt.release_safe&&receipt.data_restored);
 else if(test=="restore")assert(core_calls==1&&callbacks==2&&receipt.data_connected&&!receipt.release_safe&&!receipt.data_restored&&!receipt.amp_pass);
 else assert(core_calls==1&&callbacks==1&&!receipt.amp_pass&&receipt.release_safe&&receipt.data_restored);
}
'''
with tempfile.TemporaryDirectory(prefix='zizhan-wire-platform-') as temp:
    d=Path(temp);cpp=d/'platform.cpp';cpp.write_text(harness+stubs+source+main);objects=[]
    for name in ('GeniI2c','ZizhanCs35l41'):
        obj=d/(name+'.o');subprocess.run([os.environ.get('CC','cc'),'-std=c11','-fsanitize=address,undefined','-c',str(R/'drivers/PongPower'/(name+'.c')),'-o',str(obj)],check=True,timeout=30);objects.append(str(obj))
    binary=d/'platform';subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(R/'drivers/PongPower'),str(cpp),*objects,'-o',str(binary)],check=True,timeout=30)
    for case in ('pass','missing','old-core','foreign-pin','claim','restore','callback','output-pass','output-fail','mailbox','iv','pulse','unattended-pulse'):
        subprocess.run([str(binary),case],check=True,timeout=10)
    # Reproduce the exact rejected dispatch before considering this a
    # regression test: the real owner must route mode35 through native RAM.
    dispatch='mode>=33&&mode<=40'
    assert source.count(dispatch)==1
    cpp.write_text(harness+stubs+source.replace(dispatch,'mode==33||mode==34')+main)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(R/'drivers/PongPower'),str(cpp),*objects,'-o',str(binary)],check=True,timeout=30)
    rejected=subprocess.run([str(binary),'pass'],capture_output=True,text=True,timeout=10)
    assert rejected.returncode!=0 and 'native_calls==1' in rejected.stderr
print('PASS actual mode35 SE15 owner/native-init dispatch/live GPIO124 callback/reset-before-restore/9controls and rejected-old-dispatch reproduction;chip proofs are controlled inputs,not hardware')
