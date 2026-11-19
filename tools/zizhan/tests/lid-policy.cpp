#include <cassert>
#include <cstdio>
#include "ZizhanLidPolicy.h"
using zizhan_lid::Action;using zizhan_lid::decide;
int main(){
 /* baseline / no transition never presses */
 assert(decide(false,true,1,10000,false,0)==Action::None);
 assert(decide(false,false,0,10000,false,0)==Action::None);
 /* close while on -> sleep; open while off -> wake */
 assert(decide(true,true,1,10000,false,0)==Action::PressSleep);
 assert(decide(true,false,0,10000,false,0)==Action::PressWake);
 /* already in the wanted state -> nothing (no toggle back) */
 assert(decide(true,true,0,10000,false,0)==Action::None);
 assert(decide(true,false,1,10000,false,0)==Action::None);
 /* display executor off or unknown (-1) -> nothing */
 assert(decide(true,true,-1,10000,false,0)==Action::None);
 assert(decide(true,false,-1,10000,false,0)==Action::None);
 /* transitioning -> skip */
 assert(decide(true,true,2,10000,false,0)==Action::SkipBusy);
 /* cooldown */
 assert(decide(true,false,0,10000+1499,true,10000)==Action::SkipCooldown);
 assert(decide(true,false,0,10000+1500,true,10000)==Action::PressWake);
 /* close->sleep, display goes off, open->wake after cooldown */
 assert(decide(true,true,1,20000,true,10000)==Action::PressSleep);
 assert(decide(true,false,0,22000,true,20000)==Action::PressWake);
 puts("PASS lid policy: baseline/no-transition none, close-on sleep, open-off wake, same-state none, unknown none, busy skip, cooldown");
}
