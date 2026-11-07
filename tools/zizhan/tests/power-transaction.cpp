// SPDX-License-Identifier: MIT
#include "../../../drivers/PongPower/ZizhanPowerTransaction.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>
using namespace zizhan_power;

static Transaction graph() {
    Transaction t;
    assert(t.owner(0,1u<<1,31)==Result::Ok); // display consumes GPU
    assert(t.owner(1,1u<<3,31)==Result::Ok); // GPU consumes shared domain
    assert(t.owner(2,1u<<3,31)==Result::Ok); // storage consumes shared domain
    assert(t.owner(3,0,31)==Result::Ok);
    return t;
}
static void start(Transaction &t) {assert(t.begin(15,0,1000,7,0)==Result::Ok);}
static void ready(Transaction &t) {
    for(unsigned i=0;i<12;++i) {
        Ticket k=t.issue(i+1);assert(k.valid() && k.owner==i%4);
        assert(t.acknowledge(k,Reply::Success,i+1)==Result::Ok);
    }
    assert(t.snapshot().phase==Phase::Ready);
}
static uint32_t rollback(Transaction &t) {
    uint32_t mask=0;
    for(unsigned bound=0;bound<16 && t.snapshot().phase==Phase::RollingBack;++bound) {
        Ticket k=t.issue(100+bound);assert(k.valid() && k.step==Step::Rollback);
        assert(!(mask&(1u<<k.owner)));mask|=1u<<k.owner;
        assert(t.acknowledge(k,Reply::Success,101+bound)==Result::Ok);
    }
    assert(t.snapshot().phase==Phase::Awake);
    return mask;
}
int main(int argc,char **argv) {
    assert(argc==2);
    if(!strcmp(argv[1],"gates")) {
        Transaction t;
        assert(t.begin(1,0,100,0,0)==Result::MissingOwner);
        assert(t.owner(0,0,0)==Result::Ok);
        assert(t.begin(1,0,100,0,0)==Result::MissingCapability);
        assert(t.begin(1,0,100,0,1)==Result::Constrained);
        assert(t.begin(1,100,100,0,0)==Result::Invalid);
        assert(t.owner(16,0,31)==Result::Invalid);
        assert(t.owner(0,1,31)==Result::Invalid);
        assert(t.owner(0,2,31)==Result::Ok && t.owner(1,1,31)==Result::Ok);
        assert(t.begin(3,0,100,0,0)==Result::DependencyCycle);
        assert(t.snapshot().phase==Phase::Awake && !t.issue(1).valid());
    } else if(!strcmp(argv[1],"success")) {
        Transaction t=graph();start(t);ready(t);
        const auto g=t.snapshot().generation;
        assert(t.enter(g,7,0,false,20)==Result::NoExecutor);
        assert(t.snapshot().phase==Phase::Ready);
        assert(t.enter(g,7,0,true,21)==Result::Ok);
        assert(t.wake(g-1,22)==Result::Stale);
        assert(t.wake(g,23)==Result::Ok && t.cancel(23)==Result::Invalid);
        for(unsigned pass=0;pass<2;++pass)for(unsigned id:{3u,1u,0u,2u}) {
            Ticket k=t.issue(30);assert(k.valid() && k.owner==id);
            assert(k.step==(pass?Step::Verify:Step::Resume));
            assert(t.acknowledge(k,Reply::Success,31)==Result::Ok);
        }
        assert(t.snapshot().phase==Phase::Awake);
        assert(t.begin(15,40,1000,8,0)==Result::Ok && t.snapshot().generation==g+1);
    } else if(!strcmp(argv[1],"rejected")) {
        for(unsigned fail=0;fail<12;++fail) {
            Transaction t=graph();start(t);
            for(unsigned i=0;i<fail;++i) {
                Ticket k=t.issue(1);assert(k.valid());
                assert(t.acknowledge(k,Reply::Success,2)==Result::Ok);
            }
            Ticket k=t.issue(3);assert(k.valid());
            uint32_t touched=t.snapshot().touched;
            assert(t.acknowledge(k,Reply::Rejected,4)==Result::Invalid);
            assert(rollback(t)==touched);
        }
    } else if(!strcmp(argv[1],"late-stale")) {
        Transaction t=graph();start(t);
        Ticket first=t.issue(1);assert(first.valid());
        assert(!t.issue(2).valid()); // pending action cannot be issued twice
        assert(t.cancel(3)==Result::Ok && !t.issue(4).valid());
        assert(t.acknowledge(first,Reply::Success,5)==Result::Ok);
        assert(rollback(t)==1);
        assert(t.begin(15,200,1000,9,0)==Result::Ok);
        Ticket next=t.issue(201);auto before=t.snapshot();
        assert(t.acknowledge(first,Reply::Success,202)==Result::Stale);
        assert(t.snapshot().touched==before.touched && t.snapshot().sequence==before.sequence);
        assert(t.acknowledge(next,Reply::Success,203)==Result::Ok);
        assert(t.acknowledge(next,Reply::Success,204)==Result::Stale);
    } else if(!strcmp(argv[1],"uncertain")) {
        Transaction t=graph();start(t);
        Ticket k=t.issue(1);assert(k.valid());
        assert(t.acknowledge(k,Reply::Uncertain,2)==Result::Uncertain);
        assert(t.snapshot().phase==Phase::Failed && t.snapshot().uncertain==1 && t.snapshot().pending);
        assert(!t.issue(3).valid() && t.begin(15,4,1000,7,0)==Result::Busy);
        Transaction r=graph();start(r);k=r.issue(1);r.acknowledge(k,Reply::Success,2);
        assert(r.cancel(3)==Result::Ok);k=r.issue(4);
        assert(r.acknowledge(k,Reply::Rejected,5)==Result::Invalid && r.snapshot().phase==Phase::Failed);
        Transaction bad=graph();start(bad);k=bad.issue(1);
        assert(bad.acknowledge(k,static_cast<Reply>(99),2)==Result::Invalid);
        assert(bad.snapshot().phase==Phase::Failed && bad.snapshot().pending);
    } else if(!strcmp(argv[1],"deadline")) {
        Transaction t=graph();assert(t.begin(15,0,10,7,0)==Result::Ok);
        Ticket k=t.issue(1);assert(k.valid());
        assert(!t.issue(10).valid());
        assert(t.snapshot().phase==Phase::Failed && t.snapshot().pending && t.snapshot().uncertain==1);
        assert(t.acknowledge(k,Reply::Success,11)==Result::Stale);
        assert(t.begin(15,12,1000,7,0)==Result::Busy);
    } else if(!strcmp(argv[1],"entry-race")) {
        Transaction t=graph();start(t);ready(t);
        assert(t.enter(t.snapshot().generation,8,0,true,20)==Result::WakeChanged);
        assert(rollback(t)==15);
        start(t);ready(t);
        assert(t.enter(t.snapshot().generation,7,1,true,20)==Result::Constrained);
        assert(rollback(t)==15);
    } else assert(false);
    puts("PASS production power transaction model (no hardware executor)");
}
