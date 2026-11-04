/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include "PongWifiLease.h"
int main(void) {
  unsigned char mac[6]={2,3,4,5,6,7};
  unsigned char b[280]={2,1,6};
  unsigned char options[]={0,53,1,5,1,4,255,255,255,0,3,4,192,0,2,1,6,8,192,0,2,2,192,0,2,3,255};
  memcpy(b+16,(unsigned char[]){192,0,2,9},4);
  memcpy(b+28,mac,6); memcpy(b+236,"\x63\x82\x53\x63",4); memcpy(b+240,options,sizeof options);
  struct pong_wifi_lease lease={0};
  assert(pong_wifi_dhcp_fields(b,240+sizeof options,mac,&lease)==5 && lease.version==1 && lease.dns_count==2 && lease.router[3]==1);
  lease.version=0; b[28]^=1;
  assert(!pong_wifi_dhcp_fields(b,sizeof b,mac,&lease) && !lease.version);
  b[28]^=1;
  assert(!pong_wifi_dhcp_fields(b,250,mac,&lease) && !lease.version);
  b[240]=121; b[241]=0; memcpy(b+242,options+1,sizeof(options)-1);
  assert(pong_wifi_dhcp_fields(b,241+sizeof options,mac,&lease)==5 && !lease.version);
}
