/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * zizhan BT-01: BlueTool's "boot" script picks its controller firmware from a table keyed by the MobileGestalt WifiVendor answer
 * (and the product hash). That answer comes from the Broadcom WLAN HAL (AppleOLYHAL vendor-id), which this Qualcomm board does not
 * have, so BlueTool loops in runCommandForCurrentConfig "fwPos == NO_FW_FOUND" / "No WiFi - Iteration" until its watchdog exits.
 *
 * This library is inserted into BlueTool only (XNU patch 0031: boot-arg pong_inj=/pong/bt.dylib:BlueTool) and interposes
 * MGCopyAnswer there: WifiVendor gets the boot-arg zizhan_bt_vendor (default "Murata"), every other key is answered by
 * MobileGestalt as before. Each distinct key BlueTool asks is logged once, to see what else the boot path depends on.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>

CFTypeRef MGCopyAnswer(CFStringRef key, CFDictionaryRef options);

static char vendor[32] = "Murata";

static void read_vendor(void)
{
    static int done;
    if (done) {
        return;
    }
    done = 1;
    char args[2048];
    size_t length = sizeof(args) - 1;
    if (sysctlbyname("kern.bootargs", args, &length, NULL, 0)) {
        return;
    }
    args[length < sizeof(args) ? length : sizeof(args) - 1] = 0;
    const char *p = strstr(args, "zizhan_bt_vendor=");
    if (!p || (p != args && p[-1] != ' ')) {
        return;
    }
    p += strlen("zizhan_bt_vendor=");
    size_t n = strcspn(p, " \t");
    if (n == 0 || n >= sizeof(vendor)) {
        return;
    }
    memcpy(vendor, p, n);
    vendor[n] = 0;
}

static void log_key(CFStringRef key)
{
    static CFMutableSetRef seen;
    if (!key || CFGetTypeID(key) != CFStringGetTypeID()) {
        return;
    }
    if (!seen) {
        seen = CFSetCreateMutable(NULL, 0, &kCFTypeSetCallBacks);
    }
    if (!seen || CFSetContainsValue(seen, key) || CFSetGetCount(seen) >= 64) {
        return;
    }
    CFSetAddValue(seen, key);
    char name[96] = {0};
    CFStringGetCString(key, name, sizeof(name), kCFStringEncodingUTF8);
    fprintf(stderr, "Zizhan BT: MGCopyAnswer %s\n", name);
}

static CFTypeRef bt_copy_answer(CFStringRef key, CFDictionaryRef options)
{
    log_key(key);
    if (key && CFGetTypeID(key) == CFStringGetTypeID() && CFEqual(key, CFSTR("WifiVendor"))) {
        read_vendor();
        fprintf(stderr, "Zizhan BT: WifiVendor -> %s\n", vendor);
        return CFStringCreateWithCString(NULL, vendor, kCFStringEncodingUTF8);
    }
    return MGCopyAnswer(key, options);
}

__attribute__((used)) static const struct {
    const void *replacement;
    const void *replacee;
} bt_interpose[] __attribute__((section("__DATA,__interpose"))) = {
    {(const void *)bt_copy_answer, (const void *)MGCopyAnswer},
};
