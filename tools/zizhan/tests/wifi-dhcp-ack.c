/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include "PongWifiLease.h"
int main(void) {
  unsigned char mac[6]={2,3,4,5,6,7};
  unsigned char b[280]={2,1,6};
  unsigned char options[]={0,53,1,5,1,4,255,255,255,0,3,4,192,0,2,1,6,8,192,0,2,2,192,0,2,3,51,4,0,0,0,30,255};
  memcpy(b+16,(unsigned char[]){192,0,2,9},4);
  memcpy(b+28,mac,6); memcpy(b+236,"\x63\x82\x53\x63",4); memcpy(b+240,options,sizeof options);
  struct pong_wifi_lease lease={0};
  assert(pong_wifi_dhcp_fields(b,240+sizeof options,mac,&lease)==5 && lease.version==PONG_WIFI_LEASE_VERSION && lease.dns_count==2 && lease.router[3]==1);
  struct pong_wifi_lease_tracker t={0};
  unsigned char req[8]={1,0,0,0,11,0,0,0};
  pong_wifi_lease_link(&t,true);
  pong_wifi_lease_packet(&t,true,3,req,&lease,10);
  b[4]=12;
  pong_wifi_lease_packet(&t,false,5,b,&lease,11);
  assert(!pong_wifi_lease_snapshot(&t,11).version);
  b[4]=11;
  pong_wifi_lease_packet(&t,false,5,b,&lease,11);
  assert(pong_wifi_lease_snapshot(&t,40).sequence==1);
  assert(!pong_wifi_lease_snapshot(&t,41).version);
  req[4]=b[4]=13;
  struct pong_wifi_lease replacement=lease;
  replacement.router[3]=254;
  replacement.dns[0][3]=53;
  pong_wifi_lease_packet(&t,true,3,req,&lease,35);
  pong_wifi_lease_packet(&t,false,5,b,&replacement,36);
  struct pong_wifi_lease renewed=pong_wifi_lease_snapshot(&t,41);
  assert(renewed.sequence==2 && renewed.router[3]==254 && renewed.dns[0][3]==53);
  struct pong_wifi_lease empty={0};
  req[4]=b[4]=14;
  pong_wifi_lease_packet(&t,true,3,req,&empty,42);
  pong_wifi_lease_packet(&t,false,6,b,&empty,43);
  assert(!pong_wifi_lease_snapshot(&t,43).version);
  pong_wifi_lease_link(&t,false);
  pong_wifi_lease_link(&t,true);
  assert(pong_wifi_lease_snapshot(&t,44).link==3 && !pong_wifi_lease_snapshot(&t,44).version);
  pong_wifi_lease_packet(&t,false,5,b,&lease,45);
  assert(!pong_wifi_lease_snapshot(&t,45).version);
  req[4]=b[4]=15;
  pong_wifi_lease_packet(&t,true,3,req,&lease,46);
  pong_wifi_lease_packet(&t,false,5,b,&lease,47);
  assert(pong_wifi_lease_snapshot(&t,47).link==3 && pong_wifi_lease_snapshot(&t,47).sequence==4);
  pong_wifi_lease_packet(&t,true,7,req,&empty,48);
  assert(!pong_wifi_lease_snapshot(&t,48).version);
  lease.version=0; b[28]^=1;
  assert(!pong_wifi_dhcp_fields(b,sizeof b,mac,&lease) && !lease.version);
  b[28]^=1;
  assert(!pong_wifi_dhcp_fields(b,250,mac,&lease) && !lease.version);
  b[240]=121; b[241]=0; memcpy(b+242,options+1,sizeof(options)-1);
  assert(pong_wifi_dhcp_fields(b,241+sizeof options,mac,&lease)==5 && !lease.version);
}
