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
static CFTypeRef setup_session, repair_session;
static CreateStore create_store;
static NSString *session_option, *repair_ipv4_key, *repair_dns_key, *repair_interface_key;
static CopyStoreValue copy_value;
static CopyStoreMultiple copy_multiple;
static int (*store_error)(void);
static NotificationKey notification_key;
static NSString *setup_ipv4_key;
static SetStoreMultiple set_store;
static uint64_t applied_sequence, last_link, observed_sequence, refresh_sequence, reconnect_sequence;
static bool cycle_test, cleanup_seen;
static unsigned phase;
static NSString *retired_setup, *retired_native, *retired_ipv4, *retired_dns, *retired_repair_interface;
static Boolean (*notify_store)(CFTypeRef, CFStringRef);
static bool system_setup;
static bool test_mode, ui_test_mode, reported, test_finished, probe_running;
static unsigned retry_ticks, test_ticks, next_probe, probe_attempts;
static const char *lease_skip;
static bool namer_diagnostics;

static void saved_network_diagnostics(void)
{
    // Read only shape/count/boolean policy, never network identities or keys.
    NSArray *paths = @[@"/Library/Preferences/SystemConfiguration/com.apple.wifi.plist",
                       @"/var/preferences/SystemConfiguration/com.apple.wifi.plist",
                       @"/var/preferences/com.apple.wifi.known-networks.plist",
                       @"/var/mobile/Library/Preferences/com.apple.wifi.plist",
                       @"/Library/Preferences/SystemConfiguration/com.apple.wifi-networks.plist",
                       @"/Library/Preferences/SystemConfiguration/com.apple.wifi.known-networks.plist",
                       @"/Library/Preferences/com.apple.wifi.known-networks.plist"];
    unsigned index = 0;
    for (NSString *path in paths) {
        NSDictionary *store = [NSDictionary dictionaryWithContentsOfFile:path];
        id known = store[@"KnownNetworks"] ?: store[@"List of known networks"] ?: store[@"Networks"];
        NSUInteger count = [known isKindOfClass:NSArray.class] || [known isKindOfClass:NSDictionary.class] ? [known count] : 0;
        fprintf(stderr, "Pong WiFi18: saved-store %u dictionary=%d entries=%lu known=%lu allow-enable-present=%d allow-enable=%d\n",
                index++, store != nil, (unsigned long)store.count, (unsigned long)count,
                store[@"AllowEnable"] != nil, [store[@"AllowEnable"] boolValue]);
        // Whitelist only scalar policy names. Do not enumerate dictionary keys:
        // known-network keys can themselves contain the network identity.
        for (NSString *key in @[@"AutoJoin", @"UnifiedAutoJoin", @"DisableWiFiAutoJoinUntilFirstUserJoin", @"UserAutoJoinState"])
            if ([store[key] isKindOfClass:NSNumber.class])
                fprintf(stderr, "Pong WiFi18: saved-policy %u %s=%ld\n", index - 1, key.UTF8String, (long)[store[key] integerValue]);
        NSArray *records = [known isKindOfClass:NSDictionary.class] ? [known allValues] :
                           [known isKindOfClass:NSArray.class] ? known : @[];
        for (NSString *key in @[@"AutoJoin", @"AutoJoinEnabled", @"Disabled", @"TemporarilyDisabled"] ) {
            unsigned present = 0, enabled = 0;
            for (id record in records) if ([record isKindOfClass:NSDictionary.class] && [record[key] isKindOfClass:NSNumber.class]) {
                present++; enabled += [record[key] boolValue];
            }
            if (present) fprintf(stderr, "Pong WiFi18: saved-record-policy %u %s present=%u true=%u\n", index - 1, key.UTF8String, present, enabled);
        }
    }
}

static NSDictionary *read_state(NSString *key)
{
    if (!copy_value || !key) return nil;
    id value = CFBridgingRelease(copy_value(setup_session ?: repair_session, (__bridge CFStringRef)key));
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
    create_store = (CreateStore)dlsym(sc, "SCDynamicStoreCreateWithOptions");
    CopyStoreMultiple copy = (CopyStoreMultiple)dlsym(sc, "SCDynamicStoreCopyMultiple");
    SetStoreMultiple set = (SetStoreMultiple)dlsym(sc, "SCDynamicStoreSetMultiple");
    const CFStringRef *session_keys = dlsym(sc, "kSCDynamicStoreUseSessionKeys");
    bool ok = false;
    NSDictionary *options, *interfaces, *keys;
    NSArray *patterns;
    NSString *base;
    if (!create_store || !copy || !set || !session_keys || !*session_keys) goto done;
    session_option = (__bridge NSString *)*session_keys;
    set_store = set;
    options = @{session_option: @YES};
    setup_session = create_store(NULL, CFSTR("Pong WiFi"), (__bridge CFDictionaryRef)options, NULL, NULL);
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
    keys = @{[base stringByAppendingString:@"/Interface"]:
                               @{@"DeviceName": @"en0", @"Type": @"Ethernet", @"Hardware": @"AirPort"},
                           setup_ipv4_key: @{@"ConfigMethod": @"DHCP"}};
    ok = set(setup_session, (__bridge CFDictionaryRef)keys, NULL, NULL);
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

static bool kernel_lease_matches(const struct pong_wifi_lease *lease, bool *mask_matches)
{
    struct ifaddrs *list = NULL;
    if (getifaddrs(&list) != 0) return false;
    bool match = false;
    for (struct ifaddrs *a = list; a; a = a->ifa_next) {
        if (strcmp(a->ifa_name, "en0") ||
            (a->ifa_flags & (IFF_UP | IFF_RUNNING)) != (IFF_UP | IFF_RUNNING) ||
            !a->ifa_addr || !a->ifa_netmask || a->ifa_addr->sa_family != AF_INET) continue;
        const struct sockaddr_in *local = (const struct sockaddr_in *)a->ifa_addr;
        const struct sockaddr_in *mask = (const struct sockaddr_in *)a->ifa_netmask;
        if (!memcmp(&local->sin_addr, lease->address, 4)) {
            match = true;
            *mask_matches = !memcmp(&mask->sin_addr, lease->mask, 4);
            break;
        }
    }
    freeifaddrs(list);
    return match;
}

static int wifi_ioctl(int32_t type, struct pong_wifi_lease *lease)
{
    struct {
        char ifname[16];
        int32_t type, value;
        uint32_t length;
        uint64_t data __attribute__((aligned(8)));
    } request = {"en0", type, 0, lease ? sizeof(*lease) : 0, (uintptr_t)lease};
    _Static_assert(sizeof(request) == 40, "Apple80211 request ABI");
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return errno;
    int result = ioctl(fd, lease ? 0xc02869c9UL : 0x802869c8UL, &request) ? errno : 0;
    close(fd);
    if (!result && lease && request.length != sizeof(*lease)) return EINVAL;
    return result;
}

static void clear_repair(const char *reason)
{
    if (!repair_session) return;
    CFRelease(repair_session); repair_session = NULL;
    repair_ipv4_key = nil; repair_dns_key = nil; repair_interface_key = nil;
    applied_sequence = 0;
    fprintf(stderr, "Pong WiFi18: supplemental route removed (%s)\n", reason);
}

/* Native IPConfiguration owns the lease and address. Recover omitted route
 * and resolver options only from its current, matching DHCP ACK captured at en0. */
static void recover_lease_options(struct pong_wifi_lease lease)
{
    lease_skip = "no dynamic store session support";
    if (!create_store || !session_option || !set_store || !copy_multiple) return;
    lease_skip = "no matching live DHCP ACK";
    if (lease.version != PONG_WIFI_LEASE_VERSION || lease.dns_count > PONG_WIFI_LEASE_DNS_MAX) return;
    NSString *address = ipv4_text(lease.address), *router = ipv4_text(lease.router);
    NSMutableArray *dns = [NSMutableArray array];
    for (unsigned i = 0; i < lease.dns_count; ++i) {
        NSString *server = ipv4_text(lease.dns[i]);
        if (server) [dns addObject:server];
    }
    lease_skip = "ACK missing usable address or route options";
    if (!address || !router || !dns.count || [router isEqual:address]) return;
    lease_skip = "live ACK address not installed in kernel";
    bool mask_matches = false;
    if (!kernel_lease_matches(&lease, &mask_matches)) return;
    lease_skip = "ACK router is outside the lease subnet";
    struct in_addr local, gateway, mask;
    memcpy(&mask, lease.mask, 4); memcpy(&local, lease.address, 4); memcpy(&gateway, lease.router, 4);
    if (!mask.s_addr || (gateway.s_addr & mask.s_addr) != (local.s_addr & mask.s_addr)) return;
    NSDictionary *global = read_state(@"State:/Network/Global/IPv4");
    NSDictionary *global_dns = read_state(@"State:/Network/Global/DNS");
    NSString *primary = global[@"PrimaryService"];
    bool own_primary = primary && [[NSString stringWithFormat:@"State:/Network/Service/%@/IPv4", primary] isEqual:repair_ipv4_key];
    if (!own_primary && global[@"Router"] && [global_dns[@"ServerAddresses"] count]) {
        clear_repair("native route and DNS");
        lease_skip = "native route and DNS";
        return;
    }
    // Never demote a real primary service on another interface.
    if (global[@"PrimaryInterface"] && ![global[@"PrimaryInterface"] isEqual:@"en0"]) {
        clear_repair("another interface is primary");
        return;
    }
    if (applied_sequence == lease.sequence && own_primary &&
        [global[@"Router"] isEqual:router] && [global_dns[@"ServerAddresses"] count]) {
        lease_skip = "applied";
        return;
    }
    if (!repair_session) {
        NSDictionary *options = @{session_option: @YES};
        repair_session = create_store(NULL, CFSTR("Pong WiFi DHCP options"),
                                     (__bridge CFDictionaryRef)options, NULL, NULL);
        if (!repair_session) return;
        NSString *identifier = NSUUID.UUID.UUIDString;
        NSString *base = [@"State:/Network/Service/" stringByAppendingString:identifier];
        repair_ipv4_key = [base stringByAppendingString:@"/IPv4"];
        repair_dns_key = [base stringByAppendingString:@"/DNS"];
        repair_interface_key = [NSString stringWithFormat:@"Setup:/Network/Service/%@/Interface", identifier];
    }
    // wifid maps State service notifications through their Setup interface
    // entity. Bind this supplemental IP service to the existing REAL en0;
    // an anonymous State-only service can route packets while wifid times
    // out waiting for its interface to become routable. No new DHCP client
    // or hardware interface is created, and all keys share session lifetime.
    NSArray *interface_patterns = @[@"Setup:/Network/Service/[^/]+/Interface"];
    NSDictionary *bindings = CFBridgingRelease(copy_multiple(repair_session, NULL, (__bridge CFArrayRef)interface_patterns));
    NSDictionary *interface = nil;
    for (NSString *key in bindings) {
        id candidate = bindings[key];
        if (![key isEqual:repair_interface_key] && [candidate isKindOfClass:NSDictionary.class] &&
            [candidate[@"DeviceName"] isEqual:@"en0"]) { interface = candidate; break; }
    }
    if (!interface) {
        lease_skip = "no live en0 setup binding";
        clear_repair("interface binding disappeared");
        return;
    }
    char netmask[INET_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, lease.mask, netmask, sizeof netmask)) return;
    NSDictionary *state = @{@"Addresses": @[address], @"SubnetMasks": @[@(netmask)],
                            @"Router": router, @"InterfaceName": @"en0",
                            @"ConfirmedInterfaceName": @"en0"};
    NSDictionary *options = @{repair_interface_key: interface, repair_ipv4_key: state,
                              repair_dns_key: @{@"ServerAddresses": dns}};
    bool ok = set_store(repair_session, (__bridge CFDictionaryRef)options, NULL, NULL);
    lease_skip = ok ? "applied" : "store rejected options";
    if (!ok) { clear_repair("store rejected options"); return; }
    if (applied_sequence != lease.sequence)
        fprintf(stderr, "Pong WiFi18: live DHCP ACK route and DNS APPLIED sequence=%llu kernel-mask-match=%d\n",
                (unsigned long long)lease.sequence, mask_matches);
    applied_sequence = lease.sequence;
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
            if (!cycle_test || !ok) return;
            if (phase == 0) {
                refresh_sequence = observed_sequence;
                CFTypeRef test_store = setup_session ?: repair_session;
                CFTypeRef temporary = !test_store && create_store ?
                    create_store(NULL, CFSTR("Pong WiFi lifecycle test"), NULL, NULL, NULL) : NULL;
                bool sent = (test_store || temporary) && notify_store && notify_store(test_store ?: temporary,
                    CFSTR("State:/Network/Interface/en0/RefreshConfiguration"));
                fprintf(stderr, "Pong WiFi18: DHCP renewal request %s status=%d\n",
                        sent ? "RUNNING" : "FAIL", !sent && store_error ? store_error() : 0);
                if (temporary) CFRelease(temporary);
                if (sent) { phase = 1; test_finished = false; }
            } else if (phase == 2) {
                reconnect_sequence = observed_sequence;
                int e = wifi_ioctl(PONG_WIFI_TEST_RECONNECT, NULL);
                fprintf(stderr, "Pong WiFi18: reconnect request %s errno=%d\n", e ? "FAIL" : "RUNNING", e);
                if (!e) { phase = 3; test_finished = false; }
            } else if (phase == 4) {
                fprintf(stderr, "Pong WiFi18: network lifecycle %s\n", cleanup_seen ? "PASS" : "FAIL: cleanup not observed");
            }
        });
        [session finishTasksAndInvalidate];
    }] resume];
}

static void tick(void)
{
    if (namer_diagnostics) {
        static unsigned ticks, previous = ~0u;
        if (ticks == 10 || ticks == 60) saved_network_diagnostics();
        if ((ticks++ % 5) == 0) {
            NSDictionary *namer = read_state(@"Plugin:InterfaceNamer");
            unsigned state = (namer ? 1u : 0u) | (namer[@"*QUIET*"] ? 2u : 0u) |
                             (namer[@"*COMPLETE*"] ? 4u : 0u) | (namer[@"*TIMEOUT*"] ? 8u : 0u);
            if (state != previous) {
                fprintf(stderr, "Pong WiFi18: native InterfaceNamer state present=%d quiet=%d complete=%d timeout=%d\n",
                        !!(state & 1), !!(state & 2), !!(state & 4), !!(state & 8));
                previous = state;
            }
        }
    }
    bool ipv4 = false;
    bool up = link_active(&ipv4);
    struct pong_wifi_lease lease = {};
    bool witness = wifi_ioctl(PONG_WIFI_GET_LEASE, &lease) == 0;
    bool changed_link = witness && last_link && lease.link != last_link;
    if (witness) last_link = lease.link;
    if (!up || changed_link) {
        if (service) { CFRelease(service); service = NULL; }
        if (phase == 3 && (setup_session || repair_session)) {
            retired_setup = setup_session ? setup_ipv4_key : nil;
            retired_native = setup_session ? state_key(setup_ipv4_key) : nil;
            retired_ipv4 = repair_ipv4_key;
            retired_dns = repair_dns_key;
            retired_repair_interface = repair_interface_key;
        }
        clear_repair("link ended");
        if (setup_session) { CFRelease(setup_session); setup_session = NULL; }
        retry_ticks = 0; reported = false; system_setup = false;
    }
    if (!up && phase == 3 && !cleanup_seen) {
        NSDictionary *global = read_state(@"State:/Network/Global/IPv4");
        cleanup_seen = !read_state(retired_setup) && !read_state(retired_native) &&
                       !read_state(retired_ipv4) && !read_state(retired_dns) &&
                       !read_state(retired_repair_interface) &&
                       ![global[@"PrimaryInterface"] isEqual:@"en0"];
        if (cleanup_seen) fprintf(stderr, "Pong WiFi18: disconnect cleanup PASS\n");
    }
    if (up && !service && !setup_session && !system_setup && retry_ticks++ == 0) {
        configure_en0();
    } else if (up && !ipv4 && setup_session && ++retry_ticks >= 25) {
        clear_repair("IPv4 lost");
        if (setup_session) { CFRelease(setup_session); setup_session = NULL; }
        fprintf(stderr, "Pong WiFi18: configured DHCP timed out; releasing session\n");
    } else if (up && !ipv4 && !service && !setup_session && !system_setup && retry_ticks >= 10 && retry_ticks % 5 == 0) {
        // IPConfiguration owns the DHCP exchange and publishes its actual lease.
        NSDictionary *options = @{@"IPv4Entity": @{@"ConfigMethod": @"DHCP"}, @"NoPublish": @NO};
        service = create_service(CFSTR("en0"), (__bridge CFDictionaryRef)options);
        fprintf(stderr, "Pong WiFi18: DHCP service %s\n", service ? "RUNNING" : "create failed");
    }
    if (up && ipv4) retry_ticks = 1;
    bool mask_matches = false;
    if (applied_sequence && (!up || !ipv4 || !witness || lease.version != PONG_WIFI_LEASE_VERSION ||
                            !kernel_lease_matches(&lease, &mask_matches)))
        clear_repair("lease invalidated");
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
    if (up && ipv4 && copy_value) recover_lease_options(lease);
    if (witness && lease.version == PONG_WIFI_LEASE_VERSION && lease.sequence != observed_sequence) {
        observed_sequence = lease.sequence;
        if (test_mode) fprintf(stderr, "Pong WiFi18: live lease sequence=%llu link=%llu\n",
            (unsigned long long)lease.sequence, (unsigned long long)lease.link);
    }
    if (!test_mode || test_finished) return;
    ++test_ticks;
    if (phase == 1 && observed_sequence > refresh_sequence && ipv4) {
        fprintf(stderr, "Pong WiFi18: DHCP renewal ACK PASS\n");
        phase = 2; probe_attempts = 0; next_probe = test_ticks + 2;
    }
    if (phase == 3 && cleanup_seen && up && ipv4 && observed_sequence > reconnect_sequence) {
        phase = 4; probe_attempts = 0; next_probe = test_ticks + 2;
    }
    if ((phase == 0 || phase == 2 || phase == 4) && reported && !probe_running && test_ticks >= next_probe) test_https();
    if (!probe_running && test_ticks >= (ui_test_mode ? 165u : cycle_test ? 130u : 45u)) {
        fprintf(stderr, "Pong WiFi18: network lifecycle FAIL: deadline phase=%u\n", phase);
        test_finished = true;
    }
}

int main(void)
{
    @autoreleasepool {
        if (!boot_option("pong_wifi=1")) return 0;
        // UI tests drive the stock Settings handlers and wifid; they must
        // not enable the kernel's independent association fixture.
        ui_test_mode = boot_option("pong_wifi_ui_test=1");
        bool kernel_test = boot_option("pong_wifi_test=1");
        if (ui_test_mode && kernel_test) {
            fprintf(stderr, "Pong WiFi18: UI test FAIL: kernel association fixture also enabled\n");
            return 1;
        }
        test_mode = kernel_test || ui_test_mode;
        namer_diagnostics = boot_option("pong_wifi_diag=1") || boot_option("pong_wifi_ui_test=1");
        cycle_test = test_mode && boot_option("pong_wifi_cycle=1");
        void *framework = dlopen("/System/Library/PrivateFrameworks/IPConfiguration.framework/IPConfiguration", RTLD_NOW);
        create_service = framework ? (CreateService)dlsym(framework, "IPConfigurationServiceCreate") : NULL;
        copy_information = framework ? (CopyInformation)dlsym(framework, "IPConfigurationServiceCopyInformation") : NULL;
        notification_key = framework ? (NotificationKey)dlsym(framework, "IPConfigurationServiceGetNotificationKey") : NULL;
        void *sc = dlopen("/System/Library/Frameworks/SystemConfiguration.framework/SystemConfiguration", RTLD_NOW);
        copy_value = sc ? (CopyStoreValue)dlsym(sc, "SCDynamicStoreCopyValue") : NULL;
        copy_multiple = sc ? (CopyStoreMultiple)dlsym(sc, "SCDynamicStoreCopyMultiple") : NULL;
        notify_store = sc ? (Boolean (*)(CFTypeRef, CFStringRef))dlsym(sc, "SCDynamicStoreNotifyValue") : NULL;
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
