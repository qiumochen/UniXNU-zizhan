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
static CreateService create_service;
static CopyInformation copy_information;
static CFTypeRef service;
static bool test_mode, reported, test_finished;
static unsigned retry_ticks, test_ticks;

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

static void test_https(void)
{
    NSURLSessionConfiguration *cfg = [NSURLSessionConfiguration ephemeralSessionConfiguration];
    cfg.timeoutIntervalForRequest = 12;
    cfg.timeoutIntervalForResource = 20;
    NSURLSession *session = [NSURLSession sessionWithConfiguration:cfg];
    NSURL *url = [NSURL URLWithString:@"https://example.com/"];
    [[session dataTaskWithURL:url completionHandler:^(NSData *body, NSURLResponse *response, NSError *error) {
        NSInteger status = [response isKindOfClass:[NSHTTPURLResponse class]] ? ((NSHTTPURLResponse *)response).statusCode : 0;
        bool ok = !error && status >= 200 && status < 300 && body.length;
        fprintf(stderr, "Pong WiFi18: HTTPS test %s status=%ld bytes=%lu error=%ld\n",
                ok ? "PASS" : "FAIL", (long)status, (unsigned long)body.length, (long)error.code);
        [session finishTasksAndInvalidate];
    }] resume];
}

static void tick(void)
{
    bool ipv4 = false;
    if (!link_active(&ipv4)) {
        if (service) { CFRelease(service); service = NULL; }
        retry_ticks = 0; reported = false;
    } else if (!ipv4 && !service && retry_ticks++ % 5 == 0) {
        // IPConfiguration owns the DHCP exchange and publishes its actual
        // lease. This service exists only for an active real en0 link.
        NSDictionary *options = @{@"IPv4Entity": @{@"ConfigMethod": @"DHCP"}, @"NoPublish": @NO};
        service = create_service(CFSTR("en0"), (__bridge CFDictionaryRef)options);
        fprintf(stderr, "Pong WiFi18: DHCP service %s\n", service ? "RUNNING" : "create failed");
    }
    if (ipv4 && !reported) {
        CFDictionaryRef info = service ? copy_information(service) : NULL;
        if (!service || info) {
            fprintf(stderr, "Pong WiFi18: IPv4 %s\n", info ? "DHCP lease observed" : "already configured by system");
            reported = true;
            if (test_mode && !test_finished) { test_finished = true; test_https(); }
        }
        if (info) CFRelease(info);
    }
    if (test_mode && !test_finished && ++test_ticks >= 45) {
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
