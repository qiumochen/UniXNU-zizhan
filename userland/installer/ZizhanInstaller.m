/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* ZizhanInstaller.app v2 (/Applications/ZizhanInstaller.app, com.zizhan.installer):
 * Native iPadOS 18 (22H374) third-party decrypted .ipa installer & manager for Xiaomi MIX Fold 2.
 *
 * v2 Upgrades (Tier 2.1 APP-03):
 *  - Streaming chunked HTTP upload up to 12 GiB (256 KiB socket buffer) + live XHR progress bar (%, MB/MB, MB/s).
 *  - Recursive Mach-O inspection & Ad-hoc 4K + XML entitlements signing across main executable,
 *    Frameworks (.dylib / .framework), and PlugIns (.appex)!
 *  - Installed-app action sheet: Launch / View Bundle & Log Summary / One-Tap Uninstall.
 *  - Unattended modes: zizhan_ipa_autotest=1 (ZizhanDemoApp-16k.ipa) & zizhan_ipa_autotest=2 (ZizhanCompatSuite-16k.ipa).
 */
#import <UIKit/UIKit.h>
#import <objc/message.h>
#import <objc/runtime.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include "../zizhan-ipa-install18.h"

struct archive;
struct archive_entry;
static struct archive *(*p_archive_read_new)(void);
static int (*p_archive_read_support_format_zip)(struct archive *);
static int (*p_archive_read_open_filename)(struct archive *, const char *, size_t);
static int (*p_archive_read_next_header)(struct archive *, struct archive_entry **);
static const char *(*p_archive_entry_pathname)(struct archive_entry *);
static mode_t (*p_archive_entry_filetype)(struct archive_entry *);
static mode_t (*p_archive_entry_perm)(struct archive_entry *);
static ssize_t (*p_archive_read_data)(struct archive *, void *, size_t);
static int (*p_archive_read_free)(struct archive *);

static bool ensure_libarchive(void) {
    static dispatch_once_t once;
    static bool ok = false;
    dispatch_once(&once, ^{
        void *h = dlopen("/usr/lib/libarchive.2.dylib", RTLD_LAZY);
        if (!h) return;
        p_archive_read_new                = dlsym(h, "archive_read_new");
        p_archive_read_support_format_zip = dlsym(h, "archive_read_support_format_zip");
        p_archive_read_open_filename      = dlsym(h, "archive_read_open_filename");
        p_archive_read_next_header        = dlsym(h, "archive_read_next_header");
        p_archive_entry_pathname          = dlsym(h, "archive_entry_pathname");
        p_archive_entry_filetype          = dlsym(h, "archive_entry_filetype");
        p_archive_entry_perm              = dlsym(h, "archive_entry_perm");
        p_archive_read_data               = dlsym(h, "archive_read_data");
        p_archive_read_free               = dlsym(h, "archive_read_free");
        ok = p_archive_read_new && p_archive_read_support_format_zip &&
             p_archive_read_open_filename && p_archive_read_next_header &&
             p_archive_entry_pathname && p_archive_entry_filetype &&
             p_archive_read_data && p_archive_read_free;
    });
    return ok;
}

static bool installer_has_token(const char *args, const char *opt) {
    const size_t n = strlen(opt);
    for (const char *p = args; (p = strstr(p, opt)) != NULL; ++p) {
        if ((p == args || p[-1] == ' ' || p[-1] == '\t') &&
            (p[n] == '\0' || p[n] == ' ' || p[n] == '\t')) return true;
    }
    return false;
}

static bool installer_boot_option(const char *opt) {
    char args[2048] = {0};
    size_t len = sizeof(args) - 1;
    if (sysctlbyname("kern.zizhan_bootargs", args, &len, NULL, 0) == 0 && installer_has_token(args, opt))
        return true;
    memset(args, 0, sizeof(args));
    len = sizeof(args) - 1;
    if (sysctlbyname("kern.bootargs", args, &len, NULL, 0) == 0 && installer_has_token(args, opt))
        return true;
    return false;
}

static NSString *current_lan_ip(void) {
    struct ifaddrs *ifap = NULL;
    NSString *best = @"127.0.0.1";
    if (getifaddrs(&ifap) == 0) {
        for (struct ifaddrs *ifa = ifap; ifa; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
            if (!strcmp(ifa->ifa_name, "lo0")) continue;
            char ip[INET_ADDRSTRLEN] = {0};
            struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;
            if (inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip))) {
                best = [NSString stringWithUTF8String:ip];
                if (!strcmp(ifa->ifa_name, "en0")) break;
            }
        }
        freeifaddrs(ifap);
    }
    return best;
}

static double data_free_gib(double *totalGibOut) {
    struct statvfs vfs;
    if (statvfs("/private/var/mobile", &vfs) == 0) {
        if (totalGibOut) *totalGibOut = ((double)vfs.f_blocks * vfs.f_frsize) / (1024.0 * 1024.0 * 1024.0);
        return ((double)vfs.f_bavail * vfs.f_frsize) / (1024.0 * 1024.0 * 1024.0);
    }
    if (totalGibOut) *totalGibOut = 0.0;
    return 0.0;
}

static bool is_safe_zip_relpath(NSString *rel) {
    if (!rel.length || [rel hasPrefix:@"/"]) return false;
    for (NSString *comp in [rel pathComponents]) {
        if ([comp isEqualToString:@".."]) return false;
    }
    return true;
}

static bool extract_zip_to_dir(NSString *ipaPath, NSString *destDir, NSString **errOut) {
    if (!ensure_libarchive()) {
        if (errOut) *errOut = @"libarchive.2.dylib 不可用";
        return false;
    }
    NSFileManager *fm = [NSFileManager defaultManager];
    struct archive *a = p_archive_read_new();
    p_archive_read_support_format_zip(a);
    if (p_archive_read_open_filename(a, [ipaPath fileSystemRepresentation], 262144) != 0) {
        p_archive_read_free(a);
        if (errOut) *errOut = @"无法打开 .ipa ZIP 压缩包";
        return false;
    }
    struct archive_entry *entry = NULL;
    const size_t kBufSz = 262144;
    uint8_t *chunk = malloc(kBufSz);
    bool ok = chunk != NULL;
    unsigned extractedFiles = 0;
    while (ok && p_archive_read_next_header(a, &entry) == 0) {
        const char *rawRel = p_archive_entry_pathname(entry);
        if (!rawRel || !rawRel[0]) continue;
        NSString *rel = [NSString stringWithUTF8String:rawRel];
        if (!is_safe_zip_relpath(rel)) continue;
        NSString *full = [destDir stringByAppendingPathComponent:rel];

        mode_t ftype = p_archive_entry_filetype(entry);
        if ((ftype & S_IFMT) == S_IFDIR || rawRel[strlen(rawRel) - 1] == '/') {
            [fm createDirectoryAtPath:full withIntermediateDirectories:YES attributes:nil error:nil];
            chmod([full fileSystemRepresentation], 0755);
            continue;
        }
        NSString *parentDir = [full stringByDeletingLastPathComponent];
        [fm createDirectoryAtPath:parentDir withIntermediateDirectories:YES attributes:nil error:nil];
        chmod([parentDir fileSystemRepresentation], 0755);
        int fd = open([full fileSystemRepresentation], O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { ok = false; break; }
        ssize_t r = 0;
        while ((r = p_archive_read_data(a, chunk, kBufSz)) > 0) {
            ssize_t off = 0;
            while (off < r) {
                ssize_t w = write(fd, chunk + off, (size_t)(r - off));
                if (w <= 0) { ok = false; break; }
                off += w;
            }
            if (!ok) break;
        }
        close(fd);
        mode_t perm = p_archive_entry_perm ? p_archive_entry_perm(entry) : 0644;
        chmod([full fileSystemRepresentation], (perm & 0111) ? 0755 : 0644);
        ++extractedFiles;
    }
    free(chunk);
    p_archive_read_free(a);
    if ((!ok || extractedFiles == 0) && errOut) *errOut = @"解压 .ipa 文件失败（请确认 Data 空间充足）";
    return ok && extractedFiles > 0;
}

static int inspect_or_sign_macho_file(NSString *binPath, NSString *bundleId, ZizhanMachoReport *repOut, NSString **errOut) {
    NSData *data = [NSData dataWithContentsOfFile:binPath options:NSDataReadingMappedIfSafe error:nil];
    if (!data || data.length < 64) {
        if (errOut) *errOut = @"可执行文件缺失或过小";
        return ZIZHAN_IPA_ERR_TRUNCATED;
    }
    ZizhanMachoReport rep;
    int rc = zizhan_macho_inspect((const uint8_t *)data.bytes, data.length, &rep);
    if (rc == ZIZHAN_IPA_ERR_FAIRPLAY_ENCRYPTED) {
        if (repOut) *repOut = rep;
        if (errOut) *errOut = [NSString stringWithFormat:@"检测到 App Store FairPlay 硬件加密 (cryptid=%u)，请使用已脱壳 (Decrypted) IPA", rep.cryptid];
        return rc;
    }
    if (rc != ZIZHAN_IPA_OK) {
        if (errOut) *errOut = [NSString stringWithFormat:@"非标准 arm64 Mach-O 可执行文件 (rc=%d)", rc];
        return rc;
    }
    if (!rep.has_code_sig && rep.slice_offset == 0) {
        const char *ident = bundleId.length ? [bundleId UTF8String] : "com.zizhan.adhoc";
        size_t cap = data.length + zizhan_macho_adhoc_blob_bound(data.length, ident);
        NSMutableData *mut = [NSMutableData dataWithLength:cap];
        if (mut) {
            memcpy(mut.mutableBytes, data.bytes, data.length);
            size_t newLen = 0;
            if (zizhan_macho_sign_adhoc_4k((uint8_t *)mut.mutableBytes, data.length, cap, ident, &newLen) == ZIZHAN_IPA_OK) {
                mut.length = newLen;
                if ([mut writeToFile:binPath atomically:YES]) {
                    chmod([binPath fileSystemRepresentation], 0755);
                    (void)zizhan_macho_inspect((const uint8_t *)mut.bytes, mut.length, &rep);
                }
            }
        }
    }
    chmod([binPath fileSystemRepresentation], 0755);
    if (repOut) *repOut = rep;
    return ZIZHAN_IPA_OK;
}

static void sign_nested_bundles_in_app(NSString *appDir, NSString *fallbackBid) {
    NSFileManager *fm = [NSFileManager defaultManager];
    NSArray<NSString *> *containers = @[@"Frameworks", @"PlugIns"];
    for (NSString *cname in containers) {
        NSString *dir = [appDir stringByAppendingPathComponent:cname];
        for (NSString *sub in [fm contentsOfDirectoryAtPath:dir error:nil]) {
            NSString *p = [dir stringByAppendingPathComponent:sub];
            if ([sub hasSuffix:@".dylib"]) {
                chmod([p fileSystemRepresentation], 0755);
                (void)inspect_or_sign_macho_file(p, fallbackBid, NULL, NULL);
            } else if ([sub hasSuffix:@".framework"] || [sub hasSuffix:@".appex"] || [sub hasSuffix:@".bundle"]) {
                NSDictionary *finfo = [NSDictionary dictionaryWithContentsOfFile:[p stringByAppendingPathComponent:@"Info.plist"]];
                NSString *fbid = finfo[@"CFBundleIdentifier"] ?: fallbackBid;
                NSString *fexe = finfo[@"CFBundleExecutable"] ?: [sub stringByDeletingPathExtension];
                NSString *fp = [p stringByAppendingPathComponent:fexe];
                if ([fm fileExistsAtPath:fp]) {
                    chmod([fp fileSystemRepresentation], 0755);
                    (void)inspect_or_sign_macho_file(fp, fbid, NULL, NULL);
                }
            }
        }
    }
}

@interface ZizhanIpaItem : NSObject
@property (nonatomic, copy) NSString *path;
@property (nonatomic, copy) NSString *fileName;
@property (nonatomic, assign) uint64_t fileBytes;
@end
@implementation ZizhanIpaItem
@end

@interface ZizhanInstallerController : UITableViewController
@property (nonatomic, strong) NSMutableArray<ZizhanIpaItem *> *ipas;
@property (nonatomic, strong) NSMutableArray<NSDictionary *> *installedApps;
@property (nonatomic, copy) NSString *statusMessage;
- (void)reloadAll;
@end

static ZizhanInstallerController *gMainController;

/* --- LAN HTTP Streaming IPA Upload Server v2 (port 8088, up to 12 GiB) --- */
static void handle_http_client(int cfd) {
    char hdr[4096];
    ssize_t n = recv(cfd, hdr, sizeof(hdr) - 1, 0);
    if (n <= 0) { close(cfd); return; }
    hdr[n] = '\0';

    if (strncmp(hdr, "GET ", 4) == 0) {
        const char *html =
            "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"
            "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>MIX Fold 2 · IPA 局域网安装器 v2</title>"
            "<style>body{font-family:-apple-system,sans-serif;background:#0f141e;color:#e8eef7;padding:28px;max-width:600px;margin:auto}"
            ".box{border:2px dashed #3b82f6;border-radius:16px;padding:32px;text-align:center;background:#161f30}"
            "button,input{margin-top:16px;font-size:16px;padding:10px 20px;border-radius:10px}"
            "progress{width:100%;height:18px;margin-top:16px;display:none}</style></head>"
            "<body><h2>Xiaomi MIX Fold 2 · 大体积脱壳 IPA 流式投递 (v2)</h2>"
            "<div class='box'><p>支持 >2 GB 至 12 GB 已脱壳 (cryptid=0) .ipa 流式直写手机 Data 分区：</p>"
            "<input id='f' type='file' accept='.ipa'><br><button onclick='up()'>流式上传至手机</button>"
            "<progress id='pg' max='100' value='0'></progress>"
            "<p id='st'></p></div>"
            "<script>function up(){const f=document.getElementById('f').files[0];if(!f)return;"
            "const pg=document.getElementById('pg'),st=document.getElementById('st');pg.style.display='block';pg.value=0;"
            "const t0=Date.now(),xhr=new XMLHttpRequest();"
            "xhr.open('PUT','/upload?name='+encodeURIComponent(f.name),true);"
            "xhr.upload.onprogress=function(e){if(e.lengthComputable){const pct=(e.loaded*100/e.total).toFixed(1);"
            "const mb=(e.loaded/1048576).toFixed(1),tot=(e.total/1048576).toFixed(1);"
            "const spd=(e.loaded/1048576/Math.max(0.1,(Date.now()-t0)/1000)).toFixed(1);"
            "pg.value=pct;st.textContent='上传中 '+pct+'% ('+mb+' / '+tot+' MB, '+spd+' MB/s)';}};"
            "xhr.onload=function(){st.textContent=xhr.responseText;};"
            "xhr.onerror=function(){st.textContent='网络上传中断';};"
            "xhr.send(f);}</script></body></html>";
        send(cfd, html, strlen(html), 0);
        close(cfd);
        return;
    }

    if (strncmp(hdr, "PUT /upload", 11) == 0) {
        char *hdrEnd = strstr(hdr, "\r\n\r\n");
        long long contentLen = 0;
        const char *cl = strcasestr(hdr, "Content-Length:");
        if (cl) contentLen = strtoll(cl + 15, NULL, 10);
        char fname[160] = "uploaded.ipa";
        const char *np = strstr(hdr, "name=");
        if (np) {
            np += 5;
            size_t k = 0;
            while (np[k] && np[k] != ' ' && np[k] != '&' && np[k] != '\r' && k + 1 < sizeof(fname)) {
                char c = np[k];
                fname[k] = (c == '/' || c == '\\') ? '_' : c;
                ++k;
            }
            fname[k] = '\0';
        }
        NSString *dir = @"/private/var/mobile/Documents/IPAs";
        [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
        NSString *dst = [dir stringByAppendingPathComponent:[NSString stringWithUTF8String:fname]];
        int fd = open([dst fileSystemRepresentation], O_WRONLY | O_CREAT | O_TRUNC, 0644);
        long long written = 0;
        const long long kMaxUpload = (12LL << 30); /* 12 GiB streaming ceiling */
        if (fd >= 0 && hdrEnd && contentLen > 0 && contentLen <= kMaxUpload) {
            size_t bodyInHdr = (size_t)(n - ((hdrEnd + 4) - hdr));
            if (bodyInHdr > 0) {
                write(fd, hdrEnd + 4, bodyInHdr);
                written += (long long)bodyInHdr;
            }
            const size_t kChunk = 262144;
            char *buf = malloc(kChunk);
            while (buf && written < contentLen) {
                ssize_t r = recv(cfd, buf, kChunk, 0);
                if (r <= 0) break;
                ssize_t woff = 0;
                while (woff < r) {
                    ssize_t w = write(fd, buf + woff, (size_t)(r - woff));
                    if (w <= 0) { r = -1; break; }
                    woff += w;
                }
                if (r <= 0) break;
                written += r;
            }
            free(buf);
            close(fd);
        } else if (fd >= 0) {
            close(fd);
        }
        char resp[320];
        snprintf(resp, sizeof(resp),
                 "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nConnection: close\r\n\r\n"
                 "上传完成 (%.1f MB)！请在手机「IPA 安装器」点击对应条目一键安装。",
                 written / 1048576.0);
        send(cfd, resp, strlen(resp), 0);
        close(cfd);
        dispatch_async(dispatch_get_main_queue(), ^{
            [gMainController reloadAll];
        });
        return;
    }
    close(cfd);
}

static void *http_server_thread(void *unused) {
    (void)unused;
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) return NULL;
    int yes = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8088);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(sfd, 4) != 0) {
        close(sfd);
        return NULL;
    }
    fprintf(stderr, "ZIZHAN_IPA_INSTALLER: v2 HTTP streaming upload server listening on 0.0.0.0:8088 (max=12GiB)\n");
    for (;;) {
        int cfd = accept(sfd, NULL, NULL);
        if (cfd >= 0) handle_http_client(cfd);
    }
}

@implementation ZizhanInstallerController

- (void)viewDidLoad {
    [super viewDidLoad];
    gMainController = self;
    self.title = @"MIX Fold 2 · IPA 安装器 v2";
    self.ipas = [NSMutableArray array];
    self.installedApps = [NSMutableArray array];

    self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc]
        initWithTitle:@"刷新" style:UIBarButtonItemStylePlain target:self action:@selector(reloadAll)];

    [[NSFileManager defaultManager] createDirectoryAtPath:@"/private/var/mobile/Documents/IPAs"
                              withIntermediateDirectories:YES attributes:nil error:nil];
    pthread_t th;
    if (pthread_create(&th, NULL, http_server_thread, NULL) == 0) pthread_detach(th);

    [self reloadAll];
    double totGib = 0.0, freeGib = data_free_gib(&totGib);
    fprintf(stderr, "ZIZHAN_IPA_INSTALLER: UI_READY v2 ipas=%lu installed_user=%lu data_free_gib=%.2f data_total_gib=%.2f\n",
            (unsigned long)self.ipas.count, (unsigned long)self.installedApps.count, freeGib, totGib);
    fflush(stderr);

    if (installer_boot_option("zizhan_ipa_autotest=3")) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            [self runBootstrapAllDesktopApps];
        });
    } else if (installer_boot_option("zizhan_ipa_autotest=2")) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            [self runAutoTestWithTarget:@"/pong/ipas/ZizhanCompatSuite-16k.ipa"];
        });
    } else if (installer_boot_option("zizhan_ipa_autotest=1")) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            [self runAutoTestWithTarget:@"/pong/ipas/ZizhanDemoApp-16k.ipa"];
        });
    }
}

- (void)reloadAll {
    [self.ipas removeAllObjects];
    NSFileManager *fm = [NSFileManager defaultManager];
    NSArray<NSString *> *dirs = @[
        @"/private/var/mobile/Documents/IPAs",
        @"/private/var/mobile/Documents",
        @"/pong/ipas"
    ];
    NSMutableSet<NSString *> *seen = [NSMutableSet set];
    for (NSString *dir in dirs) {
        NSArray<NSString *> *names = [fm contentsOfDirectoryAtPath:dir error:nil];
        for (NSString *n in names) {
            if (![[n lowercaseString] hasSuffix:@".ipa"]) continue;
            NSString *full = [[dir stringByAppendingPathComponent:n] stringByStandardizingPath];
            if ([seen containsObject:full]) continue;
            [seen addObject:full];
            NSDictionary *attr = [fm attributesOfItemAtPath:full error:nil];
            ZizhanIpaItem *item = [[ZizhanIpaItem alloc] init];
            item.path = full;
            item.fileName = n;
            item.fileBytes = [attr fileSize];
            [self.ipas addObject:item];
        }
    }

    [self.installedApps removeAllObjects];
    Class wsCls = objc_lookUpClass("LSApplicationWorkspace");
    id ws = wsCls ? ((id(*)(Class, SEL))objc_msgSend)(wsCls, sel_registerName("defaultWorkspace")) : nil;
    SEL sEnum = sel_registerName("enumerateApplicationsOfType:block:");
    if (ws && [ws respondsToSelector:sEnum]) {
        NSMutableSet<NSString *> *seenBids = [NSMutableSet set];
        for (NSUInteger t = 0; t <= 1; ++t) {
            ((void(*)(id, SEL, NSUInteger, void (^)(id)))objc_msgSend)(ws, sEnum, t, ^(id proxy) {
                SEL sType = sel_registerName("applicationType");
                SEL sBid  = sel_registerName("applicationIdentifier");
                SEL sName = sel_registerName("localizedName");
                SEL sUrl  = sel_registerName("bundleURL");
                SEL sData = sel_registerName("dataContainerURL");
                NSString *atype = [proxy respondsToSelector:sType] ? ((id(*)(id, SEL))objc_msgSend)(proxy, sType) : nil;
                NSString *bid   = [proxy respondsToSelector:sBid]  ? ((id(*)(id, SEL))objc_msgSend)(proxy, sBid)  : nil;
                NSString *lname = [proxy respondsToSelector:sName] ? ((id(*)(id, SEL))objc_msgSend)(proxy, sName) : bid;
                NSURL *burl     = [proxy respondsToSelector:sUrl]  ? ((id(*)(id, SEL))objc_msgSend)(proxy, sUrl)  : nil;
                NSURL *durl     = [proxy respondsToSelector:sData] ? ((id(*)(id, SEL))objc_msgSend)(proxy, sData) : nil;
                if ([atype isEqualToString:@"User"] && bid.length > 0 && ![seenBids containsObject:bid]) {
                    [seenBids addObject:bid];
                    [self.installedApps addObject:@{
                        @"bundleID": bid,
                        @"name": lname ?: bid,
                        @"bundlePath": burl.path ?: @"",
                        @"dataPath": durl.path ?: @""
                    }];
                }
            });
        }
    }
    double totGib = 0.0, freeGib = data_free_gib(&totGib);
    self.statusMessage = [NSString stringWithFormat:@"网页投递: http://%@:8088 (<=12GB) | Data 可用 %.1f/%.1f GiB | %lu 个 IPA, 已装 %lu 个",
                          current_lan_ip(), freeGib, totGib, (unsigned long)self.ipas.count, (unsigned long)self.installedApps.count];
    [self.tableView reloadData];
}

static BOOL launch_bundle_id(NSString *bid) {
    if (!bid.length) return NO;
    Class wsCls = objc_lookUpClass("LSApplicationWorkspace");
    id ws = wsCls ? ((id(*)(Class, SEL))objc_msgSend)(wsCls, sel_registerName("defaultWorkspace")) : nil;
    SEL sOpen = sel_registerName("openApplicationWithBundleID:");
    BOOL ok = (ws && [ws respondsToSelector:sOpen]) ? ((BOOL(*)(id, SEL, id))objc_msgSend)(ws, sOpen, bid) : NO;
    if (!ok) {
        (void)dlopen("/System/Library/PrivateFrameworks/FrontBoardServices.framework/FrontBoardServices", RTLD_LAZY);
        Class fbsCls = objc_lookUpClass("FBSSystemService");
        id fbs = fbsCls ? ((id(*)(Class, SEL))objc_msgSend)(fbsCls, sel_registerName("sharedService")) : nil;
        SEL sFbsOpen = sel_registerName("openApplication:options:withResult:");
        if (fbs && [fbs respondsToSelector:sFbsOpen]) {
            ((void(*)(id, SEL, id, id, void (^)(NSError *)))objc_msgSend)(fbs, sFbsOpen, bid, nil, ^(NSError *e) {
                fprintf(stderr, "ZIZHAN_IPA_INSTALLER: FBSSystemService open %s err=%s\n",
                        [bid UTF8String], e ? [[e localizedDescription] UTF8String] : "none");
                fflush(stderr);
            });
            ok = YES;
        }
    }
    return ok;
}

- (void)uninstallBundleID:(NSString *)bid {
    if (!bid.length) return;
    BOOL miDone = NO;
    (void)dlopen("/System/Library/PrivateFrameworks/InstalledContentLibrary.framework/InstalledContentLibrary", RTLD_LAZY);
    (void)dlopen("/System/Library/PrivateFrameworks/MobileInstallation.framework/MobileInstallation", RTLD_LAZY);
    Class miClientCls = objc_lookUpClass("MIInstallerClient");
    Class miIdentCls  = objc_lookUpClass("MIAppIdentity");
    SEL sMakeClient = sel_registerName("installerWithProgressBlock:");
    SEL sInitBid    = sel_registerName("initWithBundleID:");
    SEL sUninstId   = sel_registerName("uninstallIdentity:withOptions:completion:");
    if (miClientCls && miIdentCls && [miClientCls respondsToSelector:sMakeClient]) {
        id client = ((id(*)(Class, SEL, id))objc_msgSend)(miClientCls, sMakeClient, nil);
        id ident = [miIdentCls instancesRespondToSelector:sInitBid]
                   ? ((id(*)(id, SEL, id))objc_msgSend)([miIdentCls alloc], sInitBid, bid) : nil;
        if (client && ident && [client respondsToSelector:sUninstId]) {
            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            __block BOOL uok = NO;
            ((void(*)(id, SEL, id, id, void (^)(NSError *)))objc_msgSend)(
                client, sUninstId, ident, @{}, ^(NSError *err) {
                    uok = (err == nil);
                    dispatch_semaphore_signal(sem);
                });
            (void)dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC));
            miDone = uok;
        }
    }
    Class wsCls = objc_lookUpClass("LSApplicationWorkspace");
    id ws = wsCls ? ((id(*)(Class, SEL))objc_msgSend)(wsCls, sel_registerName("defaultWorkspace")) : nil;
    if (!miDone && ws) {
        SEL sUninst = sel_registerName("uninstallApplication:withOptions:");
        if ([ws respondsToSelector:sUninst]) {
            ((BOOL(*)(id, SEL, id, id))objc_msgSend)(ws, sUninst, bid, nil);
        }
    }
    SEL sRebuild = sel_registerName("_LSPrivateRebuildApplicationDatabasesForSystemApps:internal:user:");
    if (ws && [ws respondsToSelector:sRebuild]) {
        ((BOOL(*)(id, SEL, BOOL, BOOL, BOOL))objc_msgSend)(ws, sRebuild, NO, NO, YES);
    }
    [self reloadAll];
}

- (BOOL)installIpaAtPath:(NSString *)ipaPath installedBundleOut:(NSString **)bundleOut errorMessage:(NSString **)errOut {
    NSFileManager *fm = [NSFileManager defaultManager];
    NSString *stageRoot = [@"/private/var/tmp" stringByAppendingPathComponent:
                           [NSString stringWithFormat:@"zizhan-ipa-%@", [[NSUUID UUID] UUIDString]]];
    [fm createDirectoryAtPath:stageRoot withIntermediateDirectories:YES attributes:nil error:nil];
    chmod([stageRoot fileSystemRepresentation], 0777);

    NSString *errMsg = nil;
    if (!extract_zip_to_dir(ipaPath, stageRoot, &errMsg)) {
        [fm removeItemAtPath:stageRoot error:nil];
        if (errOut) *errOut = errMsg;
        return NO;
    }

    NSString *payloadDir = [stageRoot stringByAppendingPathComponent:@"Payload"];
    chmod([payloadDir fileSystemRepresentation], 0777);
    NSString *appDir = nil;
    for (NSString *searchDir in @[payloadDir, stageRoot]) {
        for (NSString *e in [fm contentsOfDirectoryAtPath:searchDir error:nil]) {
            if ([e hasSuffix:@".app"]) {
                appDir = [searchDir stringByAppendingPathComponent:e];
                chmod([searchDir fileSystemRepresentation], 0777);
                break;
            }
        }
        if (appDir) break;
    }
    if (!appDir) {
        [fm removeItemAtPath:stageRoot error:nil];
        if (errOut) *errOut = @"IPA 内未找到 Payload/*.app 目录";
        return NO;
    }
    chmod([appDir fileSystemRepresentation], 0755);

    NSDictionary *info = [NSDictionary dictionaryWithContentsOfFile:[appDir stringByAppendingPathComponent:@"Info.plist"]];
    NSString *bundleID = info[@"CFBundleIdentifier"];
    NSString *execName = info[@"CFBundleExecutable"];
    if (!bundleID.length || !execName.length) {
        [fm removeItemAtPath:stageRoot error:nil];
        if (errOut) *errOut = @"Info.plist 缺少 CFBundleIdentifier 或 CFBundleExecutable";
        return NO;
    }

    NSString *execPath = [appDir stringByAppendingPathComponent:execName];
    chmod([execPath fileSystemRepresentation], 0755);
    ZizhanMachoReport rep;
    int mrc = inspect_or_sign_macho_file(execPath, bundleID, &rep, &errMsg);
    if (mrc != ZIZHAN_IPA_OK) {
        fprintf(stderr, "ZIZHAN_IPA_INSTALLER: REJECT ipa=%s bundle=%s rc=%d cryptid=%u reason=%s\n",
                [ipaPath UTF8String], [bundleID UTF8String], mrc, rep.cryptid,
                errMsg ? [errMsg UTF8String] : "invalid");
        fflush(stderr);
        [fm removeItemAtPath:stageRoot error:nil];
        if (errOut) *errOut = errMsg;
        return NO;
    }

    sign_nested_bundles_in_app(appDir, bundleID);

    Class wsCls = objc_lookUpClass("LSApplicationWorkspace");
    id ws = wsCls ? ((id(*)(Class, SEL))objc_msgSend)(wsCls, sel_registerName("defaultWorkspace")) : nil;
    NSURL *appURL = [NSURL fileURLWithPath:appDir isDirectory:YES];
    NSError *instErr = nil;
    NSDictionary *resInfo = nil;
    BOOL ok = NO;
    BOOL usedMiClient = NO;

    (void)dlopen("/System/Library/PrivateFrameworks/InstalledContentLibrary.framework/InstalledContentLibrary", RTLD_LAZY);
    (void)dlopen("/System/Library/PrivateFrameworks/MobileInstallation.framework/MobileInstallation", RTLD_LAZY);
    Class miClientCls = objc_lookUpClass("MIInstallerClient");
    Class miIdentCls  = objc_lookUpClass("MIAppIdentity");
    Class miOptsCls   = objc_lookUpClass("MIInstallOptions");
    SEL sMakeClient = sel_registerName("installerWithProgressBlock:");
    SEL sInitBid    = sel_registerName("initWithBundleID:");
    SEL sSecCode    = sel_registerName("supportsSecureCoding");
    SEL sInstUrl    = sel_registerName("installURL:identity:targetingDomain:withOptions:returningResultInfo:completion:");
    if (miClientCls && miIdentCls && miOptsCls &&
        [miClientCls respondsToSelector:sMakeClient] &&
        [miIdentCls respondsToSelector:sSecCode]) {
        id client = ((id(*)(Class, SEL, id))objc_msgSend)(miClientCls, sMakeClient, nil);
        id ident = [miIdentCls instancesRespondToSelector:sInitBid]
                   ? ((id(*)(id, SEL, id))objc_msgSend)([miIdentCls alloc], sInitBid, bundleID) : nil;
        id miOpts = [[miOptsCls alloc] init];
        SEL sTgt = sel_registerName("setInstallTargetType:");
        SEL sLs  = sel_registerName("setLsInstallType:");
        SEL sBid = sel_registerName("setBundleIdentifier:");
        SEL sUsr = sel_registerName("setUserInitiated:");
        SEL sLoc = sel_registerName("setAllowLocalProvisioned:");
        SEL sCln = sel_registerName("setPerformAPFSClone:");
        if ([miOpts respondsToSelector:sTgt]) ((void(*)(id, SEL, NSUInteger))objc_msgSend)(miOpts, sTgt, 1u);
        if ([miOpts respondsToSelector:sLs])  ((void(*)(id, SEL, NSUInteger))objc_msgSend)(miOpts, sLs, 1u);
        if ([miOpts respondsToSelector:sBid]) ((void(*)(id, SEL, id))objc_msgSend)(miOpts, sBid, bundleID);
        if ([miOpts respondsToSelector:sUsr]) ((void(*)(id, SEL, BOOL))objc_msgSend)(miOpts, sUsr, YES);
        if ([miOpts respondsToSelector:sLoc]) ((void(*)(id, SEL, BOOL))objc_msgSend)(miOpts, sLoc, YES);
        if ([miOpts respondsToSelector:sCln]) ((void(*)(id, SEL, BOOL))objc_msgSend)(miOpts, sCln, NO);
        if (client && ident && [client respondsToSelector:sInstUrl]) {
            __block BOOL miOk = NO;
            __block NSError *miErr = nil;
            __block id miRecs = nil;
            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            void (^comp)(BOOL, id, id, NSError *) = ^(BOOL success, id recs, id rinfo, NSError *err) {
                (void)rinfo;
                miOk = success;
                miRecs = recs;
                miErr = err;
                dispatch_semaphore_signal(sem);
            };
            ((void(*)(id, SEL, id, id, NSUInteger, id, BOOL, id))objc_msgSend)(
                client, sInstUrl, appURL, ident, 2u, miOpts, NO, comp);
            (void)dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 45 * NSEC_PER_SEC));
            ok = miOk;
            if (miOk) usedMiClient = YES;
            instErr = miErr;
            if ([miRecs isKindOfClass:[NSDictionary class]]) resInfo = miRecs;
            fprintf(stderr, "ZIZHAN_IPA_INSTALLER: MIInstallerClient bundle=%s ok=%d err=%s\n",
                    [bundleID UTF8String], (int)ok, instErr ? [[instErr localizedDescription] UTF8String] : "none");
            fflush(stderr);
        }
    }

    if (!ok && ws) {
        NSDictionary *opts = @{
            @"CFBundleIdentifier": bundleID,
            @"PackageType": @"Customer",
            @"AllowInstallLocalProvisioned": @YES
        };
        SEL sInst = sel_registerName("installApplication:withOptions:error:");
        if ([ws respondsToSelector:sInst]) {
            instErr = nil;
            ok = ((BOOL(*)(id, SEL, id, id, NSError **))objc_msgSend)(ws, sInst, appURL, opts, &instErr);
        }
    }

    if (ok && ws) {
        if (resInfo) {
            NSArray *arr = resInfo[@"InstalledAppInfoArray"];
            NSDictionary *first = ([arr isKindOfClass:[NSArray class]] && arr.count > 0) ? arr[0] : nil;
            NSURL *burl = [first isKindOfClass:[NSDictionary class]] ? first[@"BundleURL"] : nil;
            NSString *bpath = [first isKindOfClass:[NSDictionary class]] ? first[@"Path"] : nil;
            if (!burl && [bpath isKindOfClass:[NSString class]]) burl = [NSURL fileURLWithPath:bpath isDirectory:YES];
            SEL sRegApp = sel_registerName("registerApplication:");
            if (burl && [ws respondsToSelector:sRegApp]) {
                BOOL rok = ((BOOL(*)(id, SEL, id))objc_msgSend)(ws, sRegApp, burl);
                fprintf(stderr, "ZIZHAN_IPA_INSTALLER: LS registerApplication %s ok=%d\n", [[burl path] UTF8String], (int)rok);
            }
        }
        /* MIInstallerClient already registers the bundle with lsd & SpringBoard; only rebuild user DB on legacy fallback. */
        if (!usedMiClient) {
            SEL sRebuild = sel_registerName("_LSPrivateRebuildApplicationDatabasesForSystemApps:internal:user:");
            if ([ws respondsToSelector:sRebuild]) {
                BOOL rb = ((BOOL(*)(id, SEL, BOOL, BOOL, BOOL))objc_msgSend)(ws, sRebuild, NO, NO, YES);
                fprintf(stderr, "ZIZHAN_IPA_INSTALLER: LS rebuild user database ok=%d\n", (int)rb);
            }
        }
        fflush(stderr);
    }

    fprintf(stderr, "ZIZHAN_IPA_INSTALLER: INSTALL ipa=%s bundle=%s page_bytes=%u ok=%d err=%s\n",
            [ipaPath UTF8String], [bundleID UTF8String], rep.cd_page_bytes, (int)ok,
            instErr ? [[instErr localizedDescription] UTF8String] : "none");
    fflush(stderr);
    [fm removeItemAtPath:stageRoot error:nil];

    if (ok && bundleOut) *bundleOut = bundleID;
    if (!ok && errOut) *errOut = instErr ? [instErr localizedDescription] : @"MobileInstallation 安装返回失败";
    return ok;
}

static pid_t find_process_pid_by_name(const char *proc_name) {
    int (*p_listpids)(void *, int) = (int (*)(void *, int))dlsym(RTLD_DEFAULT, "proc_listallpids");
    int (*p_procname)(int, void *, uint32_t) = (int (*)(int, void *, uint32_t))dlsym(RTLD_DEFAULT, "proc_name");
    if (!p_listpids || !p_procname) return -1;
    pid_t pids[2048];
    int bytes = p_listpids(pids, (int)sizeof(pids));
    if (bytes <= 0) return -1;
    int count = bytes / (int)sizeof(pid_t);
    if (count > 2048) count = 2048;
    for (int i = 0; i < count; ++i) {
        if (pids[i] <= 1) continue;
        char name[64] = {0};
        if (p_procname(pids[i], name, sizeof(name)) > 0 && strcmp(name, proc_name) == 0) {
            return pids[i];
        }
    }
    return -1;
}

static void pin_zizhan_apps_to_springboard_page1(void) {
    NSArray<NSString *> *targets = @[@"com.zizhan.installer", @"com.zizhan.demoapp", @"com.zizhan.compatsuite"];
    NSArray<NSString *> *plistPaths = @[
        @"/private/var/mobile/Library/SpringBoard/IconState.plist",
        @"/private/var/mobile/Library/SpringBoard/DesiredIconState.plist"
    ];
    BOOL updatedAny = NO;
    for (NSString *path in plistPaths) {
        NSDictionary *orig = [NSDictionary dictionaryWithContentsOfFile:path];
        if (![orig isKindOfClass:[NSDictionary class]]) continue;
        NSMutableDictionary *root = [orig mutableCopy];
        NSArray *lists = root[@"iconLists"];
        if (![lists isKindOfClass:[NSArray class]] || lists.count == 0) continue;

        NSMutableArray *newLists = [NSMutableArray arrayWithCapacity:lists.count];
        for (id pg in lists) {
            if ([pg isKindOfClass:[NSArray class]]) {
                NSMutableArray *mpg = [pg mutableCopy];
                [mpg removeObjectsInArray:targets];
                [newLists addObject:mpg];
            } else {
                [newLists addObject:pg];
            }
        }
        NSMutableArray *page0 = newLists[0];
        /* Insert right after widgets on Page 1 so all 3 icons appear immediately on unlock */
        NSUInteger insIdx = 0;
        while (insIdx < page0.count && [page0[insIdx] isKindOfClass:[NSDictionary class]]) {
            ++insIdx;
        }
        for (NSInteger i = (NSInteger)targets.count - 1; i >= 0; --i) {
            [page0 insertObject:targets[(NSUInteger)i] atIndex:insIdx];
        }
        /* Move any overflow string icons beyond Page 1 capacity (17 items incl. 5 widgets) onto Page 2 */
        if (newLists.count < 2) [newLists addObject:[NSMutableArray array]];
        NSMutableArray *page1 = newLists[1];
        while (page0.count > 17 && [page0.lastObject isKindOfClass:[NSString class]]) {
            id moved = page0.lastObject;
            [page0 removeLastObject];
            [page1 insertObject:moved atIndex:0];
        }
        root[@"iconLists"] = newLists;
        NSData *bplist = [NSPropertyListSerialization dataWithPropertyList:root
                                                                    format:NSPropertyListBinaryFormat_v1_0
                                                                   options:0
                                                                     error:nil];
        if (bplist && [bplist writeToFile:path atomically:YES]) {
            chown([path fileSystemRepresentation], 501, 501);
            chmod([path fileSystemRepresentation], 0644);
            updatedAny = YES;
        }
    }
    fprintf(stderr, "ZIZHAN_IPA_INSTALLER: PIN_PAGE1 updated=%d\n", (int)updatedAny);
    fflush(stderr);
    if (updatedAny) {
        pid_t sbPid = find_process_pid_by_name("SpringBoard");
        fprintf(stderr, "ZIZHAN_IPA_INSTALLER: restarting SpringBoard pid=%d to reload Page 1 icons\n", (int)sbPid);
        fflush(stderr);
        if (sbPid > 1) kill(sbPid, SIGKILL);
    }
}

static long current_boot_epoch_sec(void) {
    struct timeval bt = {0, 0};
    size_t sz = sizeof(bt);
    if (sysctlbyname("kern.boottime", &bt, &sz, NULL, 0) == 0 && bt.tv_sec > 0) return (long)bt.tv_sec;
    return 1;
}

- (void)runBootstrapAllDesktopApps {
    const char *bootGuard = "/private/var/tmp/.zizhan_bootstrap_boottime";
    const long bootSec = current_boot_epoch_sec();
    NSString *prev = [NSString stringWithContentsOfFile:[NSString stringWithUTF8String:bootGuard]
                                               encoding:NSUTF8StringEncoding error:nil];
    if (prev && [prev longLongValue] == (long long)bootSec) {
        fprintf(stderr, "ZIZHAN_IPA_INSTALLER: BOOTSTRAP_SKIP already completed this boot (%ld)\n", bootSec);
        fflush(stderr);
        return;
    }
    [[NSString stringWithFormat:@"%ld\n", bootSec] writeToFile:[NSString stringWithUTF8String:bootGuard]
                                                    atomically:YES encoding:NSUTF8StringEncoding error:nil];

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSArray<NSString *> *suiteIpas = @[
            @"/pong/ipas/ZizhanInstaller-16k.ipa",
            @"/pong/ipas/ZizhanDemoApp-16k.ipa",
            @"/pong/ipas/ZizhanCompatSuite-16k.ipa"
        ];
        unsigned okCount = 0;
        for (NSString *ipa in suiteIpas) {
            NSString *bid = nil, *err = nil;
            BOOL ok = [self installIpaAtPath:ipa installedBundleOut:&bid errorMessage:&err];
            if (ok) ++okCount;
            fprintf(stderr, "ZIZHAN_IPA_INSTALLER: BOOTSTRAP_ITEM ipa=%s ok=%d bundle=%s err=%s\n",
                    [ipa UTF8String], (int)ok, bid ? [bid UTF8String] : "nil", err ? [err UTF8String] : "none");
            fflush(stderr);
        }
        fprintf(stderr, "ZIZHAN_IPA_INSTALLER: BOOTSTRAP_ALL ok=%u/%lu\n", okCount, (unsigned long)suiteIpas.count);
        fflush(stderr);
        sleep(2);
        pin_zizhan_apps_to_springboard_page1();
    });
}

- (void)runAutoTestWithTarget:(NSString *)targetIpa {
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSString *encIpa = @"/pong/ipas/FairPlayEncryptedSample.ipa";
        if ([[NSFileManager defaultManager] fileExistsAtPath:encIpa]) {
            NSString *rejErr = nil;
            BOOL encOk = [self installIpaAtPath:encIpa installedBundleOut:NULL errorMessage:&rejErr];
            fprintf(stderr, "ZIZHAN_IPA_INSTALLER: AUTOTEST_ENCRYPTED_GUARD ok=%d expected_reject=%d msg=%s\n",
                    (int)encOk, (int)(!encOk), rejErr ? [rejErr UTF8String] : "none");
            fflush(stderr);
        }
        NSString *bid = nil, *err = nil;
        BOOL ok = [self installIpaAtPath:targetIpa installedBundleOut:&bid errorMessage:&err];
        fprintf(stderr, "ZIZHAN_IPA_INSTALLER: AUTOTEST_RESULT ok=%d bundle=%s err=%s\n",
                (int)ok, bid ? [bid UTF8String] : "nil", err ? [err UTF8String] : "none");
        fflush(stderr);

        dispatch_async(dispatch_get_main_queue(), ^{
            [self reloadAll];
            self.statusMessage = ok
                ? [NSString stringWithFormat:@"自动验证成功：已安装 %@ (16K 页签名脱壳 IPA)，即将启动...", bid]
                : [NSString stringWithFormat:@"自动验证失败：%@", err];
            [self.tableView reloadData];

            if (ok && bid.length) {
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
                    BOOL launched = launch_bundle_id(bid);
                    fprintf(stderr, "ZIZHAN_IPA_INSTALLER: AUTOTEST_LAUNCH bundle=%s launched=%d\n",
                            [bid UTF8String], (int)launched);
                    fflush(stderr);
                });
            }
        });
    });
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
    (void)tableView;
    return 2;
}

- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section {
    (void)tableView;
    return section == 0
        ? [NSString stringWithFormat:@"待安装 IPA 包 — %@", self.statusMessage]
        : @"已安装第三方应用 (点击管理：打开 / 查看沙盒信息 / 卸载)";
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    (void)tableView;
    if (section == 0) return MAX(1, (NSInteger)self.ipas.count);
    return MAX(1, (NSInteger)self.installedApps.count);
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:@"c"];
    if (!cell) cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:@"c"];
    if (indexPath.section == 0) {
        if (self.ipas.count == 0) {
            cell.textLabel.text = @"暂无 .ipa 文件";
            cell.detailTextLabel.text = @"请通过浏览器访问上方 http://<IP>:8088 流式投递脱壳 .ipa，或放入 /var/mobile/Documents/IPAs";
            cell.accessoryType = UITableViewCellAccessoryNone;
        } else {
            ZizhanIpaItem *it = self.ipas[(NSUInteger)indexPath.row];
            cell.textLabel.text = it.fileName;
            double mb = it.fileBytes / 1048576.0;
            cell.detailTextLabel.text = (mb >= 1.0)
                ? [NSString stringWithFormat:@"%@ (%.2f MB) — 点击一键校验并安装", it.path, mb]
                : [NSString stringWithFormat:@"%@ (%.1f KB) — 点击一键校验并安装", it.path, it.fileBytes / 1024.0];
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
        }
    } else {
        if (self.installedApps.count == 0) {
            cell.textLabel.text = @"尚未安装第三方应用";
            cell.detailTextLabel.text = @"安装成功后将显示在此处";
            cell.accessoryType = UITableViewCellAccessoryNone;
        } else {
            NSDictionary *app = self.installedApps[(NSUInteger)indexPath.row];
            cell.textLabel.text = app[@"name"];
            cell.detailTextLabel.text = [NSString stringWithFormat:@"%@  |  %@", app[@"bundleID"], app[@"bundlePath"] ?: @""];
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
        }
    }
    return cell;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
    if (indexPath.section == 0 && indexPath.row < (NSInteger)self.ipas.count) {
        ZizhanIpaItem *it = self.ipas[(NSUInteger)indexPath.row];
        NSString *bid = nil, *err = nil;
        BOOL ok = [self installIpaAtPath:it.path installedBundleOut:&bid errorMessage:&err];
        [self reloadAll];
        UIAlertController *ac = [UIAlertController
            alertControllerWithTitle:(ok ? @"安装成功" : @"无法安装")
                             message:(ok ? [NSString stringWithFormat:@"已安装 %@", bid] : err)
                      preferredStyle:UIAlertControllerStyleAlert];
        if (ok && bid.length) {
            [ac addAction:[UIAlertAction actionWithTitle:@"立即打开" style:UIAlertActionStyleDefault handler:^(UIAlertAction *a) {
                (void)a;
                (void)launch_bundle_id(bid);
            }]];
        }
        [ac addAction:[UIAlertAction actionWithTitle:@"好" style:UIAlertActionStyleCancel handler:nil]];
        [self presentViewController:ac animated:YES completion:nil];
    } else if (indexPath.section == 1 && indexPath.row < (NSInteger)self.installedApps.count) {
        NSDictionary *app = self.installedApps[(NSUInteger)indexPath.row];
        NSString *bid = app[@"bundleID"];
        BOOL isSelf = [bid isEqualToString:@"com.zizhan.installer"];
        UIAlertController *ac = [UIAlertController
            alertControllerWithTitle:app[@"name"]
                             message:[NSString stringWithFormat:@"Bundle ID: %@\nBundle: %@\nData: %@",
                                      bid, app[@"bundlePath"] ?: @"-", app[@"dataPath"] ?: @"-"]
                      preferredStyle:UIAlertControllerStyleAlert];
        if (!isSelf) {
            [ac addAction:[UIAlertAction actionWithTitle:@"立即打开" style:UIAlertActionStyleDefault handler:^(UIAlertAction *a) {
                (void)a;
                (void)launch_bundle_id(bid);
            }]];
            [ac addAction:[UIAlertAction actionWithTitle:@"卸载应用" style:UIAlertActionStyleDestructive handler:^(UIAlertAction *a) {
                (void)a;
                [self uninstallBundleID:bid];
            }]];
        }
        [ac addAction:[UIAlertAction actionWithTitle:(isSelf ? @"好 (内置安装器受保护)" : @"取消") style:UIAlertActionStyleCancel handler:nil]];
        [self presentViewController:ac animated:YES completion:nil];
    }
}

- (BOOL)tableView:(UITableView *)tableView canEditRowAtIndexPath:(NSIndexPath *)indexPath {
    (void)tableView;
    if (indexPath.section != 1 || indexPath.row >= (NSInteger)self.installedApps.count) return NO;
    NSString *bid = self.installedApps[(NSUInteger)indexPath.row][@"bundleID"];
    return ![bid isEqualToString:@"com.zizhan.installer"];
}

- (void)tableView:(UITableView *)tableView commitEditingStyle:(UITableViewCellEditingStyle)editingStyle forRowAtIndexPath:(NSIndexPath *)indexPath {
    (void)tableView;
    if (editingStyle == UITableViewCellEditingStyleDelete && indexPath.section == 1 && indexPath.row < (NSInteger)self.installedApps.count) {
        NSString *bid = self.installedApps[(NSUInteger)indexPath.row][@"bundleID"];
        [self uninstallBundleID:bid];
    }
}

@end

@interface ZizhanInstallerAppDelegate : UIResponder <UIApplicationDelegate>
@property (nonatomic, strong) UIWindow *window;
@end

@implementation ZizhanInstallerAppDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    (void)application; (void)launchOptions;
    self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
    UINavigationController *nav = [[UINavigationController alloc]
        initWithRootViewController:[[ZizhanInstallerController alloc] initWithStyle:UITableViewStyleInsetGrouped]];
    self.window.rootViewController = nav;
    [self.window makeKeyAndVisible];
    return YES;
}
@end

int main(int argc, char *argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([ZizhanInstallerAppDelegate class]));
    }
}
