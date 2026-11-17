/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_STATUSBAR18_H
#define ZIZHAN_STATUSBAR18_H
/* Real inner panel and current UIScreen observation. No mainScreen lookup
 * while a native status-provider selector is initializing that screen.
 * A nil screen is the native default-display discovery call; permit it
 * only with explicit opt-in AND an actual zizhan L18 service match. */
static inline int zizhan_statusbar_native_pad(unsigned enabled,unsigned board,
 unsigned has_screen,double width,double height,double scale)
{
 if(enabled!=1||board!=1)return 0;
 if(!has_screen)return 1;
 return scale==2.0&&((width==2160&&height==1916)||(width==1916&&height==2160));
}
#endif
