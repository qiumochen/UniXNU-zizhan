/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <stdio.h>
#include <map>
#include <vector>
#include "ZizhanGeniTiming.h"
struct Page {
    std::map<unsigned,uint32_t> words;
    std::vector<std::pair<unsigned,uint32_t>> writes;
    unsigned refuse = ~0u;
    uint32_t read(unsigned at) { return words[at]; }
    void write(unsigned at,uint32_t value) {
        assert(at==0x7c||at==GENI_CLK_CFG||at==GENI_SCL_COUNTERS||at==GENI_DMA_MODE||
               at==GENI_TX_PACK0||at==GENI_TX_PACK1||at==GENI_RX_PACK0||at==GENI_RX_PACK1||
               at==0x810||at==0x814||at==GENI_BYTE_GRAN||at==GENI_TX_WATERMARK||at==GENI_M_IRQ_CLEAR||at==GENI_M_IRQ_EN||at==GENI_SE_IRQ_EN);
        writes.emplace_back(at,value);if(at!=refuse)words[at]=value;
    }
};
static void init(Page &se) {
    se=Page{};se.words[GENI_FW_REVISION]=0x303;
    se.words[0xe24]=se.words[0xe28]=(32u<<24)|(16u<<16);
    se.words[GENI_SE_IRQ_EN]=0x80f;
}
int main() {
    using namespace zizhan_geni;
    Page root,se;Receipt r;
    init(se);assert(prepare(se,r)==NoXo&&se.writes.empty());
    root.words[0x18]=0x100000;
    assert(select(root,r)==Ok&&r.clock_valid&&r.level==0&&root.writes.empty());
    assert(prepare(se,r)==Ok&&r.retained&&r.config==0x3000071);
    assert(se.read(GENI_CLK_CFG)==0x71&&se.read(GENI_SCL_COUNTERS)==((10u<<20)|(11u<<10)|26));
    assert(se.read(GENI_SE_IRQ_EN)==0x800); // preserve unrelated bits, mask all four external routes
    assert(se.writes.size()==13);
    for(unsigned level=0;level<8;++level) {
        root=Page{};r=Receipt{};root.words[0x28]=1;
        for(unsigned i=0;i<8;++i)root.words[0x30+4*i]=0x100;
        root.words[0x30+4*level]=1;
        assert(select(root,r)==Ok&&r.level==level&&r.selected==1);
    }
    for(unsigned bad=0;bad<4;++bad) {
        root=Page{};r=Receipt{};
        if(bad==0)root.words[0x14]=1;
        if(bad==1)root.words[0x18]=0x100;
        if(bad==2)root.words[0x18]=2;
        if(bad==3){root.words[0x28]=1;for(unsigned i=0;i<8;++i)root.words[0x30+4*i]=0x1000;}
        assert(select(root,r)==(bad==0?RootBusy:NoXo)&&!r.clock_valid&&root.writes.empty());
    }
    root=Page{};assert(select(root,r)==Ok);
    for(unsigned bad=0;bad<7;++bad) {
        init(se);
        if(bad==0)se.words[GENI_FW_REVISION]=1;
        if(bad==1)se.words[GENI_IF_DISABLE]=1;
        if(bad==2)se.words[0x40]=0x1000;
        if(bad==3)se.words[GENI_M_IRQ_STATUS]=1;
        if(bad==4)se.words[GENI_RX_FIFO_STATUS]=1;
        if(bad==5)se.words[0xe24]=(16u<<24)|(16u<<16);
        if(bad==6)se.words[0xe28]=(32u<<24)|(1u<<16);
        assert(prepare(se,r)==(bad<2?Protocol:bad<5?Busy:Fifo)&&se.writes.empty());
    }
    for(unsigned at:{0x7cu,GENI_CLK_CFG,GENI_SCL_COUNTERS,GENI_TX_PACK0,GENI_M_IRQ_EN,GENI_SE_IRQ_EN}) {
        init(se);se.refuse=at;r.retained=false;if(at==0x7c)se.words[at]=9;
        const auto result=prepare(se,r);
        // CLK/timing write refusal prevents generic preparation before FIFO;
        // other readback failures remain retained, never silently PASS.
        assert((result==Readback||result==PrepareFailed)&&r.retained);
    }
    // Original interrupt-based prepare behavior remains available unchanged.
    init(se);se.words[GENI_CLK_CFG]=0x71;se.words[GENI_SCL_COUNTERS]=1;
    GeniI2c bus={};GeniIo io={[](void*p,uint32_t at){return static_cast<Page*>(p)->read(at);},
        [](void*p,uint32_t at,uint32_t v){static_cast<Page*>(p)->write(at,v);},&se};
    uint32_t config=0;assert(!geni_i2c_prepare(&bus,&io,0x40,&config));assert(se.read(GENI_SE_IRQ_EN)==0x80f);
    // Only separate RX opt-in changes thresholds; original prepare above
    // remains13 writes. Real vendor dimensions and bit-preserving ownership
    // gates are checked before either of the at-most2 writes.
    for(unsigned test=0;test<12;++test) {
        init(se); Receipt timing; timing.clock_valid=true; RxReceipt rx;
        se.words[0x810]=0; se.words[0x814]=14;
        if(test==1){se.words[0x810]=15;se.words[0x814]=16;}
        if(test==2)timing.clock_valid=false;
        if(test==3)se.words[GENI_FW_REVISION]=0;
        if(test==4)se.words[GENI_DMA_MODE]=1;
        if(test==5)se.words[0x40]=1;
        if(test==6)se.words[GENI_M_IRQ_STATUS]=0x80;
        if(test==7)se.words[GENI_RX_FIFO_STATUS]=1;
        if(test==8)se.words[0xe24]=(32u<<24)|(8u<<16);
        if(test==9)se.words[0xe28]=(32u<<24)|(15u<<16);
        if(test==10)se.refuse=0x810;
        if(test==11)se.refuse=0x814;
        Result result=prepare_rx(se,timing,rx);
        if(test<2){assert(result==Ok&&rx.watermark_after==15&&rx.rfr_after==16);assert(rx.writes==(test?0u:2u));}
        else if(test<10)assert(result==(test==2?NoXo:test<5?Protocol:test<8?Busy:Fifo)&&se.writes.empty());
        else {assert(result==Readback);assert(rx.writes==(test==10?1u:2u));}
        for(auto write:se.writes)assert(write.first==0x810||write.first==0x814);
    }
    puts("PASS actual GENI timing/FIFO: root/DFS selection, zero-write guards, vendor timing, polling routes, failed readback and legacy prepare");
}
