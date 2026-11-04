/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef PONG_WIFI_LEASE_H
#define PONG_WIFI_LEASE_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define PONG_WIFI_GET_LEASE 0x50574c01
#define PONG_WIFI_LEASE_VERSION 1
#define PONG_WIFI_LEASE_DNS_MAX 4

struct pong_wifi_lease {
    uint32_t version, dns_count;
    uint8_t address[4], mask[4], router[4], dns[PONG_WIFI_LEASE_DNS_MAX][4];
};

static inline unsigned pong_wifi_be16(const uint8_t *p) { return (p[0] << 8) | p[1]; }

/* Shared parser for the live DHCP witness. It returns only the fields used to
 * restore native publication. It does not change, accept, or transmit packets. */
static inline unsigned pong_wifi_dhcp_fields(const uint8_t *b, size_t n,
    const uint8_t mac[6], struct pong_wifi_lease *lease)
{
    if (n < 241 || b[1] != 1 || b[2] != 6 || memcmp(b + 28, mac, 6) ||
        memcmp(b + 236, "\x63\x82\x53\x63", 4)) return 0;
    unsigned type = 0;
    bool end = false, classless = false, mask = false, router = false;
    struct pong_wifi_lease result = {};
    memcpy(result.address, b + 16, 4);
    for (size_t i = 240; i < n;) {
        const unsigned code = b[i++];
        if (!code) continue;
        if (code == 255) { end = true; break; }
        if (i == n) return 0;
        const unsigned len = b[i++];
        if (len > n - i) return 0;
        if (code == 53 && len == 1) type = b[i];
        if (code == 121 || code == 249) classless = true;
        if (code == 1 && len == 4) { memcpy(result.mask, b + i, 4); mask = true; }
        if (code == 3 && len >= 4 && len % 4 == 0) { memcpy(result.router, b + i, 4); router = true; }
        if (code == 6 && len % 4 == 0)
            for (unsigned j = 0; j < len && result.dns_count < PONG_WIFI_LEASE_DNS_MAX; j += 4)
                memcpy(result.dns[result.dns_count++], b + i + j, 4);
        i += len;
    }
    if (!end) return 0;
    if (type == 5 && b[0] == 2 && !classless && mask && router && result.dns_count) {
        result.version = PONG_WIFI_LEASE_VERSION;
        *lease = result;
    }
    return type;
}
#endif
