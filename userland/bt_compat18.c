/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * zizhan BT-01: BlueTool's "boot" script picks its controller firmware from a table keyed by the MobileGestalt WifiVendor answer
 * (and the product hash). That answer comes from the Broadcom WLAN HAL (AppleOLYHAL vendor-id), which this Qualcomm board does not
 * have, so BlueTool loops in runCommandForCurrentConfig "fwPos == NO_FW_FOUND" / "No WiFi - Iteration" until its watchdog exits.
 *
 * This library is inserted into BlueTool only (XNU patch 0031: boot-arg pong_inj=/pong/bt.dylib:BlueTool) and interposes
 * MGCopyAnswer there: WifiVendor gets the boot-arg zizhan_bt_vendor (default "Murata"), every other key is answered by
 * MobileGestalt as before. Each distinct key BlueTool asks is logged once, to see what else the boot path depends on.
 *
 * Boot script (zizhan_bt_script=1, default; 0 = off): the built-in boot script is the Broadcom one ("bcm -B", then "bcm -W"
 * downloads a Broadcom HCD and fails on this Qualcomm controller with "bcm returned -1"). BlueTool runs
 * /etc/bluetool/<HW model|platform>.boot.script instead only on Vendor or Internal installs, so ReleaseType is answered
 * "Vendor" here and that script path is served from memory (stat and fopen), without touching the system volume.
 * The controller already has its Qualcomm firmware from the kernel, so the script only opens the UART control and resets it.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>
#include <errno.h>
#include <stdlib.h>

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

static int script_mode = 1;
static void read_script_mode(void)
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
    const char *p = strstr(args, "zizhan_bt_script=");
    if (p && (p == args || p[-1] == ' ')) {
        script_mode = p[strlen("zizhan_bt_script=")] - '0';
    }
    if (strcmp(getprogname(), "BlueTool") != 0) {
        script_mode = 0;   /* bluetoothd also loads this library: it keeps the real ReleaseType */
    }
}
static const char *const scripts[] = {
    NULL,
    "device -D\nwake on\nmsleep 100\nhci reset\nbcm -N\n",
    "device -D\nhci reset\n",
};
/* init and deepsleep follow boot in the same BlueTool; their built-in versions are Broadcom too, so they become no-ops. */
static const char script_noop[] = "msleep 10\n";
/* The script for an /etc/bluetool/<name>.<boot|init|deepsleep>.script path, or NULL. */
static const char *script_for(const char *path)
{
    if (!path || strncmp(path, "/etc/bluetool/", 14) != 0) {
        return NULL;
    }
    read_script_mode();
    const size_t n = strlen(path);
    const int boot = n > 12 && strcmp(path + n - 12, ".boot.script") == 0;
    const int other = (n > 12 && strcmp(path + n - 12, ".init.script") == 0) ||
                      (n > 17 && strcmp(path + n - 17, ".deepsleep.script") == 0);
    const int serve = (boot || other) && script_mode > 0 && script_mode < 3;
    static int logged;
    if (logged++ < 16) {
        fprintf(stderr, "Zizhan BT: script path %s -> %s\n", path, serve ? "served" : "absent");
    }
    if (!serve) {
        return NULL;
    }
    return boot ? scripts[script_mode] : script_noop;
}
static int bt_stat(const char *path, struct stat *st)
{
    const char *script = script_for(path);
    if (!script) {
        return stat(path, st);
    }
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | 0444;
    st->st_nlink = 1;
    st->st_size = (off_t)strlen(script);
    return 0;
}
static FILE *bt_fopen(const char *path, const char *mode)
{
    const char *script = script_for(path);
    if (!script) {
        return fopen(path, mode);
    }
    static int runs;
    if (runs++ < 24) {
        fprintf(stderr, "Zizhan BT: running %s (mode %d)\n", strrchr(path, '/') + 1, script_mode);
    }
    return fmemopen((void *)(uintptr_t)script, strlen(script), "r");
}
static int bt_open(const char *path, int flags, ...)
{
    int mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    if (script_for(path)) {
        fprintf(stderr, "Zizhan BT: open() of the served script is not supported\n");
        errno = ENOENT;
        return -1;
    }
    return open(path, flags, mode);
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
    if (key && CFGetTypeID(key) == CFStringGetTypeID() && CFEqual(key, CFSTR("ReleaseType"))) {
        read_script_mode();
        if (script_mode > 0) {
            return CFStringCreateWithCString(NULL, "Vendor", kCFStringEncodingUTF8);   /* enables the override script lookup */
        }
    }
    return MGCopyAnswer(key, options);
}

/*
 * bluetoothd's transport (env BT_HCI_TRANSPORT > boot-arg bt-transport-override > device tree) is H4BC here: the Skywalk
 * UART (kernel control com.apple.uart.sk.bluetooth + an os_channel), which this kernel does not provide, so bluetoothd
 * exits with reason 1201 (bt-25). Its boot-arg override is not applied (bt-26, probably no kern.bootargs in its sandbox),
 * so the environment is set here, in bluetoothd only: H4 is the plain tty /dev/cu.bluetooth of XNU patch 0023.
 * zizhan_bt_transport=<name> picks another transport name (H4, H4DS, H4BC, H5, ...) when the boot-args are readable.
 */
__attribute__((constructor)) static void bt_transport(void)
{
    if (strcmp(getprogname(), "bluetoothd") != 0) {
        return;
    }
    char name[16] = "H4";
    char args[2048];
    size_t length = sizeof(args) - 1;
    if (sysctlbyname("kern.bootargs", args, &length, NULL, 0) == 0) {
        args[length < sizeof(args) ? length : sizeof(args) - 1] = 0;
        const char *p = strstr(args, "zizhan_bt_transport=");
        if (p && (p == args || p[-1] == ' ')) {
            p += strlen("zizhan_bt_transport=");
            size_t n = strcspn(p, " \t");
            if (n > 0 && n < sizeof(name)) {
                memcpy(name, p, n);
                name[n] = 0;
            }
        }
    }
    setenv("BT_HCI_TRANSPORT", name, 1);
    fprintf(stderr, "Zizhan BT: bluetoothd BT_HCI_TRANSPORT=%s\n", name);
}

__attribute__((used)) static const struct {
    const void *replacement;
    const void *replacee;
} bt_interpose[] __attribute__((section("__DATA,__interpose"))) = {
    {(const void *)bt_copy_answer, (const void *)MGCopyAnswer},
    {(const void *)bt_stat, (const void *)stat},
    {(const void *)bt_fopen, (const void *)fopen},
    {(const void *)bt_open, (const void *)open},
};
