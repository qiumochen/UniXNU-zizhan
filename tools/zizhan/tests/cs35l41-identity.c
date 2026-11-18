/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ZizhanCs35l41.h"
#include "ZizhanCs35l41TrimMap.h"
static int output_slot(uint32_t reg){const uint32_t regs[]={0x3810,0x3814,0x3808,0x4220,0x480c,0x4820,0x4c00,0x4c40,0x4c44,0x6c04,0x6000,0x3804,0x3800};for(unsigned i=0;i<13;++i)if(regs[i]==reg)return (int)i;return -1;}
typedef struct {
    GeniI2c bus;
    unsigned reads, writes, commands, fifo_writes, polls, pauses, reg;
    unsigned fail_command, stall_command, no_data_command, cancel_ack, abort_ack;
    unsigned active, stale, protocol_bad, packing_bad, idle_rx, reassert_after;
    uint64_t time;
    int frozen;
    uint32_t devid[2], revid[2];
    uint32_t otp[2], otp_error[2];
    uint32_t otp_id[2], state[2][3];
    unsigned state_reads[2], flip_after, clock_step;
    unsigned allow_value_writes, value_writes, tx_index, refuse_writes, global_change_command;
    unsigned hand_model, hand_irq, hand_samples, hand_clear, hand_mask, hand_delay, hand_refuse, hand_reassert, hand_poison;
    unsigned source_slave, source_dma, source_clear, source_auto, source_delay, source_refuse, source_reassert;
    unsigned write_order[4], order_count;
    unsigned config_model, override_at, override_value, config_samples, config_change;
    unsigned allow_patch, key_stage[2], patch_writes, patch_mismatch;
    uint32_t patch_values[2][6];
    unsigned allow_trim,trim_key_stage[2],trim_reads[2],trim_writes,trim_mismatch;
    uint32_t trim_values[2][100];ZizhanAmpTrimUpdate trim_updates[2][98];
    unsigned native_core_init,native_core_bad,native_core_writes[2];
    unsigned allow_ram,ram_mismatch,ram_value_writes,ram_sparse,read_index,short_fifo;
    unsigned coeff_mode,identity_bad;uint32_t coeff_unpacked[2][2];
    unsigned pulse_test,pulse_writes,pulse_commands[48];uint32_t pulse_volume[2],pulse_patch[2],pulse_pcm[2];
    unsigned play_fault; /* profile 19 test: 0x10010 reports a protection fault (bit 15) */
    unsigned iv_bad,iv_reads;
    unsigned mailbox_test,mailbox_fault,mailbox_count[2],mailbox_commands[2][12];uint32_t mailbox_mask[2],mailbox_irq[2],mailbox_state[2];
    unsigned run_mode,run_freeze,run_bad_mpu,run_fault,run_rate_writes,run_mpu_writes,run_core_writes;
    unsigned output_setup,output_writes,output_bad;uint32_t output_values[2][13];unsigned output_commands[28];
    uint32_t run_rates[2][16],run_mpu[2][20],run_heartbeat[2];
    unsigned run_observed,run_bad_index,run_profile_reads,run_profile_change;
    uint32_t run_pll[2];unsigned run_pll_writes[2],run_bad_pll,run_halo_state;
    unsigned run_lock;
    uint32_t run_monitor[2];unsigned run_monitor_writes[2],run_bad_monitor;
    unsigned run_format_profile,run_format_bad,run_format_written[2];uint32_t run_format_value[2];
    unsigned run_rx_profile,run_rx_bad,run_rx_writes;
    unsigned run_allow_global,run_global_writes,run_asp_enabled[2]; /* per amp: 0x4800 is a per-chip register (profile 19 keeps both prepared) */
    unsigned run_native_frame,run_bad_frame;uint32_t run_global_fs[2],run_lrclk_delay[2];
    unsigned run_native_rates,run_bad_native_rate,run_native_rate_written[2];
    unsigned run_calibration,run_bad_calibration,run_prerun_writes[2];uint32_t run_cal_r[2],run_cal_checksum[2],run_cal_status[2],run_us_bypass[2];
    uint32_t ram_values[2][7469];
    uint32_t request_registers[32768];
    uint8_t addresses[32768];
    uint32_t command;
} Fake;
static const uint32_t patch_regs[]={0x4100,0x4310,0x4400,0x381c,0x2bc20e0,0x2bc2020};
static int rate_slot(uint32_t reg){for(unsigned i=0;i<16;++i)if(reg==0x2b80080+(i%8)*8+(i>=8?0x200:0))return (int)i;return -1;}
static int debug_slot(uint32_t reg){for(unsigned i=0;i<15;++i)if(reg==zizhan_cs35l41_debug_register(i))return (int)i;return -1;}
static int mpu_slot(uint32_t reg){const uint32_t regs[]={0,4,8,12,20,24,28,32,36,44,48,52,56,60,68,72,76,80,84,92};
 for(unsigned i=0;i<20;++i){if(reg==0x2bc3000+regs[i])return (int)i;}return -1;}
static int ram_slot(uint32_t reg){
 const uint32_t base[]={0x02000000,0x0200009c,0x020003cc,0x02000a14,0x02c00000,0x03800000};
 const unsigned size[]={72,528,1596,1260,6000,20420};unsigned slot=0;
 for(unsigned i=0;i<6;++i){if(reg>=base[i]&&reg<base[i]+size[i]&&!(reg&3))return (int)(slot+(reg-base[i])/4);slot+=size[i]/4;}return -1;
}
static uint32_t swap(uint32_t v) { return (v & 255) << 24 | (v & 0xff00) << 8 | (v >> 8 & 0xff00) | v >> 24; }
static uint32_t rd(void *ctx, uint32_t reg) {
    Fake *f = ctx; ++f->reads;
    if (f->config_model) {
        if (reg == 0x40) ++f->config_samples;
        if (f->config_change && reg == GENI_M_IRQ_STATUS && f->config_samples >= 3 && f->bus.phase == GeniIdle) return 0;
        if (reg == f->override_at && f->override_at) return f->override_value;
        switch (reg) {
        case GENI_FW_REVISION: case 0x6c: return 0x303;
        case GENI_M_IRQ_EN: return GENI_M_IRQ_WANTED;
        case 0x644: return 0x03001e36;
        case 0x810: return 15;
        case 0x814: return 16;
        case 0xc48: return 0xd;
        case 0xd48: return 0x1d;
        case 0xe30: return 15;
        case 0x908: return 7;
        default: break;
        }
    }
    switch (reg) {
    case GENI_FW_REVISION: return (f->protocol_bad ? 1 : GENI_PROTO_I2C) << 8;
    case GENI_CLK_CFG: return 1;
    case GENI_SCL_COUNTERS: return 10;
    case GENI_TX_PACK0: case GENI_RX_PACK0: return f->packing_bad ? 0 : 0x7f8fe;
    case GENI_TX_PACK1: case GENI_RX_PACK1: return 0xffefe;
    case 0x40: return f->active | (f->hand_model && f->hand_poison == 1 && f->hand_samples ? 1u : 0u);
    case 0x640:
        if (f->hand_model == 2 && f->source_clear && !f->source_refuse && f->hand_samples > f->source_delay) f->source_slave = 0;
        if (f->hand_model == 2 && f->source_reassert && f->hand_samples >= 2) f->source_slave = 0x08000000;
        return f->source_slave;
    case 0xd40: return f->source_dma;
    case 0x810: return 2;
    case 0x814: return 14;
    case GENI_M_IRQ_STATUS:
        ++f->polls;
        if (f->hand_model && (!f->config_model || f->bus.phase == GeniIdle)) {
            ++f->hand_samples;
            if (f->hand_model == 2 && f->source_slave) f->hand_irq |= 0x80;
            else if (f->hand_model == 2 && f->source_auto) f->hand_irq &= ~0x80u;
            if (f->hand_clear && !f->hand_refuse && f->hand_samples > f->hand_delay) f->hand_irq = 0;
            if (f->hand_reassert && f->hand_samples >= 3) f->hand_irq = 0x04000080;
            if (f->hand_poison == 2 && f->hand_samples > 1) f->hand_irq |= GENI_M_GP_IRQ;
            return f->hand_irq;
        }
        if (f->bus.phase == GeniIdle) return f->stale | (f->reassert_after && f->commands >= f->reassert_after ? GENI_M_CMD_DONE : 0);
        if (f->bus.phase == GeniCancelling) return f->cancel_ack ? GENI_M_CMD_CANCEL : 0;
        if (f->bus.phase == GeniAborting) return f->abort_ack ? GENI_M_CMD_ABORT : 0;
        if (f->commands == f->stall_command) return 0;
        if (f->commands == f->fail_command) return GENI_M_GP_IRQ | GENI_M_CMD_DONE;
        if (f->bus.phase == GeniTxWait) return GENI_M_TX_WATERMARK;
        return GENI_M_CMD_DONE;
    case GENI_RX_FIFO_STATUS:
        if (f->bus.phase == GeniIdle) return f->idle_rx | (f->hand_model && f->hand_poison == 3 && f->hand_samples > 1 ? 1u : 0u);
        if(f->allow_ram&&f->bus.rx_length>4&&f->bus.phase==GeniReadDone&&f->commands!=f->no_data_command)return f->bus.rx_length/4-(f->short_fifo?1:0);
        return f->bus.phase == GeniReadDone && f->commands != f->no_data_command ? 1 : 0;
    case GENI_RX_FIFO: {
        const unsigned i = f->bus.address == 0x40 ? 0 : 1;
        if(f->output_setup&&output_slot(f->reg)>=0){uint32_t v=f->output_values[i][output_slot(f->reg)];if(f->output_bad&&f->output_writes==1&&f->reg==0x3810)v^=1;return swap(v);}
        if(f->run_mode){
            if(f->pulse_test&&f->reg==0x6000)return swap(f->pulse_volume[i]);
            if(f->pulse_test&&f->reg==0x2084)return swap(f->pulse_patch[i]);
            if(f->pulse_test&&f->reg==0x4c00)return swap(f->pulse_pcm[i]);
            if(f->reg==0x4c48||f->reg==0x4c4c){++f->iv_reads;return swap((f->reg==0x4c48?0x18u:0x19u)^((f->iv_bad==(f->reg==0x4c48?1u:2u))?1u:0u));}
            if(f->reg==0x10914)return swap(f->mailbox_mask[i]^(f->mailbox_fault==4?1u:0u));
            if(f->reg==0x10014&&f->mailbox_test)return swap(f->mailbox_irq[i]);
            if(f->reg==0x13004&&f->mailbox_test)return swap(f->mailbox_state[i]);
            if(rate_slot(f->reg)>=0){const unsigned at=(unsigned)rate_slot(f->reg);
                if(f->run_native_rates&&f->run_mpu_writes>=(i+1)*23&&!(f->run_native_rate_written[i]&(1u<<at)))f->run_rates[i][at]|=0xa5000000u;
                uint32_t v=f->run_rates[i][at];if(f->run_bad_native_rate==at+1&&(f->run_native_rate_written[i]&(1u<<at)))v^=1;
                return swap(v);}
            if(mpu_slot(f->reg)>=0){
                unsigned at=(unsigned)mpu_slot(f->reg);++f->run_profile_reads;
                const uint32_t observed[]={3,1,0xff0000,0xfffff,0,0,0,0,15,0,0,0,0,0,0,0,0,0,0,0};
                uint32_t value=f->run_observed?observed[at]:f->run_mpu[i][at];
                if(f->run_bad_mpu||(f->run_bad_index&&at==f->run_bad_index-1)||(f->run_profile_change&&f->run_profile_reads>20))value^=1;
                return swap(value);
            }
            if(f->reg==0x28007d0)return swap(f->state[i][2]&1?(f->run_halo_state?f->run_halo_state:2):0);
            if(f->reg==0x28007d4){if((f->state[i][2]&1)&&!f->run_freeze)++f->run_heartbeat[i];return swap(f->run_heartbeat[i]);}
            if(f->reg==0x2c04)return swap(f->run_pll[i]^((f->run_bad_pll&&f->run_pll_writes[i]==f->run_bad_pll)?0x20u:0));
            if(f->reg==0x2d10)return swap(f->run_monitor[i]^((f->run_bad_monitor&&f->run_monitor_writes[i])?1u:0u));
            if(f->reg==0x14000)return swap(0x1234);
            if(f->reg==0x4808)return swap(f->run_format_value[i]^((f->run_format_bad&&f->run_format_written[i])?0x10000u:0u));
            if(f->reg==0x4800)return swap(f->run_asp_enabled[i]^((f->run_rx_bad&&f->run_rx_writes==f->run_rx_bad)?1u:0u));
            if(f->reg==0x2c0c)return swap(f->run_global_fs[i]);
            if(f->reg==0x2800804)return swap(f->run_lrclk_delay[i]^(f->run_bad_frame?1u:0u));
            if(f->reg==0x2800800)return swap(f->run_us_bypass[i]);
            if(f->reg==0x2800268)return swap(f->run_cal_r[i]^(f->run_bad_calibration?1u:0u));
            if(f->reg==0x2800274){return swap(f->run_cal_checksum[i]);}if(f->reg==0x2800270)return swap(f->run_cal_status[i]);
            if(debug_slot(f->reg)>=0)return swap(0x40000000u+(unsigned)debug_slot(f->reg)+((f->state[i][2]&1)?0x10000u:0));
            if(f->reg>=0x10090&&f->reg<=0x1009c)return swap(f->reg==0x10098&&f->run_lock?2u:0u);
            if(f->reg>=0x10010&&f->reg<=0x1001c)return swap(f->play_fault&&f->reg==0x10010?0x8000u:0u);
            if(f->reg==0x2800258||f->reg==0x13004||f->reg==0x2c08)return 0;
            if(f->reg==0x2bc3104||f->reg==0x2bc310c||f->reg==0x2bc3114||f->reg==0x2bc7008)return f->run_fault?swap(1):0;
        }
        if(f->allow_ram&&f->reg>=0x02800000&&f->reg<0x02800060){
            static const uint32_t identity[]={0x40000,0x30000,2,0x400a4,0x2b01,0x1f4,0xe,0,0,2,0xcd,0x1d3f01,0x94,0x50,0xe,0x7bc,0xf20b,0x100,0x176,0x4c,0,0,0xbedead,0};
            unsigned word=(f->reg-0x02800000)/4;
            return swap(f->coeff_mode?(identity[word]^(f->identity_bad&&i==1&&word==12?1u:0u)):0xabc000+word);
        }
        if(f->coeff_mode&&f->reg>=0x03400090&&f->reg<0x03400098)return f->coeff_unpacked[i][(f->reg-0x03400090)/4]^(f->ram_mismatch?1u:0u);
        if(f->allow_ram&&ram_slot(f->reg)>=0){
            unsigned index=f->read_index++;
            if(f->ram_sparse&&f->bus.rx_length==4){unsigned stride=f->reg>=0x03800000?20:12;if((f->reg&0xffff)%stride)return 0;}
            return f->ram_values[i][ram_slot(f->reg+index*4)]^(f->ram_mismatch?1u:0u);
        }
        uint32_t value;
        if (f->reg == 0x10) value = f->otp_id[i];
        else if (f->reg == 0x2014 || f->reg == 0x2018 || f->reg == 0x2bc1000) {
            unsigned at = f->reg == 0x2014 ? 0 : f->reg == 0x2018 ? 1 : 2;
            ++f->state_reads[i]; value = f->state[i][at];
            if(at==2&&f->native_core_bad&&f->native_core_writes[i])value^=0x100;
            if (at == 0 && f->global_change_command && f->commands >= f->global_change_command) value |= 1;
            if (f->flip_after && f->state_reads[i] > 3 && at == 2) value ^= 0x100;
        } else if (f->reg >= 0x400 && f->reg <= 0x47c) value = ((f->reg - 0x400) / 4 * 0x1234567u) ^ (0xa5a55a5au + i);
        else value = f->reg == 0x1001c ? f->otp[i] : f->reg == 0x10018 ? f->otp_error[i] :
                     f->reg ? f->revid[i] : f->devid[i];
        if(f->allow_trim&&zizhan_amp_trim_register(f->reg)){
            assert(f->trim_key_stage[i]==2);unsigned field=f->trim_reads[i]/2;
            assert(field<98&&f->trim_updates[i][field].reg==f->reg);
            unsigned slot=0;while(zizhan_amp_trim8[slot].reg!=f->reg)++slot;
            value=f->trim_values[i][slot];if(f->trim_mismatch&&(f->trim_reads[i]&1))value^=1;
            ++f->trim_reads[i];
        }
        for(unsigned n=0;n<6;++n)if(f->reg==patch_regs[n]){
            assert(f->allow_patch&&f->key_stage[i]==2);value=f->patch_values[i][n];
            if(f->patch_mismatch)value^=1;
        }
        return swap(value);
    }
    default: return 0;
    }
}
static void wr(void *ctx, uint32_t reg, uint32_t value) {
    Fake *f = ctx; ++f->writes;
    // Strict allowlist of GENI transaction registers: no codec values,
    // controller preparation, clocks, pinmux, reset or amplifier enable.
    assert(reg == GENI_TX_LEN || reg == GENI_M_CMD0 || reg == GENI_TX_WATERMARK ||
           reg == GENI_TX_FIFO || reg == GENI_M_IRQ_CLEAR || reg == GENI_RX_LEN || reg == GENI_M_CMD_CTRL ||
           (f->hand_model == 2 && reg == 0x648));
    if (f->hand_model == 2 && reg == 0x648) {
        assert(value == 0x08000000 && f->source_slave == value && !f->source_clear);
        ++f->source_clear; f->write_order[f->order_count++] = reg;
    }
    if (f->hand_model && (!f->config_model || f->bus.phase == GeniIdle) && reg == GENI_M_IRQ_CLEAR) {
        if (f->hand_model == 2) { assert(!f->source_slave); f->write_order[f->order_count++] = reg; }
        assert(value && !(value & ~0x04000080u) && value == f->hand_irq);
        ++f->hand_clear; f->hand_mask = value;
    }
    if (reg == GENI_RX_LEN){assert(value==4||(f->allow_ram&&(value==12||value==20)));f->read_index=0;}
    if (reg == GENI_TX_LEN) assert(value == 4 || (f->allow_value_writes && value == 8));
    if (reg == GENI_M_CMD0) {
        unsigned opcode = value >> 27, address = (value >> 9) & 127;
        assert(address == 0x40 || address == 0x42);
        if (opcode == GENI_OP_WRITE) {
            assert(!!(value & GENI_STOP_STRETCH) == !!f->bus.reading);
            assert(f->bus.reading || f->allow_value_writes);
            f->tx_index = 0; ++f->commands;
            f->addresses[f->commands - 1] = address;
        } else assert(opcode == GENI_OP_READ && !(value & GENI_STOP_STRETCH));
        f->command = value;
    }
    if (reg == GENI_TX_FIFO) {
        if (f->tx_index++ == 1) {
            assert(f->allow_value_writes && !f->bus.reading && f->bus.length == 8);
            const unsigned chip = f->bus.address == 0x40 ? 0 : 1;
            if(f->output_setup&&output_slot(f->reg)>=0){assert(f->output_writes<28&&!(f->state[chip][0]&1)&&!(f->state[chip][1]&0x31)&&f->state[chip][2]==0);f->output_commands[f->output_writes++]=f->commands;f->output_values[chip][output_slot(f->reg)]=swap(value);++f->fifo_writes;return;}
            if(f->run_mode){
                uint32_t v=swap(value);int rate=rate_slot(f->reg),mpu=mpu_slot(f->reg);
                if(f->pulse_test&&(f->reg==0x6000||f->reg==0x2084||f->reg==0x40||f->reg==0x2018||f->reg==0x4c00)){
                    assert(f->pulse_writes<48);f->pulse_commands[f->pulse_writes++]=f->commands;
                    if(f->reg==0x6000){assert(v==0x267c||v==0x3f44);f->pulse_volume[chip]=v;}
                    else if(f->reg==0x2084){assert(v==0x2f1aa0||v==0x2f1aa3);f->pulse_patch[chip]=v;}
                    else if(f->reg==0x40)assert(v==0x55||v==0xaa||v==0xcc||v==0x33);
                    else if(f->reg==0x4c00){assert(v==0||(v==8&&!(f->state[chip][1]&1)));f->pulse_pcm[chip]=v;}
                    else{assert(f->state[chip][0]==1&&f->state[chip][2]==1&&(v==0x3300||v==0x3320||v==0x3321));f->state[chip][1]=v;}
                }else if(f->mailbox_test&&(f->reg==0x10814||f->reg==0x10014||f->reg==0x10914||f->reg==0x13020)){
                    assert(f->state[chip][2]==1&&f->state[chip][0]==1&&!(f->state[chip][1]&0x31));
                    assert(f->mailbox_count[chip]<12);f->mailbox_commands[chip][f->mailbox_count[chip]++]=f->commands;
                    
            if(f->reg==0x10914)f->mailbox_mask[chip]=v;
                    else if(f->reg==0x10014){assert(v==0x200000);if(f->mailbox_fault!=3)f->mailbox_irq[chip]&=~v;}
                    else if(f->reg==0x10814)assert(v==0x100000);
                    else{assert((v==1||v==2)&&!(f->mailbox_mask[chip]&0x100000));if(f->mailbox_fault!=1)f->mailbox_irq[chip]|=0x200000;f->mailbox_state[chip]=f->mailbox_fault==2?7u:v==1?1u:0u;}
                }else if(rate>=0){
                    if(v!=1){assert(f->run_native_rates&&v==(f->run_rates[chip][rate]&~0x1fu)&&f->run_mpu_writes>=(chip+1)*23&&!(f->state[chip][2]&1)&&!(f->state[chip][1]&0x31)&&f->run_asp_enabled[chip]==(f->run_rx_profile?0x10000u:0u));f->run_native_rate_written[chip]|=1u<<rate;}
                    f->run_rates[chip][rate]=v;++f->run_rate_writes;}
                else if(mpu>=0){assert(v==0xffffffff);f->run_mpu[chip][mpu]=v;++f->run_mpu_writes;}
                else if(f->reg==0x2bc3140){assert(v==0x5555||v==0xaaaa||v==0);++f->run_mpu_writes;}
                else if(f->reg==0x2800800||f->reg==0x2800268||f->reg==0x2800274||f->reg==0x2800270){
                    assert(f->run_calibration&&!f->state[chip][0]&&!(f->state[chip][1]&0x31)&&!f->run_asp_enabled[chip]);
                    unsigned n=f->run_prerun_writes[chip];
                    if(f->reg==0x2800800){assert(!chip&&n==0&&v==1);f->run_us_bypass[chip]=v;}
                    else if(f->reg==0x2800268){assert(n==(chip?0u:1u)&&v>0&&v<0xffffffu);f->run_cal_r[chip]=v;}
                    else if(f->reg==0x2800274){assert(n==(chip?1u:2u)&&v==f->run_cal_r[chip]+1);f->run_cal_checksum[chip]=v;}
                    else{assert(n==(chip?2u:3u)&&v==1);f->run_cal_status[chip]=v;}
                    ++f->run_prerun_writes[chip];
                }else if(f->reg==0x2c0c){assert(f->run_native_frame&&v==((f->run_global_fs[chip]&~31u)|4u)&&!f->run_asp_enabled[chip]&&!f->state[chip][0]);f->run_global_fs[chip]=v;}
                else if(f->reg==0x2800804){assert(f->run_native_frame&&v==32&&!f->run_asp_enabled[chip]&&!f->state[chip][0]);f->run_lrclk_delay[chip]=v;}
                else if(f->reg==0x4808){assert(f->run_format_profile&&v==((f->run_format_value[chip]&~0xff000755u)|0x20000000u)&&!f->run_format_written[chip]&&!f->state[chip][0]&&!(f->state[chip][1]&0x31)&&!f->run_asp_enabled[chip]);f->run_format_value[chip]=v;++f->run_format_written[chip];}
                else if(f->reg==0x4800){assert(f->run_rx_profile&&(v==0x10000||v==0)&&!(f->state[chip][1]&0x31)&&!(f->state[chip][2]&1));assert(v==0||(!f->run_asp_enabled[chip]&&f->run_pll_writes[chip]==6));f->run_asp_enabled[chip]=v;++f->run_rx_writes;}
                else if(f->reg==0x2014){assert(f->run_allow_global&&(v==0||v==1)&&!(f->state[chip][1]&0x31)&&(f->run_asp_enabled[chip]==0||(f->run_rx_profile&&v==0&&f->run_asp_enabled[chip]==0x10000))&&!(f->state[chip][2]&1));f->state[chip][0]=v;++f->run_global_writes;}
                else if(f->reg==0x2d10){assert(v==0x24010&&!f->run_monitor_writes[chip]&&!f->run_pll_writes[chip]);f->run_monitor[chip]=v;++f->run_monitor_writes[chip];}
                else if(f->reg==0x2c04){
                    const uint32_t sequence[]={0x810,0xe70,0xe60,0xe60,0x660,0x670};
                    assert(f->run_pll_writes[chip]<6&&v==sequence[f->run_pll_writes[chip]]);
                    assert(!(f->state[chip][0]&1)&&!(f->state[chip][1]&0x31)&&!(f->state[chip][2]&1));
                    f->run_pll[chip]=v;++f->run_pll_writes[chip];
                }else{assert(f->reg==0x2bc1000&&(v==(f->native_core_init?0u:0x100u)||v==(f->native_core_init?1u:0x101u)));assert((!(f->state[chip][0]&1)||f->run_allow_global)&&!(f->state[chip][1]&0x31)&&f->run_asp_enabled[chip]==(f->run_rx_profile?0x10000u:0u));f->state[chip][2]=v;++f->run_core_writes;}
                ++f->fifo_writes;return;
            }
            if(f->native_core_init&&f->reg==0x2bc1000){assert(swap(value)==0&&!f->native_core_writes[chip]&&!f->state[chip][0]&&!(f->state[chip][1]&0x31)&&!(f->state[chip][2]&1));f->state[chip][2]=0;++f->native_core_writes[chip];++f->fifo_writes;return;}
            if(f->coeff_mode&&f->reg>=0x03400090&&f->reg<0x03400098){assert(chip==1);f->coeff_unpacked[chip][(f->reg-0x03400090)/4]=value;++f->ram_value_writes;++f->fifo_writes;return;}
            if(f->allow_ram&&ram_slot(f->reg)>=0){f->ram_values[chip][ram_slot(f->reg)]=value;++f->ram_value_writes;++f->fifo_writes;return;}
            if(f->allow_trim&&(f->reg==0x40||zizhan_amp_trim_register(f->reg))){
                const uint32_t v=swap(value);
                if(f->reg==0x40){unsigned *stage=&f->trim_key_stage[chip];
                    assert((v==0x55&&*stage==0)||(v==0xaa&&*stage==1)||(v==0xcc&&*stage==2)||(v==0x33&&*stage==3));++*stage;
                }else{
                    assert(f->trim_key_stage[chip]==2);unsigned field=f->trim_reads[chip]/2;
                    assert(field<98&&f->trim_updates[chip][field].reg==f->reg&&(f->trim_reads[chip]&1));
                    unsigned slot=0;while(zizhan_amp_trim8[slot].reg!=f->reg)++slot;
                    ZizhanAmpTrimUpdate u=f->trim_updates[chip][field];
                    assert(v==((f->trim_values[chip][slot]&~u.mask)|(u.value&u.mask)));
                    f->trim_values[chip][slot]=v;++f->trim_writes;
                }
                ++f->fifo_writes;return;
            }
            if(f->allow_patch && f->reg!=0x2018 && f->reg!=0x2bc1000){
                const uint32_t v=swap(value);
                if(f->reg==0x40){
                    unsigned *stage=&f->key_stage[chip];
                    assert((v==0x5555&&*stage==0)||(v==0xaaaa&&*stage==1)||(v==0xcccc&&*stage==2)||(v==0x3333&&*stage==3));
                    ++*stage;
                }else{
                    assert(f->key_stage[chip]==2);unsigned found=0;
                    for(unsigned n=0;n<6;++n)if(f->reg==patch_regs[n]){assert(v==(n==3?0x51u:0u));f->patch_values[chip][n]=v;found=1;}
                    assert(found);
                }
                ++f->patch_writes;++f->fifo_writes;return;
            }
            assert(f->reg == 0x2018 || f->reg == 0x2bc1000);
            const unsigned at = f->reg == 0x2018 ? 1 : 2;
            const uint32_t mask = at == 1 ? 0x31 : 1;
            assert(swap(value) == (f->state[chip][at] & ~mask));
            assert(!(f->state[chip][0] & 1));
            ++f->value_writes; ++f->fifo_writes;
            if (!f->refuse_writes) f->state[chip][at] = swap(value);
            return;
        }
        const uint32_t requested = swap(value);
        assert((f->output_setup&&output_slot(requested)>=0)||requested == 0 || requested == 4 || requested == 0x1001c || requested == 0x10018 ||
               (f->run_mode&&(((requested>=0x10010&&requested<=0x1001c)||(requested>=0x10090&&requested<=0x1009c))||debug_slot(requested)>=0||requested==0x2800800||requested==0x2800268||requested==0x2800274||requested==0x2800270||requested==0x2c0c||requested==0x2800804||requested==0x4800||requested==0x4808||requested==0x2d10||requested==0x14000||rate_slot(requested)>=0||mpu_slot(requested)>=0||requested==0x2bc3140||requested==0x28007d0||requested==0x28007d4||requested==0x2800258||requested==0x13004||requested==0x10914||requested==0x6000||requested==0x2084||requested==0x40||requested==0x4c48||requested==0x4c4c||requested==0x4c00||requested==0x4820||requested==0x6c04||requested==0x201c||requested==0x2020||requested==0x2024||requested==0x4804||requested==0x10814||requested==0x13020||requested==0x2c04||requested==0x2c08||requested==0x2bc3104||requested==0x2bc310c||requested==0x2bc3114||requested==0x2bc7008)) ||
               (f->allow_ram&&ram_slot(requested)>=0) ||
               (f->allow_ram&&requested>=0x02800000&&requested<0x02800060&&!(requested&3)) ||
               (f->coeff_mode&&requested>=0x03400090&&requested<0x03400098&&!(requested&3)) ||
               (f->allow_trim&&(requested==0x40||zizhan_amp_trim_register(requested))) ||
               (f->allow_patch&&(requested==0x40||requested==0x4100||requested==0x4310||requested==0x4400||requested==0x381c||requested==0x2bc20e0||requested==0x2bc2020)) ||
               requested == 0x10 || requested == 0x2014 || requested == 0x2018 || requested == 0x2bc1000 ||
               (requested >= 0x400 && requested <= 0x47c && !(requested & 3)));
        f->reg = requested; f->request_registers[f->commands - 1] = requested; ++f->fifo_writes;
    }
    if (reg == GENI_M_CMD_CTRL) assert(value == GENI_CMD_CTRL_CANCEL || value == GENI_CMD_CTRL_ABORT);
}
static uint64_t now(void *ctx) { Fake *f = ctx; if (f->commands > 8 && f->bus.phase == GeniIdle) f->time += f->clock_step; return f->frozen ? 0 : f->time; }
static void pause_us(void *ctx, unsigned us) { Fake *f = ctx; assert(us == 20 || us == 1000 || (f->run_mode&&us==10000)); ++f->pauses; f->time += us; }
static void init(Fake *f) {
    memset(f, 0, sizeof *f); f->bus.io = (GeniIo){rd, wr, f}; f->bus.address = 0x55;
    f->devid[0] = 0x35a40; f->devid[1] = 0x35b40; f->revid[0] = 0xb2; f->revid[1] = 0xb1;
    f->otp[0] = f->otp[1] = 2; f->otp_id[0] = 1; f->otp_id[1] = 8;
}
static int identify(Fake *f, unsigned enabled, unsigned prerequisites, ZizhanAmpIdentity *out) {
    const ZizhanAmpClock clock = {f, now, pause_us};
    return zizhan_cs35l41_identify(enabled, prerequisites, &f->bus, &clock, out);
}
static unsigned prepare_trim(Fake *f,ZizhanAmpSnapshot *snap,ZizhanAmpB2Patch *patch){
    init(f);f->revid[1]=0xb2;f->devid[1]=0x35a40;f->allow_value_writes=1;f->otp_id[0]=f->otp_id[1]=8;
    f->state[0][1]=f->state[1][1]=0x3321;f->state[0][2]=f->state[1][2]=0x101;
    const unsigned flags=ZizhanAmpExclusiveSe15|ZizhanAmpPinsVerified|ZizhanAmpResetReleased|ZizhanAmpUnmanagedSupplyTopology;
    const ZizhanAmpClock c={f,now,pause_us};ZizhanAmpSafeOff off;ZizhanAmpReadiness ready;
    assert(!zizhan_cs35l41_safe_off(1,flags,&f->bus,&c,&off));
    f->config_model=1;f->hand_model=2;f->hand_irq=0x80;f->source_slave=0x08000000;
    assert(!zizhan_cs35l41_owned_snapshot(1,flags,&f->bus,&c,&off,&ready,snap)&&snap->status==ZizhanAmpPass);
    f->allow_patch=1;assert(!zizhan_cs35l41_b2_patch(1,flags,&f->bus,&c,snap,patch)&&patch->status==ZizhanAmpPass);
    f->allow_trim=1;
    for(unsigned chip=0;chip<2;++chip){
        assert(zizhan_amp_decode_trim8(8,snap->words[chip],f->trim_updates[chip]));
        for(unsigned i=0;i<100;++i)f->trim_values[chip][i]=0xa5a55a5a;
    }
    return f->commands;
}
int main(void) {
    Fake f; ZizhanAmpIdentity out;
    for (unsigned mode = 0; mode < 3; mode += 2) {
        init(&f); assert(!identify(&f, mode, 15, &out) && out.status == ZizhanAmpSkip);
        assert(!f.reads && !f.writes && !f.pauses);
    }
    for (unsigned flags = 0; flags < 15; ++flags) {
        init(&f); assert(!identify(&f, 1, flags, &out) && out.reason == ZizhanAmpPrerequisite);
        assert(!f.reads && !f.writes);
    }
    init(&f); assert(!identify(&f, 1, 15, &out) && out.status == ZizhanAmpPass && out.completed == 2);
    assert(out.devid[0] == 0x35a40 && out.devid[1] == 0x35b40 && out.revid[0] == 0xb2 && out.revid[1] == 0xb1);
    assert(f.commands == 4 && f.fifo_writes == 4 && f.bus.address == 0x55);
    assert(f.addresses[0] == 0x40 && f.addresses[1] == 0x40 && f.addresses[2] == 0x42 && f.addresses[3] == 0x42);
    for (unsigned test = 0; test < 5; ++test) {
        init(&f);
        if (test == 0) f.bus.phase = GeniTxWait;
        if (test == 1) f.protocol_bad = 1;
        if (test == 2) f.packing_bad = 1;
        if (test == 3) f.active = 1;
        if (test == 4) f.stale = GENI_M_CMD_DONE;
        assert(!identify(&f, 1, 15, &out) && out.status == ZizhanAmpSkip && !f.writes);
    }
    for (unsigned command = 1; command <= 4; ++command) {
        init(&f); f.fail_command = command;
        assert(identify(&f, 1, 15, &out) == ZizhanAmpBus && out.status == ZizhanAmpFail);
        assert(out.controller_uncertain && f.commands == command && out.completed == (command > 2));
    }
    for (unsigned test = 0; test < 4; ++test) {
        init(&f); f.stall_command = 1;
        if (test == 1) f.cancel_ack = 1;
        if (test == 2) f.abort_ack = 1;
        if (test == 3) f.frozen = 1;
        assert(identify(&f, 1, 15, &out) == ZizhanAmpTimeout && out.controller_uncertain && f.commands == 1);
        assert(f.pauses <= 1200 && !out.completed);
        if (!f.cancel_ack && !f.abort_ack) assert(f.bus.phase == GeniAborting); // never fake Idle
    }
    init(&f); f.no_data_command = 1;
    assert(identify(&f, 1, 15, &out) == ZizhanAmpBus && out.bus_error == GeniErrNoData && out.controller_uncertain);
    init(&f); f.devid[1] = 0x35a40;
    assert(identify(&f, 1, 15, &out) == ZizhanAmpWrongIdentity && out.completed == 1 && !out.controller_uncertain);
    assert(f.bus.address == 0x55);
    init(&f); f.devid[0] = 0;
    assert(identify(&f, 1, 15, &out) == ZizhanAmpWrongIdentity && !out.completed);
    assert(zizhan_cs35l41_identify(1, 15, NULL, NULL, &out) == ZizhanAmpArgument);
    const unsigned boot_flags = ZizhanAmpExclusiveSe15 | ZizhanAmpPinsVerified | ZizhanAmpResetReleased | ZizhanAmpUnmanagedSupplyTopology;
    for (unsigned test = 0; test < 6; ++test) {
        init(&f); const ZizhanAmpClock clock = {&f,now,pause_us};
        if(test==1)f.otp[0]=0;
        if(test==2)f.otp_error[0]=0x80000000;
        if(test==3)f.otp_error[1]=0x80000000;
        if(test==4)f.fail_command=1;
        if(test==5){f.otp[0]=0;f.frozen=1;}
        const int rc=zizhan_cs35l41_boot_identify(1,boot_flags,&f.bus,&clock,&out);
        if(test==0)assert(!rc&&out.status==ZizhanAmpPass&&out.digital_ready==2&&out.completed==2&&f.commands==8);
        else if(test==1||test==5)assert(rc==ZizhanAmpOtpTimeout&&!out.digital_ready&&!out.completed&&f.commands<=64);
        else if(test==2||test==3)assert(rc==ZizhanAmpOtpError&&out.digital_ready==(test==3)&&out.completed==(test==3));
        else assert(rc==ZizhanAmpBus&&out.controller_uncertain);
    }
    init(&f);const ZizhanAmpClock clock={&f,now,pause_us};
    assert(!zizhan_cs35l41_boot_identify(1,15,&f.bus,&clock,&out)&&out.status==ZizhanAmpSkip&&!f.reads&&!f.writes);
    // Snapshot invokes actual production GENI and preserves distinct chip
    // input, BE32 order, before/after state and exact partial counts.
    ZizhanAmpSnapshot snap;
    for (unsigned test = 0; test < 2; ++test) {
        init(&f); f.revid[1] = 0xb2; f.devid[1] = 0x35a40;
        if (test) f.state[0][2] = f.state[1][2] = 0x101;
        assert(!zizhan_cs35l41_snapshot(1, boot_flags, &f.bus, &clock, &snap));
        assert(snap.status == ZizhanAmpPass && snap.completed == 2 && f.commands == 86 && f.fifo_writes == 86);
        assert(f.bus.address == 0x55 && snap.otp_id[0] == 1 && snap.otp_id[1] == 8);
        for (unsigned chip = 0; chip < 2; ++chip) {
            assert(snap.word_count[chip] == 32 && snap.before_count[chip] == 3 && snap.after_count[chip] == 3);
            assert(snap.output_disabled[chip] && snap.boost_disabled[chip] && snap.dsp_stopped[chip] == !test);
            for (unsigned word = 0; word < 32; ++word) assert(snap.words[chip][word] == (word * 0x1234567u ^ (0xa5a55a5au + chip)));
        }
    }
    for (unsigned command = 1; command <= 86; ++command) {
        init(&f); f.revid[1] = 0xb2; f.devid[1] = 0x35a40; f.fail_command = command;
        assert(zizhan_cs35l41_snapshot(1, boot_flags, &f.bus, &clock, &snap) == ZizhanAmpBus);
        assert(snap.status == ZizhanAmpFail && snap.identity.controller_uncertain && f.commands == command);
        if (command >= 13 && command <= 44) assert(snap.word_count[0] == command - 13);
        if (command >= 52 && command <= 83) assert(snap.word_count[1] == command - 52);
        assert(snap.completed == (command > 47));
    }
    for (unsigned test = 0; test < 8; ++test) {
        init(&f); f.revid[1] = 0xb2; f.devid[1] = 0x35a40;
        if (test == 0) f.state[0][0] = 1;
        if (test == 1) f.state[0][1] = 1;
        if (test == 2) f.state[1][1] = 0x20;
        if (test == 3) f.otp_id[0] = 9;
        if (test == 4) f.revid[1] = 0xb4;
        if (test == 5) f.flip_after = 1;
        if (test == 6) f.clock_step = 100000;
        if (test == 7) { f.stall_command = 13; f.frozen = 1; }
        int rc = zizhan_cs35l41_snapshot(1, boot_flags, &f.bus, &clock, &snap);
        assert(rc && snap.status == ZizhanAmpFail);
        assert(rc == (test < 3 ? ZizhanAmpOutputActive : test == 3 ? ZizhanAmpUnsupportedOtp :
                      test == 4 ? ZizhanAmpWrongIdentity : test == 5 ? ZizhanAmpStateChanged :
                      test == 6 ? ZizhanAmpSnapshotTimeout : ZizhanAmpTimeout));
        if (test < 2 || test == 3) assert(!snap.word_count[0]);
        if (test == 7) assert(snap.identity.controller_uncertain && !snap.word_count[0] && f.pauses <= 1248);
    }
    for (unsigned test = 0; test < 3; ++test) {
        init(&f); assert(!zizhan_cs35l41_snapshot(test == 1 ? 1 : test, test == 1 ? 15 : boot_flags, &f.bus, &clock, &snap));
        assert(snap.status == ZizhanAmpSkip && !f.reads && !f.writes);
    }
    ZizhanAmpSafeOff off;
    unsigned successful_commands = 0;
    for (unsigned test = 0; test < 3; ++test) {
        init(&f); f.revid[1] = 0xb2; f.devid[1] = 0x35a40; f.allow_value_writes = 1;
        for (unsigned chip = 0; chip < 2; ++chip) {
            f.state[chip][0] = 0x80000000; // non-enable bits must be preserved
            f.state[chip][1] = test == 1 ? 0x3300 : (0xa5a53321u + chip * 2);
            f.state[chip][2] = test == 1 ? 0x100 : (0x5a5a0101u + chip * 2);
        }
        uint32_t original[2][3]; memcpy(original, f.state, sizeof original);
        if (test == 2) f.frozen = 1;
        assert(!zizhan_cs35l41_safe_off(1, boot_flags, &f.bus, &clock, &off));
        assert(off.status == ZizhanAmpPass && off.completed == 2 && f.bus.address == 0x55);
        assert(off.attempted_writes == (test == 1 ? 0 : 4) && off.acknowledged_writes == off.attempted_writes);
        assert(f.value_writes == off.attempted_writes);
        for (unsigned chip = 0; chip < 2; ++chip) {
            assert(off.before_count[chip] == 3 && off.after_count[chip] == 3);
            assert(f.state[chip][0] == original[chip][0]);
            assert(f.state[chip][1] == (original[chip][1] & ~0x31u));
            assert(f.state[chip][2] == (original[chip][2] & ~1u));
        }
        if (test == 0) {
            successful_commands = f.commands; assert(successful_commands == 36);
            assert(!zizhan_cs35l41_snapshot(1, boot_flags, &f.bus, &clock, &snap));
            assert(snap.status == ZizhanAmpPass && snap.completed == 2 && snap.dsp_stopped[0] && snap.dsp_stopped[1]);
            assert(f.value_writes == 4 && f.commands == 122);
        }
    }
    // Every transaction, including value-write dispatch and its readback,
    // must fail closed without retry and retain partial progress honestly.
    for (unsigned cmd = 1; cmd <= successful_commands; ++cmd) {
        init(&f); f.revid[1] = 0xb2; f.devid[1] = 0x35a40; f.allow_value_writes = 1;
        f.state[0][1] = f.state[1][1] = 0x3321; f.state[0][2] = f.state[1][2] = 0x101;
        f.fail_command = cmd;
        assert(zizhan_cs35l41_safe_off(1, boot_flags, &f.bus, &clock, &off) == ZizhanAmpBus);
        assert(off.status == ZizhanAmpFail && off.identity.controller_uncertain && f.commands == cmd);
        assert(off.acknowledged_writes <= off.attempted_writes && off.attempted_writes <= 4);
        assert(off.completed == (cmd > 25));
    }
    for (unsigned test = 0; test < 7; ++test) {
        init(&f); f.revid[1] = 0xb2; f.devid[1] = 0x35a40; f.allow_value_writes = 1;
        f.state[0][1] = f.state[1][1] = 0x3321; f.state[0][2] = f.state[1][2] = 0x101;
        if (test < 2) f.state[test][0] = 1;
        if (test == 2) f.global_change_command = 15;
        if (test == 3) f.refuse_writes = 1;
        if (test == 4) f.revid[1] = 0xb4;
        if (test == 5) f.flip_after = 1;
        if (test == 6) { f.stall_command = 17; f.frozen = 1; }
        int rc = zizhan_cs35l41_safe_off(1, boot_flags, &f.bus, &clock, &off);
        assert(rc == (test < 3 ? ZizhanAmpOutputActive : test == 3 ? ZizhanAmpReadback :
                      test == 4 ? ZizhanAmpWrongIdentity : test == 5 ? ZizhanAmpStateChanged : ZizhanAmpTimeout));
        assert(off.status == ZizhanAmpFail);
        if (test < 3 || test == 4) assert(!off.attempted_writes && !f.value_writes);
        if (test == 3) assert(off.attempted_writes == 1 && off.acknowledged_writes == 1);
        if (test == 6) assert(off.attempted_writes == 1 && !off.acknowledged_writes && off.identity.controller_uncertain);
    }
    for (unsigned test = 0; test < 3; ++test) {
        init(&f); assert(!zizhan_cs35l41_safe_off(test == 1 ? 1 : test, test == 1 ? 15 : boot_flags, &f.bus, &clock, &off));
        assert(off.status == ZizhanAmpSkip && !f.reads && !f.writes);
    }
    // Entry diagnostics distinguish every refusal source without clearing
    // hardware or consuming residual FIFO data. Even a level/completion that
    // reappears after successful owned transactions is not silently ignored.
    for (unsigned test = 0; test < 6; ++test) {
        init(&f);
        if (test == 0) f.bus.phase = GeniTxWait;
        if (test == 1) f.active = 0x1001;
        if (test == 2) f.stale = GENI_M_CMD_DONE;
        if (test == 3) f.idle_rx = 1;
        if (test == 4) f.stale = GENI_M_TX_WATERMARK;
        if (test == 5) { f.active = 1; f.stale = GENI_M_GP_IRQ; f.idle_rx = 0x01000001; }
        assert(!identify(&f, 1, 15, &out) && out.reason == ZizhanAmpBusy && !f.writes);
        assert(out.prerequisite_valid == (test == 0 ? 1u : 3u));
        assert(out.busy_sources == (test == 0 ? 1u : test == 1 ? 2u : test == 3 ? 8u : test == 5 ? 14u : 4u));
        if (test) assert(out.entry_se_status == f.active && out.entry_irq == f.stale && out.entry_rx_fifo == f.idle_rx);
    }
    init(&f); f.revid[1] = 0xb2; f.devid[1] = 0x35a40; f.allow_value_writes = 1;
    f.state[0][1] = f.state[1][1] = 0x3321; f.state[0][2] = f.state[1][2] = 0x101;
    f.reassert_after = 36;
    assert(!zizhan_cs35l41_safe_off(1, boot_flags, &f.bus, &clock, &off) && off.status == ZizhanAmpPass);
    const unsigned writes_before = f.writes;
    assert(!zizhan_cs35l41_snapshot(1, boot_flags, &f.bus, &clock, &snap));
    assert(snap.status == ZizhanAmpSkip && snap.reason == ZizhanAmpBusy && !snap.completed);
    assert(snap.identity.prerequisite_valid == 3 && snap.identity.busy_sources == 4);
    assert(snap.identity.entry_irq == GENI_M_CMD_DONE && !snap.identity.entry_se_status && !snap.identity.entry_rx_fifo);
    assert(f.commands == 36 && f.writes == writes_before && !snap.word_count[0] && !snap.word_count[1]);
    ZizhanAmpHandoff hand;
    for (unsigned test = 0; test < 12; ++test) {
        init(&f); f.revid[1] = 0xb2; f.devid[1] = 0x35a40; f.allow_value_writes = 1;
        f.state[0][1] = f.state[1][1] = 0x3321; f.state[0][2] = f.state[1][2] = 0x101;
        assert(!zizhan_cs35l41_safe_off(1, boot_flags, &f.bus, &clock, &off) && off.status == ZizhanAmpPass);
        f.hand_model = 1; f.hand_irq = test == 0 ? 0 : test == 1 ? 0x80 : 0x04000080;
        if (test == 3) f.hand_delay = 4;
        if (test == 4 || test == 5) f.hand_refuse = 1;
        if (test == 5) f.frozen = 1;
        if (test == 6) f.hand_reassert = 1;
        if (test >= 7 && test <= 9) f.hand_poison = test - 6;
        if (test == 10) f.hand_irq |= GENI_M_CMD_DONE; // unknown for this narrow lease
        if (test == 11) f.idle_rx = 1;
        const unsigned writes_before = f.writes;
        int rc = zizhan_cs35l41_handoff(1, boot_flags, &f.bus, &clock, &off, &hand);
        assert(f.commands == 36 && f.value_writes == 4 && f.hand_clear <= 1);
        if (test < 4) {
            assert(!rc && hand.status == ZizhanAmpPass && !hand.after_irq && !hand.after_rx && !hand.after_se);
            assert(hand.clear_writes == (test != 0) && hand.rx_watermark == 2 && hand.rx_rfr_watermark == 14);
            assert(hand.samples == (test == 0 ? 2u : test == 3 ? 6u : 3u));
        } else {
            assert(rc && hand.status == ZizhanAmpFail);
            assert(rc == (test == 4 || test == 5 ? ZizhanAmpTimeout : ZizhanAmpStateChanged));
            assert(hand.samples <= 26);
            if (test >= 10) assert(!hand.clear_writes && f.writes == writes_before);
        }
        assert(f.writes == writes_before + hand.clear_writes);
        if (test == 2) {
            f.hand_model = 0; f.stale = 0;
            assert(!zizhan_cs35l41_snapshot(1, boot_flags, &f.bus, &clock, &snap));
            assert(snap.status == ZizhanAmpPass && snap.completed == 2 && f.commands == 122 && f.value_writes == 4);
        }
    }
    for (unsigned test = 0; test < 5; ++test) {
        init(&f); unsigned reads_before = f.reads;
        ZizhanAmpSafeOff missing = {0};
        assert(!zizhan_cs35l41_handoff(test == 0 ? 0 : test == 1 ? 2 : 1,
            test == 2 ? 15 : boot_flags, &f.bus, &clock, test == 4 ? NULL : &missing, &hand));
        assert(hand.status == ZizhanAmpSkip && !f.writes && f.reads == reads_before);
    }
    for (unsigned test = 0; test < 12; ++test) {
        init(&f); f.revid[1] = 0xb2; f.devid[1] = 0x35a40; f.allow_value_writes = 1;
        f.state[0][1] = f.state[1][1] = 0x3321; f.state[0][2] = f.state[1][2] = 0x101;
        assert(!zizhan_cs35l41_safe_off(1, boot_flags, &f.bus, &clock, &off) && off.status == ZizhanAmpPass);
        f.hand_model = 2; f.hand_irq = 0x80; f.source_slave = 0x08000000;
        if (test == 1) f.source_auto = 1;
        if (test == 2) {f.source_auto = 1; f.source_delay = 4;}
        if (test == 3) {f.hand_irq = 0; f.source_slave = 0;}
        if (test == 4) f.source_refuse = 1;
        if (test == 5) {f.source_refuse = 1; f.frozen = 1;}
        if (test == 6) {f.source_auto = 1; f.source_reassert = 1;}
        if (test == 7) f.source_dma = 1;
        if (test == 8) f.source_slave |= 2;
        if (test == 9) f.hand_irq |= GENI_M_GP_IRQ;
        if (test == 10) f.idle_rx = 1;
        if (test == 11) f.hand_poison = 1;
        const unsigned commands = f.commands, writes = f.writes;
        int rc = zizhan_cs35l41_source_handoff(1, boot_flags, &f.bus, &clock, &off, &hand);
        assert(f.commands == commands && f.value_writes == 4 && f.source_clear <= 1 && f.hand_clear <= 1);
        assert(f.writes == writes + hand.slave_clear_writes + hand.clear_writes);
        if (test < 4) {
            assert(!rc && hand.status == ZizhanAmpPass && !hand.slave_after && !hand.after_irq);
            assert(hand.slave_clear_writes == (test != 3));
            assert(hand.clear_writes == (test == 0));
            if (test == 0) assert(f.write_order[0] == 0x648 && f.write_order[1] == GENI_M_IRQ_CLEAR);
            if (test == 1) { f.hand_model = 0; assert(!zizhan_cs35l41_snapshot(1,boot_flags,&f.bus,&clock,&snap)); assert(snap.status == ZizhanAmpPass); }
        } else {
            assert(rc && hand.status == ZizhanAmpFail && hand.samples <= 26);
            if (test >= 7 && test < 11) assert(!hand.slave_clear_writes && !hand.clear_writes);
            if (test == 11) assert(hand.slave_clear_writes == 1 && !hand.clear_writes);
            if (test == 4 || test == 5) assert(rc == ZizhanAmpTimeout && hand.slave_clear_writes == 1 && !hand.clear_writes);
        }
    }
    ZizhanAmpBusConfig config;
    init(&f); f.active=0x1001; f.stale=0x80; f.source_slave=0x08000000; f.source_dma=0x10; f.idle_rx=1;
    const unsigned before_reads=f.reads;
    assert(!zizhan_cs35l41_bus_config(1,boot_flags,&f.bus,&config));
    assert(config.valid==24&&config.phase==GeniIdle&&f.reads==before_reads+24&&!f.writes);
    assert(config.values[0]==0x1001&&config.values[1]==0x300&&config.values[3]==0x80);
    assert(config.values[5]==0x08000000&&config.values[13]==1&&config.values[18]==0x10);
    for(unsigned test=0;test<3;++test){
        init(&f);assert(!zizhan_cs35l41_bus_config(test==0?0:test==1?2:1,test==2?15:boot_flags,&f.bus,&config));
        assert(!config.valid&&!f.reads&&!f.writes);
    }
    ZizhanAmpReadiness ready;
    const unsigned bad_regs[] = {0x68,0x6c,0x614,0x644,0xe1c,0x258,0x254,0x284,0x288,0x800,0x804,0x810,0x814,0xc40,0xc48,0xd40,0xd48,0xe18,0xe30,0x908,0xe40};
    for(unsigned test=0;test<sizeof bad_regs/sizeof bad_regs[0]+2;++test) {
        init(&f);f.revid[1]=0xb2;f.devid[1]=0x35a40;f.allow_value_writes=1;
        f.state[0][1]=f.state[1][1]=0x3321;f.state[0][2]=f.state[1][2]=0x101;
        assert(!zizhan_cs35l41_safe_off(1,boot_flags,&f.bus,&clock,&off)&&off.status==ZizhanAmpPass);
        f.config_model=1;f.hand_model=2;f.hand_irq=0x80;f.source_slave=0x08000000;
        if(test>=2){f.override_at=bad_regs[test-2];f.override_value=f.override_at==0x614?GENI_M_IRQ_WANTED|0x80:1;}
        if(test==1){f.source_slave=0;f.hand_irq=0;}
        int rc=zizhan_cs35l41_owned_snapshot(1,boot_flags,&f.bus,&clock,&off,&ready,&snap);
        if(test<2){assert(!rc&&ready.status==ZizhanAmpPass&&snap.status==ZizhanAmpPass&&snap.completed==2);
            assert(ready.masked_status==(test==0)&&ready.slave_clear_writes==(test==0)&&!f.hand_clear);
            assert(snap.identity.entry_irq==(test==0?0x80u:0u));}
        else assert(rc&&ready.status==ZizhanAmpFail&&f.commands==36&&!f.source_clear&&!f.hand_clear);
    }
    for(unsigned test=0;test<6;++test){
        init(&f);f.revid[1]=0xb2;f.devid[1]=0x35a40;f.allow_value_writes=1;
        f.state[0][1]=f.state[1][1]=0x3321;f.state[0][2]=f.state[1][2]=0x101;
        assert(!zizhan_cs35l41_safe_off(1,boot_flags,&f.bus,&clock,&off));
        f.config_model=1;f.hand_model=2;f.hand_irq=0x80;f.source_slave=0x08000000;
        if(test==0)f.config_change=1;
        if(test==1){f.source_refuse=1;f.frozen=1;}
        if(test==2)f.hand_irq|=GENI_M_CMD_DONE;
        if(test==3)f.bus.phase=GeniTxWait;
        if(test==4)off.after_count[1]=0;
        if(test==5)off.identity.controller_uncertain=1;
        const unsigned commands=f.commands;
        int rc=zizhan_cs35l41_owned_snapshot(1,boot_flags,&f.bus,&clock,&off,&ready,&snap);
        if(test<4)assert(rc&&ready.status==ZizhanAmpFail);
        else assert(!rc&&ready.status==ZizhanAmpSkip&&!ready.samples);
        assert(f.commands==commands&&!f.hand_clear);
        if(test==1)assert(ready.samples<=26&&ready.reason==ZizhanAmpTimeout&&ready.slave_clear_writes==1);
        if(test==0)assert(ready.reason==ZizhanAmpStateChanged);
    }
    ZizhanAmpB2Patch patched;
    for(unsigned test=0;test<50;++test){
        init(&f);f.revid[1]=0xb2;f.devid[1]=0x35a40;f.allow_value_writes=1;
        f.state[0][1]=f.state[1][1]=0x3321;f.state[0][2]=f.state[1][2]=0x101;
        assert(!zizhan_cs35l41_safe_off(1,boot_flags,&f.bus,&clock,&off));
        f.config_model=1;f.hand_model=2;f.hand_irq=0x80;f.source_slave=0x08000000;
        assert(!zizhan_cs35l41_owned_snapshot(1,boot_flags,&f.bus,&clock,&off,&ready,&snap)&&snap.status==ZizhanAmpPass);
        const unsigned before=f.commands;f.allow_patch=1;
        for(unsigned c=0;c<2;++c)for(unsigned n=0;n<6;++n)f.patch_values[c][n]=0xdead;
        if(test>=1&&test<=44)f.fail_command=before+test;
        if(test==45)f.patch_mismatch=1;
        if(test==46)snap.state_after[0][0]=1;
        if(test==47)f.state[0][0]=1;
        if(test==48){f.stall_command=before+4;f.frozen=1;}
        if(test==49)snap.identity.revid[1]=0xb1;
        int rc=zizhan_cs35l41_b2_patch(1,boot_flags,&f.bus,&clock,&snap,&patched);
        if(!test){
            assert(!rc&&patched.status==ZizhanAmpPass&&patched.completed==2&&patched.attempted_writes==20&&patched.acknowledged_writes==20);
            assert(f.commands==before+44&&f.patch_writes==20&&f.key_stage[0]==4&&f.key_stage[1]==4);
            for(unsigned c=0;c<2;++c){assert(patched.patch_readbacks[c]==6&&patched.close_acknowledged[c]==2&&patched.state_count[c]==3);assert(patched.after_state[c][0]==0&&patched.after_state[c][1]==0x3300&&patched.after_state[c][2]==0x100);}
        }else if(test<=44){assert(rc==ZizhanAmpBus&&patched.transport.controller_uncertain&&f.commands==before+test);assert(patched.completed==(test>22));}
        else if(test==45){assert(rc==ZizhanAmpReadback&&patched.close_acknowledged[0]==2&&f.key_stage[0]==4&&!patched.completed);}
        else if(test==46||test==49)assert(!rc&&patched.status==ZizhanAmpSkip&&f.commands==before&&!f.patch_writes);
        else if(test==47)assert(rc==ZizhanAmpStateChanged&&!f.patch_writes);
        else assert(rc==ZizhanAmpTimeout&&patched.transport.controller_uncertain&&!patched.completed);
    }
    ZizhanAmpTrim trim;unsigned trim_commands=0;
    for(unsigned test=0;test<7;++test){
        const unsigned before=prepare_trim(&f,&snap,&patched);
        if(test==1)f.trim_mismatch=1;
        if(test==2)snap.otp_id[1]=2;
        if(test==3)patched.close_acknowledged[0]=0;
        if(test==4)f.state[0][0]=1;
        if(test==5){f.stall_command=before+6;f.frozen=1;}
        if(test==6)patched.transport.controller_uncertain=1;
        int rc=zizhan_cs35l41_trim(1,boot_flags,&f.bus,&clock,&snap,&patched,&trim);
        if(!test){
            assert(!rc&&trim.status==ZizhanAmpPass&&trim.completed==2&&trim.key_attempts==8&&trim.readbacks[0]==98&&trim.readbacks[1]==98);
            assert(trim.close_acknowledged[0]==2&&trim.close_acknowledged[1]==2&&trim.state_count[0]==3&&trim.state_count[1]==3);
            assert(trim.attempted_writes==trim.acknowledged_writes&&trim.trim_attempts==f.trim_writes&&trim.trim_attempts<=196);
            for(unsigned chip=0;chip<2;++chip)for(unsigned i=0;i<98;++i){ZizhanAmpTrimUpdate u=f.trim_updates[chip][i];unsigned slot=0;while(zizhan_amp_trim8[slot].reg!=u.reg)++slot;assert((f.trim_values[chip][slot]&u.mask)==u.value);}
            trim_commands=f.commands-before;assert(trim_commands<=608);
        }else if(test==1)assert(rc==ZizhanAmpReadback&&trim.close_acknowledged[0]==2&&!trim.completed);
        else if(test==2||test==3||test==6)assert(!rc&&trim.status==ZizhanAmpSkip&&f.commands==before&&!f.trim_writes);
        else if(test==4)assert(rc==ZizhanAmpStateChanged&&!f.trim_writes);
        else assert(rc==ZizhanAmpTimeout&&trim.transport.controller_uncertain&&!trim.completed);
    }
    // Actual production transaction failure at every read/write position.
    for(unsigned fail=1;fail<=trim_commands;++fail){
        const unsigned before=prepare_trim(&f,&snap,&patched);f.fail_command=before+fail;
        assert(zizhan_cs35l41_trim(1,boot_flags,&f.bus,&clock,&snap,&patched,&trim)==ZizhanAmpBus);
        assert(trim.status==ZizhanAmpFail&&trim.transport.controller_uncertain&&f.commands==before+fail);
        assert(trim.acknowledged_writes<=trim.attempted_writes&&trim.attempted_writes<=204);
    }
    puts("PASS CS35L41 production identity/snapshot/safe-off + GENI: BE32,36-transaction failures,clear-only bit preservation,readback/partial/frozen guards; no hardware");
    return 0;
}
