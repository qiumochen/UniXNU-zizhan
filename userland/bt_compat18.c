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
#include <termios.h>
#include <stdint.h>
#include <unistd.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <pthread.h>
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
/*
 * bt-33 (2026-11-23): in iOS 18 bluetoothd the H4 transport's "POSIX" driver is the Skywalk UART
 * (kctl com.apple.uart.sk.bluetooth + os_channel, abort reason 1201 here); plain tty I/O is gone.
 * The VIRT driver (0x100184470) opens "<name>-in" write-only and "<name>-out" read-only/non-blocking
 * and its VirtualCTRL receive state machine reads H4-framed packets. Use transport "VIRT:VIRT:<name>"
 * and answer those two opens with XNU 0023's tty /dev/cu.bluetooth, in raw mode.
 */
#define BT_VIRT_NAME "/var/run/zizhan-bt"
#define BT_TTY "/dev/cu.bluetooth"
static int bt_tty_open(int flags)
{
    int fd = open(BT_TTY, flags | O_NOCTTY);
    if (fd < 0) {
        fprintf(stderr, "Zizhan BT: open %s flags=0x%x failed errno=%d\n", BT_TTY, flags, errno);
        return -1;
    }
    struct termios tio;
    if (tcgetattr(fd, &tio) == 0) {
        cfmakeraw(&tio);
        (void)tcsetattr(fd, TCSANOW, &tio);
    }
    return fd;
}
static int bt_virt_open(const char *path, int flags)
{
    static int write_fd = -1;
    size_t n = strlen(BT_VIRT_NAME);
    if (strncmp(path, BT_VIRT_NAME, n) != 0) {
        return -2;
    }
    int fd = -1;
    if (strcmp(path + n, "-in") == 0) {
        fd = bt_tty_open(O_WRONLY);
        write_fd = fd;
    } else if (strcmp(path + n, "-out") == 0) {
        fd = bt_tty_open(O_RDONLY | (flags & O_NONBLOCK));
        if (fd < 0 && write_fd >= 0) {
            fd = dup(write_fd);   /* a second open was refused: share the description (flags are shared too) */
        }
    } else {
        return -2;
    }
    fprintf(stderr, "Zizhan BT: VIRT %s -> %s fd=%d\n", path, BT_TTY, fd);
    return fd;
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
    if (path) {
        int fd = bt_virt_open(path, flags);
        if (fd != -2) {
            return fd;
        }
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

/*
 * bt-29 (2026-11-23): bluetoothd aborts with "BT VSC init failed" (reason 102) because registering a vendor-specific
 * event callback fails (list add 0x1001b64c0: 0x524 already registered, 0x525 list full, 0x6a no memory). Its capacity
 * is byte 7 of OI config 0x2c (pointer global 0x1009e33f0; the default config holds 4) and the list head is global
 * 0x1009cd130. A read-only thread samples both every millisecond for 20 s and prints every change, with callback
 * addresses converted back to the binary's static addresses (main image slide). zizhan_bt_watch=0 disables it.
 */
#define BT_VSE_CONFIG_PTR 0x1009e33f0ULL
#define BT_VSE_LIST_HEAD  0x1009cd130ULL
#define BT_OI_ZONE_PTR    0x1009c8fe8ULL   /* malloc zone of the OI allocator 0x1001436b4 (created by 0x100143614) */
#define BT_VSC_CHIP       0x1009cdd60ULL   /* 32-bit chip id stored by 0x10022288c at the start of VSC init */
#define BT_CHIP_SOURCE    0x1009c1fdcULL   /* the id VSC init passes to it */
static void *bt_vse_watch(void *arg)
{
    (void)arg;
    /* Inserted libraries come first in the image list (bt-29: image 0 was /pong/bt.dylib): find the executable. */
    uint32_t index = UINT32_MAX;
    for (uint32_t i = 0, n = _dyld_image_count(); i < n; i++) {
        const struct mach_header *header = _dyld_get_image_header(i);
        if (header && header->filetype == MH_EXECUTE) {
            index = i;
            break;
        }
    }
    const char *image = index != UINT32_MAX ? _dyld_get_image_name(index) : NULL;
    if (!image || !strstr(image, "bluetoothd")) {
        fprintf(stderr, "Zizhan BT watch: executable is %s; not watching\n", image ? image : "(null)");
        return NULL;
    }
    intptr_t slide = _dyld_get_image_vmaddr_slide(index);
    volatile uint64_t *config_ptr = (volatile uint64_t *)(uintptr_t)(BT_VSE_CONFIG_PTR + (uint64_t)slide);
    volatile uint64_t *list_head = (volatile uint64_t *)(uintptr_t)(BT_VSE_LIST_HEAD + (uint64_t)slide);
    volatile uint64_t *zone_ptr = (volatile uint64_t *)(uintptr_t)(BT_OI_ZONE_PTR + (uint64_t)slide);
    uint64_t last_config = 1, last_list = 1, last_zone = 1, last_slot[8];
    volatile uint32_t *vsc_chip = (volatile uint32_t *)(uintptr_t)(BT_VSC_CHIP + (uint64_t)slide);
    volatile uint32_t *chip_source = (volatile uint32_t *)(uintptr_t)(BT_CHIP_SOURCE + (uint64_t)slide);
    uint32_t last_vsc = 0xffffffff, last_source = 0xffffffff;
    unsigned last_capacity = 0x100;
    memset(last_slot, 0xff, sizeof(last_slot));
    fprintf(stderr, "Zizhan BT watch: slide=0x%lx\n", (unsigned long)slide);
    for (unsigned tick = 0; tick < 900000; tick++) {   /* 180 s at 0.2 ms */
        uint64_t config = *config_ptr, list = *list_head, zone = *zone_ptr;
        uint32_t vsc = *vsc_chip, source = *chip_source;
        if (vsc != last_vsc || source != last_source) {
            fprintf(stderr, "Zizhan BT watch: t=%ums vsc_chip=0x%x chip_source=0x%x\n", tick / 5, vsc, source);
            last_vsc = vsc;
            last_source = source;
        }
        if (zone != last_zone) {
            fprintf(stderr, "Zizhan BT watch: t=%ums zone=0x%llx\n", tick / 5, (unsigned long long)zone);
            last_zone = zone;
        }
        unsigned capacity = config ? ((volatile uint8_t *)(uintptr_t)config)[7] : 0;
        if (config != last_config || capacity != last_capacity || list != last_list) {
            const volatile uint8_t *c = (const volatile uint8_t *)(uintptr_t)config;
            fprintf(stderr, "Zizhan BT watch: t=%ums config=0x%llx cap=%u list=0x%llx", tick / 5, (unsigned long long)config,
                    capacity, (unsigned long long)list);
            if (config) {
                fprintf(stderr, " bytes=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x", c[0], c[1], c[2], c[3], c[4], c[5],
                        c[6], c[7], c[8], c[9], c[10], c[11]);
            }
            fprintf(stderr, "\n");
            last_config = config;
            last_capacity = capacity;
            last_list = list;
        }
        if (list && capacity) {
            for (unsigned i = 0; i < capacity && i < 8; i++) {
                uint64_t callback = ((volatile uint64_t *)(uintptr_t)list)[2 * i];
                if (callback != last_slot[i]) {
                    fprintf(stderr, "Zizhan BT watch: t=%ums slot %u callback=0x%llx static=0x%llx\n", tick / 5, i,
                            (unsigned long long)callback,
                            (unsigned long long)(callback ? callback - (uint64_t)slide : 0));
                    last_slot[i] = callback;
                }
            }
        }
        /* bt-31: VSC init comes more than 20 s after the config appears; sample every 0.2 ms for 180 s. */
        usleep(200);
    }
    fprintf(stderr, "Zizhan BT watch: done\n");
    return NULL;
}

__attribute__((constructor)) static void bt_transport(void)
{
    if (strcmp(getprogname(), "bluetoothd") != 0) {
        return;
    }
    /* "TRANSPORT:DRIVER[:ARG]". bt-32: a bare "H4" was also taken as the driver name, but the drivers are USB, POSIX,
     * APPLEBT, VIRT and PCIE*; H4 has none of its own ("FATAL ERROR - no hci driver was found", status 0x66 = abort
     * reason 102). The plist path pairs H4 with POSIX, but POSIX is Skywalk (bt-33: reason 1201): use VIRT. */
    char name[48] = "VIRT:VIRT:" BT_VIRT_NAME;
    char args[2048] = "";
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
    if (strstr(args, "zizhan_bt_watch=0") == NULL) {
        pthread_t thread;
        if (pthread_create(&thread, NULL, bt_vse_watch, NULL) == 0) {
            pthread_detach(thread);
        }
    }
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
