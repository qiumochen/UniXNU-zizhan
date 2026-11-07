/* SPDX-License-Identifier: MIT
 * zizhan POWER-02: explicit bring-up and owned preference restoration.
 * Planning is pure CoreFoundation: the caller applies the two-key change.
 */
#pragma once
#include <CoreFoundation/CoreFoundation.h>
#include <stdbool.h>

#define ZIZHAN_AUTODIM_KEY CFSTR("SBDisableAutoDim")
#define ZIZHAN_AUTODIM_OWNER_KEY CFSTR("dev.unixnu.zizhan.autodim-owner")
#define ZIZHAN_AUTODIM_OWNER CFSTR("zizhan-bringup-v1")

typedef struct {
    bool write_value, write_marker, request_wake;
    CFTypeRef value, marker; /* owned references; NULL means remove */
    const char *status, *reason;
} ZizhanBringupPlan;

static inline bool zizhan_autodim_marker_valid(CFTypeRef marker)
{
    if(!marker || CFGetTypeID(marker)!=CFDictionaryGetTypeID())return false;
    CFDictionaryRef d=(CFDictionaryRef)marker;
    CFTypeRef owner=CFDictionaryGetValue(d,CFSTR("owner"));
    CFTypeRef present=CFDictionaryGetValue(d,CFSTR("present"));
    if(!owner || !CFEqual(owner,ZIZHAN_AUTODIM_OWNER) ||
       !present || CFGetTypeID(present)!=CFBooleanGetTypeID())return false;
    CFTypeRef original=CFDictionaryGetValue(d,CFSTR("original"));
    return CFBooleanGetValue((CFBooleanRef)present) ? original!=NULL : original==NULL;
}

static inline ZizhanBringupPlan zizhan_bringup_plan(
    bool bringup, bool migrate_legacy, CFTypeRef current, CFTypeRef marker)
{
    ZizhanBringupPlan p={false,false,false,NULL,NULL,"SKIP","no-owned-preference"};
    const bool current_true=current && CFGetTypeID(current)==CFBooleanGetTypeID() &&
        CFBooleanGetValue((CFBooleanRef)current);
    if(bringup && migrate_legacy) {p.reason="conflicting-modes";return p;}
    if(marker && !zizhan_autodim_marker_valid(marker)) {
        p.reason="unknown-ownership-marker";return p;
    }
    if(bringup) {
        if(!marker) {
            CFMutableDictionaryRef d=CFDictionaryCreateMutable(
                kCFAllocatorDefault,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
            if(!d) {p.status="FAIL";p.reason="marker-allocation";return p;}
            CFDictionarySetValue(d,CFSTR("owner"),ZIZHAN_AUTODIM_OWNER);
            CFDictionarySetValue(d,CFSTR("present"),current?kCFBooleanTrue:kCFBooleanFalse);
            if(current)CFDictionarySetValue(d,CFSTR("original"),current);
            p.write_marker=true;p.marker=d;
        }
        p.request_wake=true;p.status="PASS";p.reason="explicit-bringup";
        if(!current_true) {p.write_value=true;p.value=CFRetain(kCFBooleanTrue);}
        return p;
    }
    if(marker) {
        /* Restore only while the current value is still the one we installed.
         * An external change is preserved, and our ownership is relinquished. */
        p.write_marker=true;p.status="PASS";
        p.reason=current_true?"restore-owned-original":"preserve-external-change";
        if(current_true) {
            p.write_value=true;
            CFTypeRef prior=CFDictionaryGetValue((CFDictionaryRef)marker,CFSTR("original"));
            if(prior)p.value=CFRetain(prior);
        }
    } else if(migrate_legacy && current_true) {
        /* Explicit opt-in for the old adapter's unmarked Boolean true only.
         * Do not erase other native preference values or unrelated keys. */
        p.write_value=true;p.status="PASS";p.reason="explicit-legacy-autodim-removal";
    } else if(current_true) {
        p.reason="unmarked-autodim-preserved";
    }
    return p;
}

static inline void zizhan_bringup_plan_release(ZizhanBringupPlan *p)
{
    if(p->value)CFRelease(p->value);
    if(p->marker)CFRelease(p->marker);
    p->value=NULL;p->marker=NULL;
}
