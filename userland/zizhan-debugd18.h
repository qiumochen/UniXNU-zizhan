/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* zizhan DBG-01: LAN Live Debug HTTP Server + Web Remote Console + Safe Hot-UI Loader.
 *
 * Hosted inside `backboardd` when `zizhan_debugd=1` (zero launchd cache edits,
 * survives SpringBoard hot-reloads/crashes). Also compiles standalone under
 * `-DZIZHAN_DEBUGD_HOST_TEST` on macOS and Ubuntu Linux with ASan/UBSan.
 */
#ifndef ZIZHAN_DEBUGD18_H
#define ZIZHAN_DEBUGD18_H

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#ifndef ZIZHAN_DEBUGD_HOST_TEST
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <notify.h>
#include <sys/sysctl.h>
#endif

#include "zizhan-debug-proto.h"
#include "zizhan-ipa-install18.h"

#ifdef ZIZHAN_DEBUGD_HOST_TEST
typedef struct {
    int (*read_klog)(const ZizhanDbgKlogReq *req, ZizhanDbgKlogHdr *hdr_out,
                     uint8_t *buf, size_t buf_cap);
    int (*read_fb)(const ZizhanDbgFbReq *req, ZizhanDbgFbHdr *hdr_out,
                   uint8_t **pixels_out, size_t *pixels_len_out);
    int (*ctl)(const ZizhanDbgCtlReq *req, ZizhanDbgStatusResp *resp_out);
    int (*sysctl_get)(const char *name, char *val_out, size_t val_cap);
    int (*sysctl_set)(const char *name, const char *val_str);
    int (*open_cmd)(const char *cmd);
    int (*stage_ui)(const uint8_t *bytes, size_t len);
} ZizhanDebugdHostOps;

static ZizhanDebugdHostOps g_zdbg_host_ops;
static char g_zdbg_hot_ui_dir_override[256];
#endif

static char     g_zdbg_token[ZIZHAN_DBG_TOKEN_MAX_LEN + 1];
static size_t   g_zdbg_token_len;
static bool     g_zdbg_allow_notoken;
static bool     g_zdbg_user_present;
static uint16_t g_zdbg_bound_port;
static int      g_zdbg_listen_fd = -1;
static volatile int g_zdbg_stop_flag;
static volatile uint32_t g_zdbg_active_clients;
static volatile uint64_t g_zdbg_trial_gen;
static pthread_mutex_t g_zdbg_reload_mtx = PTHREAD_MUTEX_INITIALIZER;

static int zdbg_send_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t sent = 0;
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags |= MSG_NOSIGNAL;
#endif
    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, flags);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static void zdbg_send_response(int fd, int code, const char *status_txt,
                               const char *ctype, const void *body, size_t body_len,
                               const char *extra_hdrs) {
    char hdr[768];
    int n = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %zu\r\n"
                     "Cache-Control: no-store\r\n"
                     "Referrer-Policy: no-referrer\r\n"
                     "X-Content-Type-Options: nosniff\r\n"
                     "Connection: close\r\n"
                     "%s"
                     "\r\n",
                     code, status_txt, ctype, body_len, extra_hdrs ? extra_hdrs : "");
    if (n > 0) {
        size_t hn = (size_t)n < sizeof(hdr) ? (size_t)n : (sizeof(hdr) - 1u);
        (void)zdbg_send_all(fd, hdr, hn);
    }
    if (body && body_len > 0u) {
        (void)zdbg_send_all(fd, body, body_len);
    }
}

static void zdbg_send_json(int fd, int code, const char *status_txt, const char *json_str) {
    zdbg_send_response(fd, code, status_txt, "application/json; charset=utf-8",
                       json_str, json_str ? strlen(json_str) : 0u, NULL);
}

static void zdbg_json_escape(const char *src, char *dst, size_t cap) {
    if (!dst || cap == 0u) {
        return;
    }
    size_t j = 0u;
    for (size_t i = 0u; src && src[i] != '\0' && j + 6u < cap; ++i) {
        const unsigned char c = (unsigned char)src[i];
        if (c == '"' || c == '\\') {
            dst[j++] = '\\';
            dst[j++] = (char)c;
        } else if (c == '\n') {
            dst[j++] = '\\';
            dst[j++] = 'n';
        } else if (c == '\r') {
            dst[j++] = '\\';
            dst[j++] = 'r';
        } else if (c == '\t') {
            dst[j++] = '\\';
            dst[j++] = 't';
        } else if (c >= 0x20u && c < 0x7fu) {
            dst[j++] = (char)c;
        } else {
            dst[j++] = '?';
        }
    }
    dst[j] = '\0';
}

static int zdbg_hex_val(char c) {
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return 10 + (c - 'a'); }
    if (c >= 'A' && c <= 'F') { return 10 + (c - 'A'); }
    return -1;
}

static void zdbg_url_decode(const char *src, size_t src_len, char *dst, size_t dst_cap) {
    if (!dst || dst_cap == 0u) {
        return;
    }
    size_t j = 0u;
    for (size_t i = 0u; i < src_len && j + 1u < dst_cap; ++i) {
        if (src[i] == '%' && i + 2u < src_len) {
            int hi = zdbg_hex_val(src[i + 1u]);
            int lo = zdbg_hex_val(src[i + 2u]);
            if (hi >= 0 && lo >= 0) {
                char decoded = (char)((hi << 4) | lo);
                if (decoded != '\0') {
                    dst[j++] = decoded;
                }
                i += 2u;
                continue;
            }
        } else if (src[i] == '+') {
            dst[j++] = ' ';
            continue;
        }
        dst[j++] = src[i];
    }
    dst[j] = '\0';
}

static bool zdbg_query_param(const char *query, const char *key, char *out, size_t out_cap) {
    if (!query || !key || !out || out_cap == 0u) {
        return false;
    }
    const size_t klen = strlen(key);
    const char *p = query;
    while (*p != '\0') {
        const char *amp = strchr(p, '&');
        const size_t seg_len = amp ? (size_t)(amp - p) : strlen(p);
        const char *eq = (const char *)memchr(p, '=', seg_len);
        if (eq && (size_t)(eq - p) == klen && memcmp(p, key, klen) == 0) {
            const char *val = eq + 1;
            const size_t vlen = seg_len - klen - 1u;
            zdbg_url_decode(val, vlen, out, out_cap);
            return true;
        }
        if (!amp) {
            break;
        }
        p = amp + 1;
    }
    return false;
}

static int64_t zdbg_query_i64(const char *query, const char *key, int64_t def_val) {
    char buf[40];
    if (!zdbg_query_param(query, key, buf, sizeof(buf)) || buf[0] == '\0') {
        return def_val;
    }
    char *end = NULL;
    long long v = strtoll(buf, &end, 0);
    if (end == buf || *end != '\0') {
        return def_val;
    }
    return (int64_t)v;
}

static int zdbg_ascii_strncasecmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca >= 'A' && ca <= 'Z') { ca = (unsigned char)(ca + ('a' - 'A')); }
        if (cb >= 'A' && cb <= 'Z') { cb = (unsigned char)(cb + ('a' - 'A')); }
        if (ca != cb || ca == '\0') {
            return (int)ca - (int)cb;
        }
    }
    return 0;
}

static bool zdbg_header_value(const char *hdrs, const char *name, char *out, size_t out_cap) {
    if (!hdrs || !name || !out || out_cap == 0u) {
        return false;
    }
    const size_t nlen = strlen(name);
    const char *line = strstr(hdrs, "\r\n");
    if (!line) {
        return false;
    }
    line += 2;
    while (*line != '\0' && !(line[0] == '\r' && line[1] == '\n')) {
        const char *eol = strstr(line, "\r\n");
        if (!eol) {
            break;
        }
        const char *colon = (const char *)memchr(line, ':', (size_t)(eol - line));
        if (colon && (size_t)(colon - line) == nlen &&
            zdbg_ascii_strncasecmp(line, name, nlen) == 0) {
            const char *v = colon + 1;
            while (v < eol && (*v == ' ' || *v == '\t')) {
                ++v;
            }
            size_t vlen = (size_t)(eol - v);
            while (vlen > 0u && (v[vlen - 1u] == ' ' || v[vlen - 1u] == '\t')) {
                --vlen;
            }
            if (vlen >= out_cap) {
                vlen = out_cap - 1u;
            }
            memcpy(out, v, vlen);
            out[vlen] = '\0';
            return true;
        }
        line = eol + 2;
    }
    return false;
}

static bool zdbg_check_request_auth(const char *hdrs, const char *query) {
    if (g_zdbg_allow_notoken) {
        return true;
    }
    char cand[ZIZHAN_DBG_TOKEN_MAX_LEN + 32];
    if (zdbg_header_value(hdrs, "Authorization", cand, sizeof(cand))) {
        if (zdbg_ascii_strncasecmp(cand, "Bearer ", 7u) == 0) {
            const char *tok = cand + 7;
            while (*tok == ' ') { ++tok; }
            if (zizhan_dbg_token_eq(g_zdbg_token, g_zdbg_token_len, tok, strlen(tok))) {
                return true;
            }
        }
    }
    if (zdbg_header_value(hdrs, "X-Zizhan-Token", cand, sizeof(cand))) {
        if (zizhan_dbg_token_eq(g_zdbg_token, g_zdbg_token_len, cand, strlen(cand))) {
            return true;
        }
    }
    if (zdbg_query_param(query, "token", cand, sizeof(cand))) {
        if (zizhan_dbg_token_eq(g_zdbg_token, g_zdbg_token_len, cand, strlen(cand))) {
            return true;
        }
    }
    return false;
}

/* --- Backend Adapters (Kernel sysctls on iPadOS, Mockable Ops in Host Tests) --- */

static int zdbg_backend_ctl(const ZizhanDbgCtlReq *req, ZizhanDbgStatusResp *resp) {
#ifdef ZIZHAN_DEBUGD_HOST_TEST
    if (g_zdbg_host_ops.ctl) {
        return g_zdbg_host_ops.ctl(req, resp);
    }
    return ENXIO;
#else
    size_t rlen = sizeof(*resp);
    memset(resp, 0, sizeof(*resp));
    if (sysctlbyname("kern.zizhan_dbg_ctl", resp, &rlen, (void *)(uintptr_t)req, sizeof(*req)) != 0) {
        return errno ? errno : EIO;
    }
    return 0;
#endif
}

static int zdbg_backend_klog(const ZizhanDbgKlogReq *req, ZizhanDbgKlogHdr *hdr,
                             uint8_t *payload, size_t payload_cap) {
#ifdef ZIZHAN_DEBUGD_HOST_TEST
    if (g_zdbg_host_ops.read_klog) {
        return g_zdbg_host_ops.read_klog(req, hdr, payload, payload_cap);
    }
    return ENXIO;
#else
    const size_t total_cap = sizeof(*hdr) + payload_cap;
    uint8_t *tmp = (uint8_t *)malloc(total_cap);
    if (!tmp) {
        return ENOMEM;
    }
    size_t got = total_cap;
    int rc = sysctlbyname("kern.zizhan_dbg_klog", tmp, &got, (void *)(uintptr_t)req, sizeof(*req));
    if (rc != 0) {
        int err = errno ? errno : EIO;
        free(tmp);
        return err;
    }
    if (got < sizeof(*hdr)) {
        free(tmp);
        return EIO;
    }
    memcpy(hdr, tmp, sizeof(*hdr));
    size_t body = got - sizeof(*hdr);
    if (body < hdr->byte_len) {
        hdr->byte_len = (uint32_t)body;
    }
    if (hdr->byte_len > payload_cap) {
        hdr->byte_len = (uint32_t)payload_cap;
    }
    if (hdr->byte_len > 0u) {
        memcpy(payload, tmp + sizeof(*hdr), hdr->byte_len);
    }
    free(tmp);
    return 0;
#endif
}

static int zdbg_backend_fb(const ZizhanDbgFbReq *req, ZizhanDbgFbHdr *hdr,
                           uint8_t **pixels_out, size_t *pixels_len_out) {
#ifdef ZIZHAN_DEBUGD_HOST_TEST
    if (g_zdbg_host_ops.read_fb) {
        return g_zdbg_host_ops.read_fb(req, hdr, pixels_out, pixels_len_out);
    }
    return ENXIO;
#else
    size_t total = 0;
    if (sysctlbyname("kern.zizhan_dbg_fb", NULL, &total, (void *)(uintptr_t)req, sizeof(*req)) != 0) {
        return errno ? errno : EAGAIN;
    }
    if (total < sizeof(*hdr) || total > (32u + 4096u * 4096u * 4u)) {
        return EIO;
    }
    uint8_t *raw = (uint8_t *)malloc(total);
    if (!raw) {
        return ENOMEM;
    }
    size_t got = total;
    if (sysctlbyname("kern.zizhan_dbg_fb", raw, &got, (void *)(uintptr_t)req, sizeof(*req)) != 0 ||
        got < sizeof(*hdr)) {
        int err = errno ? errno : EIO;
        free(raw);
        return err;
    }
    memcpy(hdr, raw, sizeof(*hdr));
    const size_t need_px = (size_t)hdr->stride * (size_t)hdr->height;
    if (got < sizeof(*hdr) + need_px || need_px == 0u) {
        free(raw);
        return EIO;
    }
    uint8_t *px = (uint8_t *)malloc(need_px);
    if (!px) {
        free(raw);
        return ENOMEM;
    }
    memcpy(px, raw + sizeof(*hdr), need_px);
    free(raw);
    *pixels_out = px;
    *pixels_len_out = need_px;
    return 0;
#endif
}

static int zdbg_backend_open_cmd(const char *cmd) {
    if (!cmd || cmd[0] == '\0' || strlen(cmd) > 220u) {
        return EINVAL;
    }
    for (size_t i = 0; cmd[i] != '\0'; ++i) {
        if ((unsigned char)cmd[i] < 0x20u || (unsigned char)cmd[i] == 0x7fu) {
            return EINVAL;
        }
    }
#ifdef ZIZHAN_DEBUGD_HOST_TEST
    if (g_zdbg_host_ops.open_cmd) {
        return g_zdbg_host_ops.open_cmd(cmd);
    }
    return 0;
#else
    (void)mkdir(ZIZHAN_DBG_CMD_DIR, 0755);
    int dfd = open(ZIZHAN_DBG_CMD_DIR, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (dfd < 0) {
        return errno ? errno : EIO;
    }
    const char *tmp_name = "debug-open-cmd.tmp";
    (void)unlinkat(dfd, tmp_name, 0);
    int fd = openat(dfd, tmp_name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) {
        int e = errno ? errno : EIO;
        close(dfd);
        return e;
    }
    const size_t len = strlen(cmd);
    ssize_t wr = write(fd, cmd, len);
    (void)close(fd);
    if (wr != (ssize_t)len || renameat(dfd, tmp_name, dfd, ZIZHAN_DBG_CMD_NAME) != 0) {
        (void)unlinkat(dfd, tmp_name, 0);
        close(dfd);
        return EIO;
    }
    close(dfd);
    notify_post(ZIZHAN_DBG_DARWIN_NOTIFY);
    return 0;
#endif
}

static int zdbg_backend_sysctl_get(const char *name, char *val_out, size_t val_cap) {
    if (!zizhan_dbg_sysctl_name_valid(name)) {
        return EPERM;
    }
#ifdef ZIZHAN_DEBUGD_HOST_TEST
    if (g_zdbg_host_ops.sysctl_get) {
        return g_zdbg_host_ops.sysctl_get(name, val_out, val_cap);
    }
    return ENOENT;
#else
    uint8_t raw[1024];
    size_t sz = sizeof(raw) - 1u;
    if (sysctlbyname(name, raw, &sz, NULL, 0) != 0) {
        return errno ? errno : ENOENT;
    }
    if (sz == 4u) {
        int32_t iv = 0;
        memcpy(&iv, raw, 4u);
        uint32_t uv = (uint32_t)iv;
        snprintf(val_out, val_cap, "%d (0x%x)", iv, uv);
        return 0;
    }
    if (sz == 8u) {
        uint64_t qv = 0;
        memcpy(&qv, raw, 8u);
        snprintf(val_out, val_cap, "%llu (0x%llx)",
                 (unsigned long long)qv, (unsigned long long)qv);
        return 0;
    }
    raw[sz < sizeof(raw) ? sz : (sizeof(raw) - 1u)] = '\0';
    bool printable = true;
    for (size_t i = 0; i < sz && raw[i] != '\0'; ++i) {
        if ((raw[i] < 0x20u && raw[i] != '\t' && raw[i] != '\n') || raw[i] >= 0x7fu) {
            printable = false;
            break;
        }
    }
    if (printable) {
        snprintf(val_out, val_cap, "%s", (const char *)raw);
    } else {
        size_t pos = 0;
        pos += (size_t)snprintf(val_out, val_cap, "hex:");
        for (size_t i = 0; i < sz && i < 64u && pos + 3u < val_cap; ++i) {
            pos += (size_t)snprintf(val_out + pos, val_cap - pos, "%02x", raw[i]);
        }
    }
    return 0;
#endif
}

static int zdbg_backend_sysctl_set(const char *name, const char *val_str) {
    if (!zizhan_dbg_sysctl_write_allowed(name, val_str, g_zdbg_user_present ? 1 : 0)) {
        return EPERM;
    }
#ifdef ZIZHAN_DEBUGD_HOST_TEST
    if (g_zdbg_host_ops.sysctl_set) {
        return g_zdbg_host_ops.sysctl_set(name, val_str);
    }
    return 0;
#else
    int mib[12];
    size_t mib_len = 10u;
    bool is_string_oid = false;
    if (sysctlnametomib(name, mib + 2, &mib_len) == 0 && mib_len > 0u && mib_len <= 10u) {
        mib[0] = 0; /* CTL_SYSCTL */
        mib[1] = 4; /* OIDFMT */
        uint8_t fmt_buf[64];
        size_t fmt_sz = sizeof(fmt_buf);
        if (sysctl(mib, (u_int)(mib_len + 2u), fmt_buf, &fmt_sz, NULL, 0) == 0 && fmt_sz >= sizeof(uint32_t)) {
            uint32_t kind = 0u;
            memcpy(&kind, fmt_buf, sizeof(kind));
            if ((kind & 0x0fu) == 3u) { /* CTLTYPE_STRING */
                is_string_oid = true;
            }
        }
    }
    uint8_t cur_probe[64];
    size_t cur_sz = sizeof(cur_probe);
    const bool have_sz = (!is_string_oid && sysctlbyname(name, cur_probe, &cur_sz, NULL, 0) == 0);
    int64_t parsed = 0;
    if (have_sz && zizhan_dbg_parse_i64(val_str, &parsed)) {
        if (cur_sz == 8u) {
            int64_t v64 = parsed;
            return sysctlbyname(name, NULL, NULL, &v64, sizeof(v64)) == 0 ? 0 : (errno ? errno : EIO);
        }
        if (cur_sz == 4u) {
            int32_t v32 = (int32_t)parsed;
            return sysctlbyname(name, NULL, NULL, &v32, sizeof(v32)) == 0 ? 0 : (errno ? errno : EIO);
        }
        if (cur_sz == 2u) {
            int16_t v16 = (int16_t)parsed;
            return sysctlbyname(name, NULL, NULL, &v16, sizeof(v16)) == 0 ? 0 : (errno ? errno : EIO);
        }
        if (cur_sz == 1u) {
            int8_t v8 = (int8_t)parsed;
            return sysctlbyname(name, NULL, NULL, &v8, sizeof(v8)) == 0 ? 0 : (errno ? errno : EIO);
        }
    }
    if (sysctlbyname(name, NULL, NULL, (void *)(uintptr_t)val_str, strlen(val_str)) != 0) {
        return errno ? errno : EIO;
    }
    return 0;
#endif
}

/* --- Image Encoding (Runtime dlopen ImageIO JPEG/PNG + Pure-C PNG Fallback) --- */

#ifndef ZIZHAN_DEBUGD_HOST_TEST
typedef struct CGImageDestination *ZizhanCGImageDestRef;
typedef ZizhanCGImageDestRef (*ZdbgDestCreateFn)(CFMutableDataRef, CFStringRef, size_t, CFDictionaryRef);
typedef void (*ZdbgDestAddImageFn)(ZizhanCGImageDestRef, CGImageRef, CFDictionaryRef);
typedef bool (*ZdbgDestFinalizeFn)(ZizhanCGImageDestRef);

static ZdbgDestCreateFn   g_zdbg_img_create;
static ZdbgDestAddImageFn g_zdbg_img_add;
static ZdbgDestFinalizeFn g_zdbg_img_fin;
static dispatch_once_t    g_zdbg_img_once;

static void zdbg_init_imageio(void *unused) {
    (void)unused;
    void *h = dlopen("/System/Library/Frameworks/ImageIO.framework/ImageIO", RTLD_LAZY | RTLD_LOCAL);
    if (!h) {
        return;
    }
    g_zdbg_img_create = (ZdbgDestCreateFn)dlsym(h, "CGImageDestinationCreateWithData");
    g_zdbg_img_add = (ZdbgDestAddImageFn)dlsym(h, "CGImageDestinationAddImage");
    g_zdbg_img_fin = (ZdbgDestFinalizeFn)dlsym(h, "CGImageDestinationFinalize");
}
#endif

static uint8_t *zdbg_encode_image(const uint8_t *bgra, uint32_t w, uint32_t h, uint32_t stride,
                                  bool want_jpeg, int quality_pct,
                                  size_t *out_len, const char **mime_out) {
    *out_len = 0u;
    *mime_out = "image/png";
#ifndef ZIZHAN_DEBUGD_HOST_TEST
    dispatch_once_f(&g_zdbg_img_once, NULL, zdbg_init_imageio);
    if (g_zdbg_img_create && g_zdbg_img_add && g_zdbg_img_fin) {
        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        CFDataRef raw_cf = CFDataCreateWithBytesNoCopy(
            NULL, bgra, (CFIndex)((size_t)stride * (size_t)h), kCFAllocatorNull);
        CGDataProviderRef prov = raw_cf ? CGDataProviderCreateWithCFData(raw_cf) : NULL;
        const CGBitmapInfo bmi = (CGBitmapInfo)kCGBitmapByteOrder32Little |
                                 (CGBitmapInfo)kCGImageAlphaNoneSkipFirst;
        CGImageRef img = (cs && prov) ? CGImageCreate(
            w, h, 8u, 32u, stride, cs, bmi, prov, NULL, false, kCGRenderingIntentDefault) : NULL;
        CFMutableDataRef dst_data = img ? CFDataCreateMutable(NULL, 0) : NULL;
        CFStringRef uti = want_jpeg ? CFSTR("public.jpeg") : CFSTR("public.png");
        ZizhanCGImageDestRef dest = dst_data ? g_zdbg_img_create(dst_data, uti, 1u, NULL) : NULL;
        bool encoded = false;
        if (dest && img) {
            CFMutableDictionaryRef props = NULL;
            if (want_jpeg) {
                double q = (double)(quality_pct < 10 ? 10 : (quality_pct > 100 ? 100 : quality_pct)) / 100.0;
                CFNumberRef qnum = CFNumberCreate(NULL, kCFNumberDoubleType, &q);
                if (qnum) {
                    props = CFDictionaryCreateMutable(
                        NULL, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
                    if (props) {
                        CFDictionarySetValue(props, CFSTR("kCGImageDestinationLossyCompressionQuality"), qnum);
                    }
                    CFRelease(qnum);
                }
            }
            g_zdbg_img_add(dest, img, props);
            encoded = g_zdbg_img_fin(dest);
            if (props) { CFRelease(props); }
        }
        if (dest) { CFRelease((CFTypeRef)dest); }
        if (img) { CGImageRelease(img); }
        if (prov) { CGDataProviderRelease(prov); }
        if (raw_cf) { CFRelease(raw_cf); }
        if (cs) { CGColorSpaceRelease(cs); }
        if (encoded && dst_data && CFDataGetLength(dst_data) > 0) {
            size_t len = (size_t)CFDataGetLength(dst_data);
            uint8_t *copy = (uint8_t *)malloc(len);
            if (copy) {
                memcpy(copy, CFDataGetBytePtr(dst_data), len);
                CFRelease(dst_data);
                *out_len = len;
                *mime_out = want_jpeg ? "image/jpeg" : "image/png";
                return copy;
            }
        }
        if (dst_data) { CFRelease(dst_data); }
    }
#else
    (void)want_jpeg;
    (void)quality_pct;
#endif
    const size_t cap = zizhan_dbg_png_stored_bound(w, h);
    uint8_t *png = (uint8_t *)malloc(cap);
    if (!png) {
        return NULL;
    }
    const size_t len = zizhan_dbg_encode_png_stored(bgra, w, h, stride, png, cap);
    if (len == 0u) {
        free(png);
        return NULL;
    }
    *out_len = len;
    *mime_out = "image/png";
    return png;
}

/* --- Hot-UI 12s Hung-SpringBoard Watchdog & Safe Staging --- */

typedef struct {
    uint64_t gen;
} ZdbgWatchdogArg;

static void *zdbg_hot_ui_watchdog_thread(void *arg) {
    ZdbgWatchdogArg *wa = (ZdbgWatchdogArg *)arg;
    const uint64_t my_gen = wa->gen;
    free(wa);
#ifdef ZIZHAN_DEBUGD_HOST_TEST
    usleep(120 * 1000); /* 120 ms in fast host unit tests */
#else
    sleep(12);
#endif
    if (__atomic_load_n(&g_zdbg_trial_gen, __ATOMIC_ACQUIRE) != my_gen) {
        return NULL;
    }
    ZizhanDbgCtlReq qreq;
    ZizhanDbgStatusResp qresp;
    memset(&qreq, 0, sizeof(qreq));
    qreq.op = ZIZHAN_DBG_OP_HOT_UI;
    qreq.target = ZIZHAN_HOT_UI_QUERY;
    if (zdbg_backend_ctl(&qreq, &qresp) == 0 && qresp.hot_ui_state == ZIZHAN_HOT_UI_STATE_TRIAL) {
        /* SpringBoard failed to reach 5s main-queue confirmation -> revert & force-restart! */
        qreq.target = ZIZHAN_HOT_UI_REVERT;
        (void)zdbg_backend_ctl(&qreq, &qresp);
        memset(&qreq, 0, sizeof(qreq));
        qreq.op = ZIZHAN_DBG_OP_SIGNAL_SB;
        qreq.target = 9u; /* SIGKILL wedged trial SpringBoard */
        (void)zdbg_backend_ctl(&qreq, &qresp);
        fprintf(stderr, "ZIZHAN_DEBUGD: SpringBoard trial timed out -> reverted to stock UI dylib\n");
    }
    return NULL;
}

static int zdbg_stage_and_arm_hot_ui(const uint8_t *dylib_bytes, size_t dylib_len,
                                     char *err_msg, size_t err_cap, char *sha_hex_out) {
    ZizhanMachoReport rep;
    int mrc = zizhan_macho_inspect(dylib_bytes, dylib_len, &rep);
    if (mrc != ZIZHAN_IPA_OK) {
        snprintf(err_msg, err_cap, "invalid Mach-O (%d)", mrc);
        return EINVAL;
    }
    if (rep.slice_offset != 0u || rep.filetype != 6u /* MH_DYLIB */) {
        snprintf(err_msg, err_cap, "expected thin arm64 MH_DYLIB (got filetype=%u slice_off=%u)",
                 rep.filetype, rep.slice_offset);
        return EINVAL;
    }
    if (!rep.has_code_sig || rep.cd_page_bytes != 4096u) {
        snprintf(err_msg, err_cap, "dylib must carry a 4096-byte page signature (got page_bytes=%u)",
                 rep.cd_page_bytes);
        return EINVAL;
    }

    uint8_t sha[32];
    zizhan_ipa_sha256(dylib_bytes, dylib_len, sha);
    for (unsigned i = 0; i < 32u; ++i) {
        snprintf(sha_hex_out + i * 2u, 3u, "%02x", sha[i]);
    }

    pthread_mutex_lock(&g_zdbg_reload_mtx);
#ifdef ZIZHAN_DEBUGD_HOST_TEST
    if (g_zdbg_hot_ui_dir_override[0] != '\0') {
        if (mkdir(g_zdbg_hot_ui_dir_override, 0755) != 0 && errno != EEXIST) {
            pthread_mutex_unlock(&g_zdbg_reload_mtx);
            snprintf(err_msg, err_cap, "mkdir hot-ui failed (errno=%d)", errno);
            return EIO;
        }
        int dfd = open(g_zdbg_hot_ui_dir_override, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        if (dfd < 0) {
            int e = errno ? errno : EPERM;
            pthread_mutex_unlock(&g_zdbg_reload_mtx);
            snprintf(err_msg, err_cap, "open hot-ui dir failed (errno=%d)", e);
            return e;
        }
        const char *tmp_name = "libpong-ui18.dylib.tmp";
        (void)unlinkat(dfd, tmp_name, 0);
        int fd = openat(dfd, tmp_name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0755);
        if (fd < 0) {
            int e = errno ? errno : EIO;
            close(dfd);
            pthread_mutex_unlock(&g_zdbg_reload_mtx);
            snprintf(err_msg, err_cap, "openat staging file failed (errno=%d)", e);
            return e;
        }
        size_t off = 0u;
        while (off < dylib_len) {
            ssize_t wr = write(fd, dylib_bytes + off, dylib_len - off);
            if (wr <= 0) {
                if (wr < 0 && errno == EINTR) { continue; }
                close(fd);
                (void)unlinkat(dfd, tmp_name, 0);
                close(dfd);
                pthread_mutex_unlock(&g_zdbg_reload_mtx);
                snprintf(err_msg, err_cap, "write staging file failed");
                return EIO;
            }
            off += (size_t)wr;
        }
        (void)fsync(fd);
        (void)fchmod(fd, 0755);
        close(fd);
        if (renameat(dfd, tmp_name, dfd, "libpong-ui18.dylib") != 0) {
            int e = errno ? errno : EIO;
            (void)unlinkat(dfd, tmp_name, 0);
            close(dfd);
            pthread_mutex_unlock(&g_zdbg_reload_mtx);
            snprintf(err_msg, err_cap, "renameat failed (errno=%d)", e);
            return e;
        }
        close(dfd);
    }
    if (g_zdbg_host_ops.stage_ui) {
        int se = g_zdbg_host_ops.stage_ui(dylib_bytes, dylib_len);
        if (se != 0) {
            pthread_mutex_unlock(&g_zdbg_reload_mtx);
            snprintf(err_msg, err_cap, "host stage_ui failed (errno=%d)", se);
            return se;
        }
    }
#else
    /* Stage via kernel root-owned ping-pong slots in /private/var/db/zizhan-hot-ui-{0,1}.dylib */
    if (sysctlbyname("kern.zizhan_dbg_stage_ui", NULL, NULL,
                     (void *)(uintptr_t)dylib_bytes, dylib_len) != 0) {
        int e = errno ? errno : EIO;
        pthread_mutex_unlock(&g_zdbg_reload_mtx);
        snprintf(err_msg, err_cap, "kernel zizhan_dbg_stage_ui failed (errno=%d)", e);
        return e;
    }
#endif

    ZizhanDbgCtlReq creq;
    ZizhanDbgStatusResp cresp;
    memset(&creq, 0, sizeof(creq));
    creq.op = ZIZHAN_DBG_OP_HOT_UI;
    creq.target = ZIZHAN_HOT_UI_ARM;
    int rc = zdbg_backend_ctl(&creq, &cresp);
    if (rc != 0) {
        pthread_mutex_unlock(&g_zdbg_reload_mtx);
        snprintf(err_msg, err_cap, "kernel HOT_UI_ARM failed (%d)", rc);
        return rc;
    }

    const uint64_t gen = __atomic_add_fetch(&g_zdbg_trial_gen, 1ull, __ATOMIC_ACQ_REL);
    ZdbgWatchdogArg *wa = (ZdbgWatchdogArg *)malloc(sizeof(*wa));
    if (wa) {
        wa->gen = gen;
        pthread_t wth;
        if (pthread_create(&wth, NULL, zdbg_hot_ui_watchdog_thread, wa) == 0) {
            pthread_detach(wth);
        } else {
            free(wa);
        }
    }

    /* Ask SpringBoard to restart cleanly so launchd spawns it once with the candidate dylib. */
    memset(&creq, 0, sizeof(creq));
    creq.op = ZIZHAN_DBG_OP_SIGNAL_SB;
    creq.target = 15u; /* SIGTERM */
    (void)zdbg_backend_ctl(&creq, &cresp);
    pthread_mutex_unlock(&g_zdbg_reload_mtx);
    return 0;
}

/* --- Single-Page Zero-Dependency Web Remote Console (`GET /`) --- */
static const char kZdbgWebHtml[] =
"<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>Zizhan Live Debug Console</title>"
"<style>"
":root{color-scheme:dark;font-family:-apple-system,BlinkMacSystemFont,monospace;background:#0d1117;color:#e6edf3}"
"body{margin:0;padding:12px;display:grid;grid-template-columns:minmax(320px,520px) 1fr;gap:14px;min-height:96vh}"
"@media(max-width:900px){body{grid-template-columns:1fr}}"
".card{background:#161b22;border:1px solid #30363d;border-radius:10px;padding:12px;display:flex;flex-direction:column;gap:8px}"
".row{display:flex;flex-wrap:wrap;gap:6px;align-items:center}"
"button,select,input{background:#21262d;color:#e6edf3;border:1px solid #30363d;border-radius:6px;padding:6px 10px;font-size:13px;cursor:pointer}"
"button:hover{background:#30363d}input[type=text]{flex:1;min-width:140px;cursor:text}"
"#screenWrap{position:relative;display:flex;justify-content:center;align-items:center;background:#010409;border-radius:8px;overflow:hidden;min-height:360px}"
"#screenImg{max-width:100%;max-height:74vh;user-select:none;-webkit-user-drag:none;cursor:crosshair}"
"#klog{flex:1;min-height:380px;max-height:76vh;overflow:auto;background:#010409;padding:8px;border-radius:6px;font-size:12px;line-height:1.4;white-space:pre-wrap;word-break:break-all;margin:0}"
".pill{font-size:12px;padding:3px 8px;border-radius:999px;background:#1f6feb33;border:1px solid #1f6feb88}"
"</style></head><body>"
"<div class=\"card\">"
"<div class=\"row\"><strong>MIX Fold 2 (zizhan)</strong><span id=\"stPill\" class=\"pill\">connecting...</span></div>"
"<div class=\"row\">"
"<select id=\"panelSel\"><option value=\"auto\">Panel: Auto</option><option value=\"inner\">Inner (2160x1916)</option><option value=\"outer\">Outer (1080x2520)</option></select>"
"<select id=\"scaleSel\"><option value=\"2\" selected>Scale 1/2</option><option value=\"4\">Scale 1/4 (Fast)</option><option value=\"1\">Scale 1/1 (Full)</option></select>"
"<label><input type=\"checkbox\" id=\"liveChk\" checked> Live</label>"
"<button onclick=\"refreshShot()\">Shot</button>"
"</div>"
"<div id=\"screenWrap\"><img id=\"screenImg\" alt=\"screen\"></div>"
"<div class=\"row\">"
"<button onclick=\"sendKey('power')\">Power</button>"
"<button onclick=\"sendKey('home')\">Home</button>"
"<button onclick=\"sendKey('vol_up')\">Vol+</button>"
"<button onclick=\"sendKey('vol_down')\">Vol-</button>"
"<button onclick=\"sendFold('closed')\">Fold</button>"
"<button onclick=\"sendFold('open')\">Unfold</button>"
"<button onclick=\"sendFold('auto')\">Hall Auto</button>"
"</div>"
"<div class=\"row\">"
"<input type=\"text\" id=\"openTxt\" placeholder=\"bundle:com.zizhan.installer or https://...\">"
"<button onclick=\"sendOpen()\">Open</button>"
"</div>"
"</div>"
"<div class=\"card\">"
"<div class=\"row\"><strong>Live Ramoops Console</strong>"
"<button onclick=\"keepalive()\">+5m Watchdog</button>"
"<button onclick=\"sampleSb()\">Sample SB 3s</button>"
"<button onclick=\"document.getElementById('klog').textContent=''\">Clear</button>"
"</div>"
"<pre id=\"klog\"></pre>"
"</div>"
"<script>"
"const u=new URL(location.href),hp=new URLSearchParams(u.hash.startsWith('#')?u.hash.slice(1):'');"
"let tok=hp.get('token')||u.searchParams.get('token')||sessionStorage.getItem('zdbg_tok')||'';"
"if(hp.has('token')||u.searchParams.has('token')){sessionStorage.setItem('zdbg_tok',tok);u.hash='';u.searchParams.delete('token');history.replaceState(null,'',u);}"
"function hdrs(extra){const h=Object.assign({},extra||{});if(tok)h['X-Zizhan-Token']=tok;return h;}"
"let kseq=0,srcW=2160,srcH=1916,ptDown=null,busyShot=false;"
"const img=document.getElementById('screenImg'),kpre=document.getElementById('klog'),pill=document.getElementById('stPill');"
"async function refreshShot(){if(busyShot)return;busyShot=true;try{"
"const p=document.getElementById('panelSel').value,s=document.getElementById('scaleSel').value;"
"const r=await fetch(`/api/screenshot?panel=${p}&scale=${s}&format=jpeg&t=${Date.now()}`,{headers:hdrs()});"
"if(r.ok){srcW=parseInt(r.headers.get('X-Zizhan-Src-Width')||'2160');srcH=parseInt(r.headers.get('X-Zizhan-Src-Height')||'1916');"
"const b=await r.blob(),url=URL.createObjectURL(b);const old=img.dataset.u;img.onload=()=>{if(old)URL.revokeObjectURL(old);};img.dataset.u=url;img.src=url;}"
"}catch(e){}busyShot=false;}"
"function mapPt(ev){const r=img.getBoundingClientRect();if(!r.width||!r.height)return[0,0];"
"const x=Math.round(((ev.clientX-r.left)/r.width)*(srcW-1)),y=Math.round(((ev.clientY-r.top)/r.height)*(srcH-1));"
"return[Math.max(0,Math.min(srcW-1,x)),Math.max(0,Math.min(srcH-1,y))];}"
"img.addEventListener('pointerdown',e=>{e.preventDefault();ptDown={p:mapPt(e),t:Date.now()};});"
"img.addEventListener('pointerup',async e=>{if(!ptDown)return;const p1=mapPt(e),p0=ptDown.p,dt=Math.min(1200,Math.max(50,Date.now()-ptDown.t));ptDown=null;"
"const panel=document.getElementById('panelSel').value;const dx=Math.abs(p1[0]-p0[0]),dy=Math.abs(p1[1]-p0[1]);"
"const q=(dx>18||dy>18)?`op=swipe&panel=${panel}&x0=${p0[0]}&y0=${p0[1]}&x1=${p1[0]}&y1=${p1[1]}&ms=${dt}`:`op=tap&panel=${panel}&x=${p0[0]}&y=${p0[1]}`;"
"await fetch('/api/input?'+q,{method:'POST',headers:hdrs()});setTimeout(refreshShot,120);});"
"async function sendKey(k){await fetch('/api/input?op=key&name='+k,{method:'POST',headers:hdrs()});setTimeout(refreshShot,180);}"
"async function sendFold(m){await fetch('/api/input?op=fold&state='+m,{method:'POST',headers:hdrs()});setTimeout(refreshShot,250);}"
"async function sendOpen(){const v=document.getElementById('openTxt').value.trim();if(v)await fetch('/api/open?target='+encodeURIComponent(v),{method:'POST',headers:hdrs()});}"
"async function keepalive(){await fetch('/api/keepalive?seconds=360',{method:'POST',headers:hdrs()});pollStatus();}"
"async function sampleSb(){await fetch('/api/sample?proc=SpringBoard&seconds=3',{method:'POST',headers:hdrs()});}"
"async function pollStatus(){try{const r=await fetch('/api/status',{headers:hdrs()});if(r.ok){const s=await r.json();"
"pill.textContent=`batt ${s.batt_soc_pct}% (${s.batt_ma}mA) | wdt ${s.autoreset_rem_s}s | lid ${s.lid_closed===1?'closed':'open'} | view ${s.outer_active_view} | hotUI ${s.hot_ui_state}`;}}catch(e){}}"
"async function pollLog(){try{const r=await fetch(`/api/klog?since=${kseq}&max=32768`,{headers:hdrs()});if(r.ok){"
"const ns=r.headers.get('X-Zizhan-Next-Seq');if(ns)kseq=ns;const t=await r.text();if(t){kpre.textContent+=t;if(kpre.textContent.length>200000)kpre.textContent=kpre.textContent.slice(-150000);kpre.scrollTop=kpre.scrollHeight;}}}catch(e){}}"
"setInterval(()=>{if(document.getElementById('liveChk').checked)refreshShot();},650);"
"setInterval(pollLog,500);setInterval(pollStatus,2000);setInterval(keepalive,60000);"
"pollStatus();pollLog();refreshShot();"
"</script></body></html>";

/* --- HTTP Route Dispatcher --- */

static void zdbg_format_status_json(const ZizhanDbgStatusResp *st, char *out, size_t cap) {
    snprintf(out, cap,
             "{\"ok\":true,\"uptime_ms\":%llu,\"klog_total_bytes\":%llu,\"pmsg_bytes\":%u,"
             "\"autoreset_rem_s\":%d,\"autoreset_pings\":%u,\"lid_closed\":%d,\"hall_override\":%d,"
             "\"outer_active_view\":%u,\"inner_display_pwr\":%d,\"zc_flipped_tag\":%u,"
             "\"batt_soc_pct\":%d,\"batt_mv\":%d,\"batt_ma\":%d,\"batt_charging\":%u,"
             "\"cpu_thermal_lvl\":%u,\"gpu_thermal_lvl\":%u,"
             "\"hot_ui_state\":%u,\"hot_ui_rollbacks\":%u,\"hot_ui_sb_execs\":%u,"
             "\"op_result\":%d,\"inner_w\":%u,\"inner_h\":%u,\"outer_w\":%u,\"outer_h\":%u,"
             "\"user_present\":%s}\n",
             (unsigned long long)st->uptime_ms,
             (unsigned long long)st->klog_total_bytes,
             st->pmsg_bytes,
             st->autoreset_rem_s,
             st->autoreset_pings,
             st->lid_closed,
             st->hall_override,
             st->outer_active_view,
             st->inner_display_pwr,
             st->zc_flipped_tag,
             st->batt_soc_pct,
             st->batt_mv,
             st->batt_ma,
             st->batt_charging,
             st->cpu_thermal_lvl,
             st->gpu_thermal_lvl,
             st->hot_ui_state,
             st->hot_ui_rollbacks,
             st->hot_ui_sb_execs,
             st->op_result,
             st->inner_w,
             st->inner_h,
             st->outer_w,
             st->outer_h,
             g_zdbg_user_present ? "true" : "false");
}

static uint32_t zdbg_parse_panel(const char *s) {
    if (!s || s[0] == '\0' || strcmp(s, "auto") == 0) {
        return ZIZHAN_DBG_PANEL_AUTO;
    }
    if (strcmp(s, "inner") == 0 || strcmp(s, "1") == 0) {
        return ZIZHAN_DBG_PANEL_INNER;
    }
    if (strcmp(s, "outer") == 0 || strcmp(s, "2") == 0) {
        return ZIZHAN_DBG_PANEL_OUTER;
    }
    if (strcmp(s, "norm") == 0 || strcmp(s, "3") == 0) {
        return ZIZHAN_DBG_PANEL_NORM;
    }
    return 99u;
}

static void zdbg_handle_http(int fd, const char *method, const char *path,
                             const char *query, const char *hdrs,
                             const uint8_t *body, size_t body_len) {
    if (!zdbg_check_request_auth(hdrs, query)) {
        zdbg_send_json(fd, 401, "Unauthorized", "{\"ok\":false,\"error\":\"unauthorized\"}\n");
        return;
    }

    const bool is_get = (strcmp(method, "GET") == 0);
    const bool is_post = (strcmp(method, "POST") == 0);

    if (is_get && (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0)) {
        zdbg_send_response(fd, 200, "OK", "text/html; charset=utf-8",
                           kZdbgWebHtml, sizeof(kZdbgWebHtml) - 1u, NULL);
        return;
    }

    if (is_get && strcmp(path, "/api/status") == 0) {
        ZizhanDbgCtlReq creq;
        ZizhanDbgStatusResp st;
        memset(&creq, 0, sizeof(creq));
        creq.op = ZIZHAN_DBG_OP_STATUS;
        int rc = zdbg_backend_ctl(&creq, &st);
        if (rc != 0) {
            char j[128];
            snprintf(j, sizeof(j), "{\"ok\":false,\"error\":\"ctl_failed\",\"errno\":%d}\n", rc);
            zdbg_send_json(fd, 500, "Internal Server Error", j);
            return;
        }
        char json[1024];
        zdbg_format_status_json(&st, json, sizeof(json));
        zdbg_send_json(fd, 200, "OK", json);
        return;
    }

    if (is_get && strcmp(path, "/api/klog") == 0) {
        ZizhanDbgKlogReq kreq;
        memset(&kreq, 0, sizeof(kreq));
        int64_t since = zdbg_query_i64(query, "since", 0);
        int64_t zone = zdbg_query_i64(query, "zone", 0);
        int64_t maxb = zdbg_query_i64(query, "max", 65536);
        if (since < 0) { since = 0; }
        if (zone < 0 || zone > 1) {
            zdbg_send_json(fd, 400, "Bad Request", "{\"ok\":false,\"error\":\"invalid_zone\"}\n");
            return;
        }
        if (maxb <= 0 || maxb > 65536) { maxb = 65536; }
        kreq.since_seq = (uint64_t)since;
        kreq.zone = (uint32_t)zone;
        kreq.max_bytes = (uint32_t)maxb;

        uint8_t *buf = (uint8_t *)malloc((size_t)maxb);
        if (!buf) {
            zdbg_send_json(fd, 500, "Internal Server Error", "{\"ok\":false,\"error\":\"oom\"}\n");
            return;
        }
        ZizhanDbgKlogHdr khdr;
        int rc = zdbg_backend_klog(&kreq, &khdr, buf, (size_t)maxb);
        if (rc != 0) {
            free(buf);
            char j[128];
            snprintf(j, sizeof(j), "{\"ok\":false,\"error\":\"klog_failed\",\"errno\":%d}\n", rc);
            zdbg_send_json(fd, 500, "Internal Server Error", j);
            return;
        }
        const uint64_t next_seq = khdr.start_seq + (uint64_t)khdr.byte_len;
        char extra[256];
        snprintf(extra, sizeof(extra),
                 "X-Zizhan-Total-Seq: %llu\r\n"
                 "X-Zizhan-Start-Seq: %llu\r\n"
                 "X-Zizhan-Next-Seq: %llu\r\n"
                 "X-Zizhan-Dropped: %u\r\n",
                 (unsigned long long)khdr.total_seq,
                 (unsigned long long)khdr.start_seq,
                 (unsigned long long)next_seq,
                 khdr.dropped);
        zdbg_send_response(fd, 200, "OK", "text/plain; charset=utf-8", buf, khdr.byte_len, extra);
        free(buf);
        return;
    }

    if (is_get && strcmp(path, "/api/screenshot") == 0) {
        char panel_s[24] = "auto", fmt_s[16] = "jpeg";
        (void)zdbg_query_param(query, "panel", panel_s, sizeof(panel_s));
        (void)zdbg_query_param(query, "format", fmt_s, sizeof(fmt_s));
        int64_t scale = zdbg_query_i64(query, "scale", 2);
        int64_t qual = zdbg_query_i64(query, "quality", 80);
        uint32_t panel_id = zdbg_parse_panel(panel_s);
        if (panel_id > ZIZHAN_DBG_PANEL_OUTER || (scale != 1 && scale != 2 && scale != 4)) {
            zdbg_send_json(fd, 400, "Bad Request", "{\"ok\":false,\"error\":\"invalid_panel_or_scale\"}\n");
            return;
        }
        ZizhanDbgFbReq fbreq;
        memset(&fbreq, 0, sizeof(fbreq));
        fbreq.panel = panel_id;
        fbreq.scale = (uint32_t)scale;

        ZizhanDbgFbHdr fbhdr;
        uint8_t *px = NULL;
        size_t px_len = 0u;
        int rc = zdbg_backend_fb(&fbreq, &fbhdr, &px, &px_len);
        if (rc != 0 || !px) {
            char j[128];
            snprintf(j, sizeof(j), "{\"ok\":false,\"error\":\"fb_unavailable\",\"errno\":%d}\n", rc);
            zdbg_send_json(fd, 503, "Service Unavailable", j);
            return;
        }
        const bool want_jpeg = (strcmp(fmt_s, "png") != 0 && strcmp(fmt_s, "raw") != 0);
        if (strcmp(fmt_s, "raw") == 0) {
            char extra[256];
            snprintf(extra, sizeof(extra),
                     "X-Zizhan-Panel: %u\r\nX-Zizhan-Width: %u\r\nX-Zizhan-Height: %u\r\n"
                     "X-Zizhan-Src-Width: %u\r\nX-Zizhan-Src-Height: %u\r\nX-Zizhan-Seq: %u\r\n",
                     fbhdr.panel, fbhdr.width, fbhdr.height,
                     fbhdr.src_width, fbhdr.src_height, fbhdr.seq);
            zdbg_send_response(fd, 200, "OK", "application/octet-stream", px, px_len, extra);
            free(px);
            return;
        }
        size_t img_len = 0u;
        const char *mime = "image/png";
        uint8_t *img = zdbg_encode_image(px, fbhdr.width, fbhdr.height, fbhdr.stride,
                                         want_jpeg, (int)qual, &img_len, &mime);
        free(px);
        if (!img || img_len == 0u) {
            free(img);
            zdbg_send_json(fd, 500, "Internal Server Error", "{\"ok\":false,\"error\":\"encode_failed\"}\n");
            return;
        }
        char extra[256];
        snprintf(extra, sizeof(extra),
                 "X-Zizhan-Panel: %u\r\nX-Zizhan-Width: %u\r\nX-Zizhan-Height: %u\r\n"
                 "X-Zizhan-Src-Width: %u\r\nX-Zizhan-Src-Height: %u\r\nX-Zizhan-Seq: %u\r\n",
                 fbhdr.panel, fbhdr.width, fbhdr.height,
                 fbhdr.src_width, fbhdr.src_height, fbhdr.seq);
        zdbg_send_response(fd, 200, "OK", mime, img, img_len, extra);
        free(img);
        return;
    }

    if (is_post && strcmp(path, "/api/keepalive") == 0) {
        ZizhanDbgCtlReq creq;
        ZizhanDbgStatusResp st;
        memset(&creq, 0, sizeof(creq));
        creq.op = ZIZHAN_DBG_OP_EXTEND_WDT;
        int64_t secs = zdbg_query_i64(query, "seconds", 360);
        if (secs < 30) { secs = 30; }
        if (secs > 3600) { secs = 3600; }
        creq.duration_ms = (uint32_t)secs;
        creq.y0 = (int32_t)zdbg_query_i64(query, "exact", 0);
        int rc = zdbg_backend_ctl(&creq, &st);
        if (rc != 0) {
            zdbg_send_json(fd, 500, "Internal Server Error", "{\"ok\":false,\"error\":\"keepalive_failed\"}\n");
            return;
        }
        char json[1024];
        zdbg_format_status_json(&st, json, sizeof(json));
        zdbg_send_json(fd, 200, "OK", json);
        return;
    }

    if (is_post && strcmp(path, "/api/input") == 0) {
        char op_s[24] = "";
        (void)zdbg_query_param(query, "op", op_s, sizeof(op_s));
        ZizhanDbgCtlReq creq;
        ZizhanDbgStatusResp st;
        memset(&creq, 0, sizeof(creq));

        if (strcmp(op_s, "tap") == 0 || strcmp(op_s, "swipe") == 0) {
            char panel_s[24] = "auto";
            (void)zdbg_query_param(query, "panel", panel_s, sizeof(panel_s));
            uint32_t pid = zdbg_parse_panel(panel_s);
            if (pid > ZIZHAN_DBG_PANEL_NORM) {
                zdbg_send_json(fd, 400, "Bad Request", "{\"ok\":false,\"error\":\"invalid_panel\"}\n");
                return;
            }
            creq.op = ZIZHAN_DBG_OP_TOUCH;
            creq.target = pid;
            if (strcmp(op_s, "tap") == 0) {
                int32_t x = (int32_t)zdbg_query_i64(query, "x", 0);
                int32_t y = (int32_t)zdbg_query_i64(query, "y", 0);
                creq.x0 = x;
                creq.y0 = y;
                creq.x1 = x;
                creq.y1 = y;
                creq.duration_ms = (uint32_t)zdbg_query_i64(query, "ms", 55);
            } else {
                creq.x0 = (int32_t)zdbg_query_i64(query, "x0", 0);
                creq.y0 = (int32_t)zdbg_query_i64(query, "y0", 0);
                creq.x1 = (int32_t)zdbg_query_i64(query, "x1", creq.x0);
                creq.y1 = (int32_t)zdbg_query_i64(query, "y1", creq.y0);
                creq.duration_ms = (uint32_t)zdbg_query_i64(query, "ms", 200);
                creq.steps = (uint32_t)zdbg_query_i64(query, "steps", 12);
            }
        } else if (strcmp(op_s, "key") == 0) {
            char key_s[24] = "";
            (void)zdbg_query_param(query, "name", key_s, sizeof(key_s));
            creq.op = ZIZHAN_DBG_OP_KEY;
            creq.duration_ms = (uint32_t)zdbg_query_i64(query, "ms", 80);
            if (strcmp(key_s, "power") == 0) {
                creq.target = ZIZHAN_DBG_KEY_POWER;
            } else if (strcmp(key_s, "vol_up") == 0 || strcmp(key_s, "volup") == 0) {
                creq.target = ZIZHAN_DBG_KEY_VOL_UP;
            } else if (strcmp(key_s, "vol_down") == 0 || strcmp(key_s, "voldown") == 0) {
                creq.target = ZIZHAN_DBG_KEY_VOL_DOWN;
            } else if (strcmp(key_s, "home") == 0) {
                creq.target = ZIZHAN_DBG_KEY_HOME;
            } else {
                zdbg_send_json(fd, 400, "Bad Request", "{\"ok\":false,\"error\":\"invalid_key\"}\n");
                return;
            }
        } else if (strcmp(op_s, "fold") == 0) {
            char st_s[24] = "";
            (void)zdbg_query_param(query, "state", st_s, sizeof(st_s));
            creq.op = ZIZHAN_DBG_OP_HALL_OVERRIDE;
            if (strcmp(st_s, "auto") == 0 || strcmp(st_s, "-1") == 0) {
                creq.x0 = -1;
            } else if (strcmp(st_s, "open") == 0 || strcmp(st_s, "0") == 0) {
                creq.x0 = 0;
            } else if (strcmp(st_s, "closed") == 0 || strcmp(st_s, "1") == 0) {
                creq.x0 = 1;
            } else {
                zdbg_send_json(fd, 400, "Bad Request", "{\"ok\":false,\"error\":\"invalid_fold_state\"}\n");
                return;
            }
        } else {
            zdbg_send_json(fd, 400, "Bad Request", "{\"ok\":false,\"error\":\"unknown_input_op\"}\n");
            return;
        }

        int rc = zdbg_backend_ctl(&creq, &st);
        if (rc != 0) {
            char j[128];
            snprintf(j, sizeof(j), "{\"ok\":false,\"error\":\"input_failed\",\"errno\":%d}\n", rc);
            zdbg_send_json(fd, (rc == EAGAIN || rc == EBUSY) ? 409 : 500, "Error", j);
            return;
        }
        char json[1024];
        zdbg_format_status_json(&st, json, sizeof(json));
        zdbg_send_json(fd, 200, "OK", json);
        return;
    }

    if (is_post && strcmp(path, "/api/open") == 0) {
        char target[224] = "";
        if (!zdbg_query_param(query, "target", target, sizeof(target)) || target[0] == '\0') {
            zdbg_send_json(fd, 400, "Bad Request", "{\"ok\":false,\"error\":\"missing_target\"}\n");
            return;
        }
        int rc = zdbg_backend_open_cmd(target);
        if (rc != 0) {
            char j[128];
            snprintf(j, sizeof(j), "{\"ok\":false,\"error\":\"open_failed\",\"errno\":%d}\n", rc);
            zdbg_send_json(fd, 400, "Bad Request", j);
            return;
        }
        char esc[320], json[420];
        zdbg_json_escape(target, esc, sizeof(esc));
        snprintf(json, sizeof(json), "{\"ok\":true,\"dispatched\":\"%s\"}\n", esc);
        zdbg_send_json(fd, 200, "OK", json);
        return;
    }

    if ((is_get || is_post) && strcmp(path, "/api/sysctl") == 0) {
        char name[104] = "";
        if (!zdbg_query_param(query, "name", name, sizeof(name)) ||
            !zizhan_dbg_sysctl_name_valid(name)) {
            zdbg_send_json(fd, 403, "Forbidden", "{\"ok\":false,\"error\":\"sysctl_not_allowed\"}\n");
            return;
        }
        if (is_post) {
            char val[128] = "";
            if (!zdbg_query_param(query, "value", val, sizeof(val))) {
                zdbg_send_json(fd, 400, "Bad Request", "{\"ok\":false,\"error\":\"missing_value\"}\n");
                return;
            }
            if (!zizhan_dbg_sysctl_write_allowed(name, val, g_zdbg_user_present ? 1 : 0)) {
                zdbg_send_json(fd, 403, "Forbidden",
                               "{\"ok\":false,\"error\":\"sysctl_write_blocked_by_safety_policy\"}\n");
                return;
            }
            int wrc = zdbg_backend_sysctl_set(name, val);
            if (wrc != 0) {
                char j[128];
                snprintf(j, sizeof(j), "{\"ok\":false,\"error\":\"sysctl_write_failed\",\"errno\":%d}\n", wrc);
                zdbg_send_json(fd, 400, "Bad Request", j);
                return;
            }
        }
        char out_val[512] = "";
        int rrc = zdbg_backend_sysctl_get(name, out_val, sizeof(out_val));
        if (rrc != 0) {
            char j[128];
            snprintf(j, sizeof(j), "{\"ok\":false,\"error\":\"sysctl_read_failed\",\"errno\":%d}\n", rrc);
            zdbg_send_json(fd, 404, "Not Found", j);
            return;
        }
        char en[160], ev[640], json[900];
        zdbg_json_escape(name, en, sizeof(en));
        zdbg_json_escape(out_val, ev, sizeof(ev));
        snprintf(json, sizeof(json), "{\"ok\":true,\"name\":\"%s\",\"value\":\"%s\"}\n", en, ev);
        zdbg_send_json(fd, 200, "OK", json);
        return;
    }

    if (is_post && strcmp(path, "/api/sample") == 0) {
        char proc_s[32] = "SpringBoard";
        (void)zdbg_query_param(query, "proc", proc_s, sizeof(proc_s));
        int64_t secs = zdbg_query_i64(query, "seconds", 3);
        ZizhanDbgCtlReq creq;
        ZizhanDbgStatusResp st;
        memset(&creq, 0, sizeof(creq));
        creq.op = ZIZHAN_DBG_OP_SAMPLE_PROC;
        creq.duration_ms = (uint32_t)(secs < 1 ? 1 : (secs > 15 ? 15 : secs));
        snprintf(creq.text, sizeof(creq.text), "%s", proc_s);
        int rc = zdbg_backend_ctl(&creq, &st);
        if (rc != 0) {
            char j[128];
            snprintf(j, sizeof(j), "{\"ok\":false,\"error\":\"sample_failed\",\"errno\":%d}\n", rc);
            zdbg_send_json(fd, rc == EBUSY ? 409 : 500, rc == EBUSY ? "Conflict" : "Internal Server Error", j);
            return;
        }
        char json[1024];
        zdbg_format_status_json(&st, json, sizeof(json));
        zdbg_send_json(fd, 200, "OK", json);
        return;
    }

    if (is_post && strcmp(path, "/api/reload-ui") == 0) {
        if (!body || body_len < 64u) {
            zdbg_send_json(fd, 400, "Bad Request", "{\"ok\":false,\"error\":\"empty_dylib_body\"}\n");
            return;
        }
        char err_msg[160] = "";
        char sha_hex[65] = "";
        int rc = zdbg_stage_and_arm_hot_ui(body, body_len, err_msg, sizeof(err_msg), sha_hex);
        if (rc != 0) {
            char esc[200], j[320];
            zdbg_json_escape(err_msg, esc, sizeof(esc));
            snprintf(j, sizeof(j), "{\"ok\":false,\"error\":\"%s\",\"errno\":%d}\n", esc, rc);
            zdbg_send_json(fd, 400, "Bad Request", j);
            return;
        }
        char json[256];
        snprintf(json, sizeof(json),
                 "{\"ok\":true,\"state\":\"trial\",\"bytes\":%zu,\"sha256\":\"%s\"}\n",
                 body_len, sha_hex);
        zdbg_send_json(fd, 200, "OK", json);
        return;
    }

    if (is_post && strcmp(path, "/api/revert-ui") == 0) {
        (void)__atomic_add_fetch(&g_zdbg_trial_gen, 1ull, __ATOMIC_ACQ_REL);
        ZizhanDbgCtlReq creq;
        ZizhanDbgStatusResp st;
        memset(&creq, 0, sizeof(creq));
        creq.op = ZIZHAN_DBG_OP_HOT_UI;
        creq.target = ZIZHAN_HOT_UI_REVERT;
        int rc = zdbg_backend_ctl(&creq, &st);
        if (rc == 0 && zdbg_query_i64(query, "restart", 1) != 0) {
            memset(&creq, 0, sizeof(creq));
            creq.op = ZIZHAN_DBG_OP_SIGNAL_SB;
            creq.target = 15u;
            (void)zdbg_backend_ctl(&creq, &st);
        }
        if (rc != 0) {
            zdbg_send_json(fd, 500, "Internal Server Error", "{\"ok\":false,\"error\":\"revert_failed\"}\n");
            return;
        }
        char json[1024];
        zdbg_format_status_json(&st, json, sizeof(json));
        zdbg_send_json(fd, 200, "OK", json);
        return;
    }

    if (is_post && strcmp(path, "/api/reboot") == 0) {
        char mode_s[24] = "fastboot";
        (void)zdbg_query_param(query, "target", mode_s, sizeof(mode_s));
        ZizhanDbgCtlReq creq;
        ZizhanDbgStatusResp st;
        memset(&creq, 0, sizeof(creq));
        creq.op = ZIZHAN_DBG_OP_REBOOT;
        if (strcmp(mode_s, "android") == 0) {
            creq.target = ZIZHAN_DBG_REBOOT_ANDROID;
        } else if (strcmp(mode_s, "fastboot") == 0 || strcmp(mode_s, "bootloader") == 0) {
            creq.target = ZIZHAN_DBG_REBOOT_FASTBOOT;
        } else {
            zdbg_send_json(fd, 400, "Bad Request", "{\"ok\":false,\"error\":\"invalid_reboot_target\"}\n");
            return;
        }
        int rc = zdbg_backend_ctl(&creq, &st);
        if (rc != 0) {
            char j[128];
            snprintf(j, sizeof(j), "{\"ok\":false,\"error\":\"reboot_busy\",\"errno\":%d}\n", rc);
            zdbg_send_json(fd, 409, "Conflict", j);
            return;
        }
        char json[128];
        snprintf(json, sizeof(json), "{\"ok\":true,\"rebooting\":\"%s\"}\n",
                 creq.target == ZIZHAN_DBG_REBOOT_ANDROID ? "android" : "fastboot");
        zdbg_send_json(fd, 200, "OK", json);
        return;
    }

    zdbg_send_json(fd, 404, "Not Found", "{\"ok\":false,\"error\":\"not_found\"}\n");
}

/* --- Per-Connection Reader & Accept Loop --- */

static void *zdbg_client_worker(void *arg) {
    const int fd = (int)(intptr_t)arg;
    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#ifdef SO_NOSIGPIPE
    int one = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    int nodelay = 1;
    (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    char hdr_buf[8192];
    size_t used = 0u;
    ssize_t hdr_end = -1;
    while (used + 1u < sizeof(hdr_buf)) {
        ssize_t n = recv(fd, hdr_buf + used, sizeof(hdr_buf) - 1u - used, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) {
                continue;
            }
            break;
        }
        used += (size_t)n;
        hdr_buf[used] = '\0';
        const char *sep = strstr(hdr_buf, "\r\n\r\n");
        if (sep) {
            hdr_end = (ssize_t)(sep - hdr_buf) + 4;
            break;
        }
    }

    if (hdr_end < 0) {
        zdbg_send_json(fd, 400, "Bad Request", "{\"ok\":false,\"error\":\"bad_http_header\"}\n");
        close(fd);
        (void)__atomic_sub_fetch(&g_zdbg_active_clients, 1u, __ATOMIC_ACQ_REL);
        return NULL;
    }

    char method[16] = "", url[1024] = "";
    {
        const char *sp1 = (const char *)memchr(hdr_buf, ' ', (size_t)hdr_end);
        const char *eol = strstr(hdr_buf, "\r\n");
        if (!sp1 || !eol || sp1 >= eol) {
            zdbg_send_json(fd, 400, "Bad Request", "{\"ok\":false,\"error\":\"bad_request_line\"}\n");
            close(fd);
            (void)__atomic_sub_fetch(&g_zdbg_active_clients, 1u, __ATOMIC_ACQ_REL);
            return NULL;
        }
        size_t mlen = (size_t)(sp1 - hdr_buf);
        if (mlen >= sizeof(method)) { mlen = sizeof(method) - 1u; }
        memcpy(method, hdr_buf, mlen);
        method[mlen] = '\0';

        const char *u0 = sp1 + 1;
        const char *sp2 = (const char *)memchr(u0, ' ', (size_t)(eol - u0));
        size_t ulen = sp2 ? (size_t)(sp2 - u0) : (size_t)(eol - u0);
        if (ulen >= sizeof(url)) { ulen = sizeof(url) - 1u; }
        memcpy(url, u0, ulen);
        url[ulen] = '\0';
    }

    char path[256] = "", query[896] = "";
    const char *qmark = strchr(url, '?');
    if (qmark) {
        size_t plen = (size_t)(qmark - url);
        if (plen >= sizeof(path)) { plen = sizeof(path) - 1u; }
        memcpy(path, url, plen);
        path[plen] = '\0';
        snprintf(query, sizeof(query), "%s", qmark + 1);
    } else {
        snprintf(path, sizeof(path), "%s", url);
    }

    char cl_str[32] = "";
    size_t content_len = 0u;
    if (zdbg_header_value(hdr_buf, "Content-Length", cl_str, sizeof(cl_str))) {
        char *end = NULL;
        unsigned long long cl = strtoull(cl_str, &end, 10);
        if (end == cl_str || *end != '\0') {
            zdbg_send_json(fd, 400, "Bad Request", "{\"ok\":false,\"error\":\"bad_content_length\"}\n");
            close(fd);
            (void)__atomic_sub_fetch(&g_zdbg_active_clients, 1u, __ATOMIC_ACQ_REL);
            return NULL;
        }
        const size_t max_body = (strcmp(path, "/api/reload-ui") == 0) ? (4u * 1024u * 1024u) : 8192u;
        if (cl > max_body) {
            zdbg_send_json(fd, 413, "Payload Too Large", "{\"ok\":false,\"error\":\"payload_too_large\"}\n");
            close(fd);
            (void)__atomic_sub_fetch(&g_zdbg_active_clients, 1u, __ATOMIC_ACQ_REL);
            return NULL;
        }
        content_len = (size_t)cl;
    }

    uint8_t *body = NULL;
    if (content_len > 0u) {
        body = (uint8_t *)malloc(content_len + 1u);
        if (!body) {
            zdbg_send_json(fd, 500, "Internal Server Error", "{\"ok\":false,\"error\":\"oom\"}\n");
            close(fd);
            (void)__atomic_sub_fetch(&g_zdbg_active_clients, 1u, __ATOMIC_ACQ_REL);
            return NULL;
        }
        const size_t already = used - (size_t)hdr_end;
        const size_t copy0 = already < content_len ? already : content_len;
        if (copy0 > 0u) {
            memcpy(body, hdr_buf + hdr_end, copy0);
        }
        size_t got = copy0;
        while (got < content_len) {
            ssize_t n = recv(fd, body + got, content_len - got, 0);
            if (n <= 0) {
                if (n < 0 && errno == EINTR) {
                    continue;
                }
                free(body);
                close(fd);
                (void)__atomic_sub_fetch(&g_zdbg_active_clients, 1u, __ATOMIC_ACQ_REL);
                return NULL;
            }
            got += (size_t)n;
        }
        body[content_len] = '\0';
    }

    hdr_buf[hdr_end] = '\0';
    zdbg_handle_http(fd, method, path, query, hdr_buf, body, content_len);
    free(body);
    close(fd);
    (void)__atomic_sub_fetch(&g_zdbg_active_clients, 1u, __ATOMIC_ACQ_REL);
    return NULL;
}

static void *zdbg_accept_loop(void *unused) {
    (void)unused;
    while (!__atomic_load_n(&g_zdbg_stop_flag, __ATOMIC_ACQUIRE)) {
        struct sockaddr_in cli;
        socklen_t clen = sizeof(cli);
        int cfd = accept(g_zdbg_listen_fd, (struct sockaddr *)&cli, &clen);
        if (cfd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (__atomic_load_n(&g_zdbg_stop_flag, __ATOMIC_ACQUIRE)) {
                break;
            }
            usleep(50 * 1000);
            continue;
        }
        if (__atomic_load_n(&g_zdbg_stop_flag, __ATOMIC_ACQUIRE)) {
            close(cfd);
            break;
        }
        uint32_t cur = __atomic_add_fetch(&g_zdbg_active_clients, 1u, __ATOMIC_ACQ_REL);
        if (cur > 6u) {
            zdbg_send_json(cfd, 503, "Service Unavailable", "{\"ok\":false,\"error\":\"busy\"}\n");
            close(cfd);
            (void)__atomic_sub_fetch(&g_zdbg_active_clients, 1u, __ATOMIC_ACQ_REL);
            continue;
        }
        pthread_t th;
        if (pthread_create(&th, NULL, zdbg_client_worker, (void *)(intptr_t)cfd) == 0) {
            pthread_detach(th);
        } else {
            close(cfd);
            (void)__atomic_sub_fetch(&g_zdbg_active_clients, 1u, __ATOMIC_ACQ_REL);
        }
    }
    return NULL;
}

static int zdbg_start_listener(const char *bind_ip, uint16_t port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        return -1;
    }
    int flags = fcntl(s, F_GETFD);
    if (flags >= 0) {
        (void)fcntl(s, F_SETFD, flags | FD_CLOEXEC);
    }
    int one = 1;
    (void)setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    if (inet_pton(AF_INET, bind_ip, &sin.sin_addr) != 1) {
        close(s);
        return -1;
    }
    if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) != 0 || listen(s, 16) != 0) {
        close(s);
        return -1;
    }
    socklen_t slen = sizeof(sin);
    if (getsockname(s, (struct sockaddr *)&sin, &slen) == 0) {
        g_zdbg_bound_port = ntohs(sin.sin_port);
    } else {
        g_zdbg_bound_port = port;
    }
    g_zdbg_listen_fd = s;
    return 0;
}

#ifndef ZIZHAN_DEBUGD_HOST_TEST
static void zizhan_debugd_start_backboard(bool user_present, bool allow_notoken, uint16_t port) {
    g_zdbg_user_present = user_present;
    g_zdbg_allow_notoken = allow_notoken;
    g_zdbg_token_len = 0u;
    memset(g_zdbg_token, 0, sizeof(g_zdbg_token));

    size_t tsz = sizeof(g_zdbg_token) - 1u;
    if (sysctlbyname("kern.zizhan_dbg_token", g_zdbg_token, &tsz, NULL, 0) == 0 &&
        tsz >= ZIZHAN_DBG_TOKEN_MIN_LEN && tsz <= ZIZHAN_DBG_TOKEN_MAX_LEN) {
        g_zdbg_token[tsz] = '\0';
        g_zdbg_token_len = strlen(g_zdbg_token);
    } else {
        g_zdbg_token[0] = '\0';
        g_zdbg_token_len = 0u;
    }

    if (g_zdbg_token_len < ZIZHAN_DBG_TOKEN_MIN_LEN && !g_zdbg_allow_notoken) {
        fprintf(stderr, "ZIZHAN_DEBUGD: SKIP missing /chosen/zizhan-dbg-token (pass zizhan_dbg_notoken=1 only for local loopback tests)\n");
        return;
    }
    const char *auth_mode = "loopback-or-notoken";
    const char *bind_ip = zizhan_dbg_select_bind_ip(g_zdbg_token_len,
                                                    g_zdbg_allow_notoken ? 1 : 0,
                                                    &auth_mode);
    if (zdbg_start_listener(bind_ip, port ? port : ZIZHAN_DBG_DEFAULT_PORT) != 0) {
        fprintf(stderr, "ZIZHAN_DEBUGD: FAIL bind %s:%u (errno=%d)\n",
                bind_ip, (unsigned)(port ? port : ZIZHAN_DBG_DEFAULT_PORT), errno);
        return;
    }
    pthread_t th;
    if (pthread_create(&th, NULL, zdbg_accept_loop, NULL) == 0) {
        pthread_detach(th);
        fprintf(stderr, "ZIZHAN_DEBUGD: READY %s:%u auth=%s user_present=%u\n",
                bind_ip, (unsigned)g_zdbg_bound_port,
                auth_mode,
                user_present ? 1u : 0u);
    } else {
        close(g_zdbg_listen_fd);
        g_zdbg_listen_fd = -1;
    }
}

static void zizhan_debugd_confirm_springboard(void *unused) {
    (void)unused;
    ZizhanDbgCtlReq creq;
    ZizhanDbgStatusResp st;
    memset(&creq, 0, sizeof(creq));
    creq.op = ZIZHAN_DBG_OP_HOT_UI;
    creq.target = ZIZHAN_HOT_UI_CONFIRM;
    if (zdbg_backend_ctl(&creq, &st) == 0) {
        fprintf(stderr, "ZIZHAN_HOT_UI: SpringBoard main-queue healthy at +5s (state=%u rollbacks=%u)\n",
                st.hot_ui_state, st.hot_ui_rollbacks);
    }
}
#endif /* !ZIZHAN_DEBUGD_HOST_TEST */

#endif /* ZIZHAN_DEBUGD18_H */
