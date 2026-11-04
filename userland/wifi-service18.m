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
static NotificationKey notification_key;
static NSString *setup_ipv4_key;
static bool system_setup;
static bool test_mode, reported, test_finished, probe_running;
static unsigned retry_ticks, test_ticks, next_probe, probe_attempts;

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

static void test_https(void)
{
    probe_running = true;
    unsigned attempt = ++probe_attempts;
    NSDictionary *ipv4 = copy_value ? CFBridgingRelease(copy_value(NULL, CFSTR("State:/Network/Global/IPv4"))) : nil;
    NSDictionary *dns = copy_value ? CFBridgingRelease(copy_value(NULL, CFSTR("State:/Network/Global/DNS"))) : nil;
    fprintf(stderr, "Pong WiFi18: HTTPS probe %u store=%d primary-en0=%d router=%d dns=%d\n",
            attempt, copy_value != NULL, [ipv4[@"PrimaryInterface"] isEqual:@"en0"], ipv4[@"Router"] != nil,
            [dns[@"ServerAddresses"] count] != 0);
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
        retry_ticks = 0; reported = false; system_setup = false;
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
                setup_session ? [setup_ipv4_key stringByReplacingCharactersInRange:NSMakeRange(0, 5) withString:@"State"] : nil;
            if (!lease && key && copy_value) lease = CFBridgingRelease(copy_value(NULL, (__bridge CFStringRef)key));
            fprintf(stderr, "Pong WiFi18: IPv4 %s; service-state=%d router=%d\n",
                    lease ? "DHCP lease observed" : "already configured by system", lease != nil,
                    (lease[@"Router"] ?: lease[@"IPv4"][@"Router"]) != nil);
            reported = true;
            if (test_mode && !probe_attempts) next_probe = test_ticks + 4;
        }
        if (info) CFRelease(info);
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
        void *framework = dlopen("/System/Library/PrivateFrameworks/IPConfiguration.framework/IPConfiguration", RTLD_NOW);
        create_service = framework ? (CreateService)dlsym(framework, "IPConfigurationServiceCreate") : NULL;
        copy_information = framework ? (CopyInformation)dlsym(framework, "IPConfigurationServiceCopyInformation") : NULL;
        notification_key = framework ? (NotificationKey)dlsym(framework, "IPConfigurationServiceGetNotificationKey") : NULL;
        void *sc = dlopen("/System/Library/Frameworks/SystemConfiguration.framework/SystemConfiguration", RTLD_NOW);
        copy_value = sc ? (CopyStoreValue)dlsym(sc, "SCDynamicStoreCopyValue") : NULL;
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
