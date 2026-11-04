/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Address configuration for the real Pong QCA6490 en0 on iPadOS 18. */
#import <Foundation/Foundation.h>
#include <arpa/inet.h>
#include <dlfcn.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include "../drivers/PongWifi/PongWifiLease.h"
#include <errno.h>
#include <unistd.h>

typedef CFTypeRef (*CreateService)(CFStringRef, CFDictionaryRef);
typedef CFDictionaryRef (*CopyInformation)(CFTypeRef);
typedef CFPropertyListRef (*CopyStoreValue)(CFTypeRef, CFStringRef);
typedef CFDictionaryRef (*CopyStoreMultiple)(CFTypeRef, CFArrayRef, CFArrayRef);
typedef CFTypeRef (*CreateStore)(CFAllocatorRef, CFStringRef, CFDictionaryRef, void *, void *);
typedef Boolean (*SetStoreMultiple)(CFTypeRef, CFDictionaryRef, CFArrayRef, CFArrayRef);
typedef CFStringRef (*NotificationKey)(CFTypeRef);
static CreateService create_service;
static CopyInformation copy_information;
static CFTypeRef service;
static CFTypeRef setup_session;
static CopyStoreValue copy_value;
static CopyStoreMultiple copy_multiple;
static int (*store_error)(void);
static NotificationKey notification_key;
static NSString *setup_ipv4_key;
static NSString *setup_dns_key;
static SetStoreMultiple set_store;
static bool lease_repaired, lease_repair_enabled;
static bool system_setup;
static bool test_mode, reported, test_finished, probe_running;
static unsigned retry_ticks, test_ticks, next_probe, probe_attempts;
static const char *lease_skip;

static NSDictionary *read_state(NSString *key)
{
    if (!copy_value || !key) return nil;
    id value = CFBridgingRelease(copy_value(setup_session, (__bridge CFStringRef)key));
    return [value isKindOfClass:NSDictionary.class] ? value : nil;
}

static NSString *state_key(NSString *setup)
{
    return [@"State:" stringByAppendingString:[setup substringFromIndex:6]];
}

static bool boot_option(const char *key)
{
    char args[2048] = {0}; size_t n = sizeof args - 1;
    if (sysctlbyname("kern.bootargs", args, &n, NULL, 0) != 0) return false;
    char *save = NULL;
    for (char *arg = strtok_r(args, " ", &save); arg; arg = strtok_r(NULL, " ", &save))
        if (!strcmp(arg, key)) return true;
    return false;
}

static bool link_active(bool *ipv4)
{
    struct ifaddrs *list = NULL; bool link = false; *ipv4 = false;
    if (getifaddrs(&list) != 0) return false;
    for (struct ifaddrs *a = list; a; a = a->ifa_next) {
        if (strcmp(a->ifa_name, "en0") || (a->ifa_flags & (IFF_UP | IFF_RUNNING)) != (IFF_UP | IFF_RUNNING)) continue;
        link = true;
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
        uint32_t ip = ntohl(((struct sockaddr_in *)a->ifa_addr)->sin_addr.s_addr);
        if (ip && (ip >> 24) != 127 && (ip >> 24) < 224 && (ip >> 16) != 0xa9fe) *ipv4 = true;
    }
    freeifaddrs(list);
    return link;
}

/* A configured DHCP service can be elected primary by IPMonitor. Register
 * only this actual interface and only when no existing setup owns en0. The
 * keys belong to the session and vanish on disconnect or process exit. */
static bool configure_en0(void)
{
    void *sc = dlopen("/System/Library/Frameworks/SystemConfiguration.framework/SystemConfiguration", RTLD_NOW);
    if (!sc) return false;
    CreateStore create = (CreateStore)dlsym(sc, "SCDynamicStoreCreateWithOptions");
    CopyStoreMultiple copy = (CopyStoreMultiple)dlsym(sc, "SCDynamicStoreCopyMultiple");
    SetStoreMultiple set = (SetStoreMultiple)dlsym(sc, "SCDynamicStoreSetMultiple");
    const CFStringRef *session_keys = dlsym(sc, "kSCDynamicStoreUseSessionKeys");
    bool ok = false;
    NSDictionary *options, *interfaces, *keys;
    NSArray *patterns;
    NSString *base;
    if (!create || !copy || !set || !session_keys) goto done;
    options = @{(__bridge NSString *)*session_keys: @YES};
    setup_session = create(NULL, CFSTR("Pong WiFi"), (__bridge CFDictionaryRef)options, NULL, NULL);
    if (!setup_session) goto done;
    patterns = @[@"Setup:/Network/Service/[^/]+/Interface"];
    interfaces = CFBridgingRelease(copy(setup_session, NULL, (__bridge CFArrayRef)patterns));
    if (!interfaces) goto done;
    for (NSDictionary *entity in interfaces.allValues)
        if ([entity[@"DeviceName"] isEqual:@"en0"]) {
            system_setup = true;
            fprintf(stderr, "Pong WiFi18: existing en0 network setup; waiting for system DHCP\n");
            goto done;
        }
    base = [@"Setup:/Network/Service/" stringByAppendingString:NSUUID.UUID.UUIDString];
    setup_ipv4_key = [base stringByAppendingString:@"/IPv4"];
    setup_dns_key = [base stringByAppendingString:@"/DNS"];
    keys = @{[base stringByAppendingString:@"/Interface"]:
                               @{@"DeviceName": @"en0", @"Type": @"Ethernet", @"Hardware": @"AirPort"},
                           setup_ipv4_key: @{@"ConfigMethod": @"DHCP"}};
    ok = set(setup_session, (__bridge CFDictionaryRef)keys, NULL, NULL);
    if (ok) set_store = set;
    fprintf(stderr, "Pong WiFi18: en0 network setup %s\n", ok ? "RUNNING" : "rejected");
done:
    if (!ok && setup_session) { CFRelease(setup_session); setup_session = NULL; }
    dlclose(sc);
    return ok;
}

static NSString *ipv4_text(const uint8_t *bytes)
{
    char text[INET_ADDRSTRLEN];
    if (!bytes[0] || bytes[0] == 127 || bytes[0] >= 224) return nil;
    return inet_ntop(AF_INET, bytes, text, sizeof text) ? @(text) : nil;
}

/* Native IPConfiguration owns the lease and address. Recover omitted route
 * and resolver options only from its current, matching DHCP ACK captured at en0. */
static void recover_lease_options(NSDictionary *state)
{
    lease_skip = "no owned en0 IPv4 state";
    if (!setup_session || !set_store || lease_repaired || !state ||
        ![state[@"InterfaceName"] isEqual:@"en0"]) return;
    lease_skip = "native router present";
    if (state[@"Router"]) return;
    struct pong_wifi_lease lease = {};
    struct {
        char ifname[16];
        int32_t type, value;
        uint32_t length;
        uint64_t data __attribute__((aligned(8)));
    } request = {"en0", PONG_WIFI_GET_LEASE, 0, sizeof(lease), (uintptr_t)&lease};
    _Static_assert(sizeof(request) == 40, "Apple80211 request ABI");
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { lease_skip = "witness socket unavailable"; return; }
    int status = ioctl(fd, 0xc02869c9UL, &request);
    close(fd);
    lease_skip = "no matching live DHCP ACK";
    if (status || request.length != sizeof(lease) || lease.version != PONG_WIFI_LEASE_VERSION ||
        lease.dns_count > PONG_WIFI_LEASE_DNS_MAX) return;
    NSString *address = ipv4_text(lease.address), *router = ipv4_text(lease.router);
    NSMutableArray *dns = [NSMutableArray array];
    for (unsigned i = 0; i < lease.dns_count; ++i) {
        NSString *server = ipv4_text(lease.dns[i]);
        if (server) [dns addObject:server];
    }
    lease_skip = "ACK address, subnet or options mismatch";
    if (!address || !router || !dns.count || [router isEqual:address] ||
        ![state[@"Addresses"] containsObject:address]) return;
    struct in_addr local, gateway, mask;
    NSString *subnet = [state[@"SubnetMasks"] firstObject];
    if (!subnet || inet_pton(AF_INET, subnet.UTF8String, &mask) != 1 ||
        memcmp(&mask, lease.mask, sizeof(mask)) ||
        inet_pton(AF_INET, address.UTF8String, &local) != 1 ||
        inet_pton(AF_INET, router.UTF8String, &gateway) != 1 ||
        (gateway.s_addr & mask.s_addr) != (local.s_addr & mask.s_addr)) return;
    NSDictionary *options = @{setup_ipv4_key: @{@"ConfigMethod": @"DHCP", @"Router": router},
                              setup_dns_key: @{@"ServerAddresses": dns}};
    lease_repaired = set_store(setup_session, (__bridge CFDictionaryRef)options, NULL, NULL);
    lease_skip = lease_repaired ? "applied" : "store rejected options";
    fprintf(stderr, "Pong WiFi18: live DHCP ACK route and DNS %s\n", lease_repaired ? "APPLIED" : "rejected");
}

static int kernel_route_error(void)
{
    /* UDP connect performs a route lookup without sending a packet. */
    struct sockaddr_in target = {.sin_len = sizeof(target), .sin_family = AF_INET, .sin_port = htons(443)};
    inet_pton(AF_INET, "1.1.1.1", &target.sin_addr);
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return errno;
    int error = connect(fd, (const struct sockaddr *)&target, sizeof(target)) < 0 ? errno : 0;
    close(fd);
    return error;
}

static void test_https(void)
{
    probe_running = true;
    unsigned attempt = ++probe_attempts;
    NSDictionary *ipv4 = read_state(@"State:/Network/Global/IPv4");
    int ipv4_error = !ipv4 && store_error ? store_error() : 0;
    NSDictionary *dns = read_state(@"State:/Network/Global/DNS");
    fprintf(stderr, "Pong WiFi18: HTTPS probe %u store=%d primary-en0=%d router=%d dns=%d\n",
            attempt, copy_value != NULL, [ipv4[@"PrimaryInterface"] isEqual:@"en0"], ipv4[@"Router"] != nil,
            [dns[@"ServerAddresses"] count] != 0);
    NSArray *patterns = @[@"^State:/Network/Service/[^/]+/IPv4$"];
    NSDictionary *states = copy_multiple ? CFBridgingRelease(copy_multiple(setup_session, NULL, (__bridge CFArrayRef)patterns)) : nil;
    unsigned en0_states = 0, en0_routers = 0;
    for (NSDictionary *state in states.allValues)
        if ([state[@"InterfaceName"] isEqual:@"en0"]) {
            ++en0_states;
            en0_routers += state[@"Router"] != nil;
        }
    NSDictionary *own = setup_session ? read_state(state_key(setup_ipv4_key)) : nil;
    fprintf(stderr, "Pong WiFi18: route diagnosis kernel-error=%d global-error=%d en0-states=%u en0-routers=%u owned-state=%d lease=%s\n",
            kernel_route_error(), ipv4_error, en0_states, en0_routers, own != nil, lease_skip ?: "not checked");
    NSURLSessionConfiguration *cfg = [NSURLSessionConfiguration ephemeralSessionConfiguration];
    cfg.timeoutIntervalForRequest = 12;
    cfg.timeoutIntervalForResource = 20;
    NSURLSession *session = [NSURLSession sessionWithConfiguration:cfg];
    NSURL *url = [NSURL URLWithString:@"https://example.com/"];
    [[session dataTaskWithURL:url completionHandler:^(NSData *body, NSURLResponse *response, NSError *error) {
        NSInteger status = [response isKindOfClass:[NSHTTPURLResponse class]] ? ((NSHTTPURLResponse *)response).statusCode : 0;
        bool ok = !error && status >= 200 && status < 300 && body.length;
        dispatch_async(dispatch_get_main_queue(), ^{
            bool final = ok || attempt >= 4;
            fprintf(stderr, "Pong WiFi18: HTTPS %s status=%ld bytes=%lu error=%ld attempt=%u\n",
                    ok ? "test PASS" : final ? "test FAIL" : "probe failed; retrying",
                    (long)status, (unsigned long)body.length, (long)error.code, attempt);
            test_finished = final;
            probe_running = false;
            next_probe = test_ticks + 4;
        });
        [session finishTasksAndInvalidate];
    }] resume];
}

static void tick(void)
{
    bool ipv4 = false;
    if (!link_active(&ipv4)) {
        if (service) { CFRelease(service); service = NULL; }
        if (setup_session) { CFRelease(setup_session); setup_session = NULL; }
        retry_ticks = 0; reported = false; system_setup = false; lease_repaired = false;
    } else if (!ipv4 && !service && ++retry_ticks == 1) {
        configure_en0();
    } else if (!ipv4 && setup_session && retry_ticks == 25) {
        CFRelease(setup_session); setup_session = NULL;
        fprintf(stderr, "Pong WiFi18: configured DHCP timed out; releasing session\n");
    } else if (!ipv4 && !service && !setup_session && !system_setup && retry_ticks >= 10 && retry_ticks % 5 == 0) {
        // IPConfiguration owns the DHCP exchange and publishes its actual
        // lease. This service exists only for an active real en0 link.
        NSDictionary *options = @{@"IPv4Entity": @{@"ConfigMethod": @"DHCP"}, @"NoPublish": @NO};
        service = create_service(CFSTR("en0"), (__bridge CFDictionaryRef)options);
        fprintf(stderr, "Pong WiFi18: DHCP service %s\n", service ? "RUNNING" : "create failed");
    }
    if (ipv4 && !reported) {
        CFDictionaryRef info = service ? copy_information(service) : NULL;
        if (!service || info) {
            NSDictionary *lease = (__bridge NSDictionary *)info;
            NSString *key = service && notification_key ? (__bridge NSString *)notification_key(service) :
                setup_session ? state_key(setup_ipv4_key) : nil;
            if (!lease) lease = read_state(key);
            fprintf(stderr, "Pong WiFi18: IPv4 %s; service-state=%d router=%d\n",
                    lease ? "DHCP lease observed" : "already configured by system", lease != nil,
                    (lease[@"Router"] ?: lease[@"IPv4"][@"Router"]) != nil);
            reported = true;
            if (test_mode && !probe_attempts) next_probe = test_ticks + 4;
        }
        if (info) CFRelease(info);
    }
    if (ipv4 && setup_session && lease_repair_enabled && !lease_repaired && copy_value) {
        recover_lease_options(read_state(state_key(setup_ipv4_key)));
    }
    if (!test_mode || test_finished) return;
    ++test_ticks;
    if (reported && !probe_running && test_ticks >= next_probe) test_https();
    if (!reported && !probe_running && test_ticks >= 45) {
        fprintf(stderr, "Pong WiFi18: address test FAIL: no usable IPv4 within 90 seconds\n");
        test_finished = true;
    }
}

int main(void)
{
    @autoreleasepool {
        if (!boot_option("pong_wifi=1")) return 0;
        test_mode = boot_option("pong_wifi_test=1");
        lease_repair_enabled = boot_option("pong_wifi_lease_repair=1");
        void *framework = dlopen("/System/Library/PrivateFrameworks/IPConfiguration.framework/IPConfiguration", RTLD_NOW);
        create_service = framework ? (CreateService)dlsym(framework, "IPConfigurationServiceCreate") : NULL;
        copy_information = framework ? (CopyInformation)dlsym(framework, "IPConfigurationServiceCopyInformation") : NULL;
        notification_key = framework ? (NotificationKey)dlsym(framework, "IPConfigurationServiceGetNotificationKey") : NULL;
        void *sc = dlopen("/System/Library/Frameworks/SystemConfiguration.framework/SystemConfiguration", RTLD_NOW);
        copy_value = sc ? (CopyStoreValue)dlsym(sc, "SCDynamicStoreCopyValue") : NULL;
        copy_multiple = sc ? (CopyStoreMultiple)dlsym(sc, "SCDynamicStoreCopyMultiple") : NULL;
        store_error = sc ? (int (*)(void))dlsym(sc, "SCError") : NULL;
        if (!create_service || !copy_information) {
            fprintf(stderr, "Pong WiFi18: DHCP service SKIP: native IPConfiguration unavailable\n");
            return 1;
        }
        fprintf(stderr, "Pong WiFi18: waiting for en0 link\n");
        dispatch_source_t timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
        dispatch_source_set_timer(timer, DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC, NSEC_PER_MSEC * 100);
        dispatch_source_set_event_handler(timer, ^{ @autoreleasepool { tick(); } });
        dispatch_resume(timer);
        dispatch_main();
    }
}
