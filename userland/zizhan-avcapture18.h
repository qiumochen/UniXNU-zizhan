/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Track 3B (CAM-02): System-level iPadOS 18 (22H374) AVCaptureSession runtime bridge.
 * Connects AVCaptureDevice / AVCaptureDeviceDiscoverySession / AVCaptureDeviceInput /
 * AVCaptureSession / AVCaptureVideoPreviewLayer / AVCaptureVideoDataOutput /
 * AVCapturePhotoOutput / AVCapturePhotoSettings / AVCaptureMetadataOutput (QR barcode scanning
 * via CIDetectorTypeQRCode) directly to the kernel camera frame/lease sysctls
 * (kern.zizhan_cam_ctrl & kern.zizhan_cam_frame).
 *
 * Every bridged AVFCapture object uses clean NSObject / CALayer init+dealloc implementations so
 * uninitialized AVFCapture _internal pointers and absent cameracaptured/FigCapture XPC endpoints
 * are never dereferenced. Works across system apps (Camera, FaceTime, MobileSafari) and
 * containerized 4K/16K third-party .ipa apps. Opt-out via zizhan_avcapture=0. */
#ifndef ZIZHAN_AVCAPTURE18_H
#define ZIZHAN_AVCAPTURE18_H

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <time.h>
#include "zizhan-cam-proto.h"

/* Lazy dynamic bindings for ImageIO / CoreVideo / CoreMedia so system daemons loading
 * libpong-ui18.dylib (installd, lsd, pkd, usermanagerd) incur zero extra dylib links. */
typedef struct __CVBuffer *ZCamCVPixelBufferRef;
typedef const struct opaqueCMFormatDescription *ZCamCMVideoFormatDescRef;
typedef struct opaqueCMSampleBuffer *ZCamCMSampleBufferRef;
typedef struct { int64_t value; int32_t timescale; uint32_t flags; int64_t epoch; } ZCamCMTime;
typedef struct { ZCamCMTime duration; ZCamCMTime presentationTimeStamp; ZCamCMTime decodeTimeStamp; } ZCamCMSampleTimingInfo;

static CFTypeRef (*p_CGImageDestinationCreateWithData)(CFMutableDataRef, CFStringRef, size_t, CFDictionaryRef);
static void (*p_CGImageDestinationAddImage)(CFTypeRef, CGImageRef, CFDictionaryRef);
static bool (*p_CGImageDestinationFinalize)(CFTypeRef);
static int (*p_CVPixelBufferCreate)(CFAllocatorRef, size_t, size_t, uint32_t, CFDictionaryRef, ZCamCVPixelBufferRef *);
static int (*p_CVPixelBufferLockBaseAddress)(ZCamCVPixelBufferRef, uint64_t);
static int (*p_CVPixelBufferUnlockBaseAddress)(ZCamCVPixelBufferRef, uint64_t);
static void *(*p_CVPixelBufferGetBaseAddress)(ZCamCVPixelBufferRef);
static size_t (*p_CVPixelBufferGetBytesPerRow)(ZCamCVPixelBufferRef);
static int (*p_CMVideoFormatDescriptionCreateForImageBuffer)(CFAllocatorRef, ZCamCVPixelBufferRef, ZCamCMVideoFormatDescRef *);
static int (*p_CMSampleBufferCreateReadyWithImageBuffer)(CFAllocatorRef, ZCamCVPixelBufferRef, ZCamCMVideoFormatDescRef, const ZCamCMSampleTimingInfo *, ZCamCMSampleBufferRef *);

static void zcam_resolve_media_syms(void) {
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        void *iio = dlopen("/System/Library/Frameworks/ImageIO.framework/ImageIO", RTLD_LAZY);
        if (iio) {
            p_CGImageDestinationCreateWithData = dlsym(iio, "CGImageDestinationCreateWithData");
            p_CGImageDestinationAddImage       = dlsym(iio, "CGImageDestinationAddImage");
            p_CGImageDestinationFinalize       = dlsym(iio, "CGImageDestinationFinalize");
        }
        void *cv = dlopen("/System/Library/Frameworks/CoreVideo.framework/CoreVideo", RTLD_LAZY);
        if (cv) {
            p_CVPixelBufferCreate            = dlsym(cv, "CVPixelBufferCreate");
            p_CVPixelBufferLockBaseAddress   = dlsym(cv, "CVPixelBufferLockBaseAddress");
            p_CVPixelBufferUnlockBaseAddress = dlsym(cv, "CVPixelBufferUnlockBaseAddress");
            p_CVPixelBufferGetBaseAddress    = dlsym(cv, "CVPixelBufferGetBaseAddress");
            p_CVPixelBufferGetBytesPerRow    = dlsym(cv, "CVPixelBufferGetBytesPerRow");
        }
        void *cm = dlopen("/System/Library/Frameworks/CoreMedia.framework/CoreMedia", RTLD_LAZY);
        if (cm) {
            p_CMVideoFormatDescriptionCreateForImageBuffer = dlsym(cm, "CMVideoFormatDescriptionCreateForImageBuffer");
            p_CMSampleBufferCreateReadyWithImageBuffer     = dlsym(cm, "CMSampleBufferCreateReadyWithImageBuffer");
        }
    });
}

static bool avcap_policy_installed;
static bool ci_sw_policy_installed;
static Class gZizhanCamDeviceCls;
static Class gZizhanCamInputCls;
static Class gZizhanCamPhotoCls;
static Class gZizhanCamPhotoSettingsCls;
static Class gZizhanCamQrMetaCls;
static Class gZizhanPreviewLayerCls;
static Class gOrigPrevCls;
static id gBackDeviceSingleton;
static id gUltraDeviceSingleton;
static id gFrontDeviceSingleton;
static IMP gOrigCiInitOpts;
static int (*p_CIMetalDeviceIsSupported)(void);
static void (*p_CIObjectUnref)(void *);

static char kKeyDevPos;
static char kKeyInputDev;
static char kKeySessionState;
static char kKeyPreviewSession;
static char kKeyPreviewGravity;
static char kKeyVoutState;
static char kKeyVoutSettings;
static char kKeyMoutState;
static char kKeyPhotoJpeg;
static char kKeyPhotoCgImg;
static char kKeyQrString;
static char kKeyQrBounds;

typedef struct {
    int running;
    int position; /* 1 = back, 2 = front */
    uint32_t last_seq;
    uint32_t frames_delivered;
    uint64_t last_lease_ns;
    dispatch_queue_t queue;
    dispatch_source_t timer;
    CFMutableArrayRef inputs;
    CFMutableArrayRef outputs;
    CFMutableArrayRef preview_layers;
    uint8_t *frame_buf;
    id ci_context;
    id qr_detector;
} ZizhanSessionCtx;

static IMP gNsObjectInitImp;
static IMP gNsObjectDeallocImp;
static IMP gNsObjectDescImp;
static IMP gNsObjectIsKindImp;
static IMP gCaLayerInitImp;
static IMP gCaLayerDeallocImp;

static IMP gOrigAuthStatus;
static IMP gOrigReqAccess;
static IMP gOrigDefaultDev3;
static IMP gOrigDefaultDev1;
static IMP gOrigDevicesMedia;
static IMP gOrigDevicesAll;
static IMP gOrigDevWithUid;
static IMP gOrigDiscDevices;
static IMP gOrigInputClsCreate;
static IMP gOrigInputInit;

static void zizhan_ci_sw_policy(void);

static id zcam_create_sw_cicontext(id self_alloc, id opts) {
    Class ciCls = objc_lookUpClass("CIContext");
    Class nsoCls = objc_lookUpClass("NSObject");
    IMP initImp = gNsObjectInitImp ? gNsObjectInitImp
                  : (nsoCls ? class_getMethodImplementation(nsoCls, sel_registerName("init")) : NULL);
    if (!ciCls || !initImp) return nil;
    id obj = self_alloc ? self_alloc : ((id(*)(Class, SEL))objc_msgSend)(ciCls, sel_registerName("alloc"));
    obj = ((id(*)(id, SEL))initImp)(obj, sel_registerName("init"));
    if (!obj) return nil;
    void *rep = ((void *(*)(Class, SEL, id))objc_msgSend)(
        ciCls, sel_registerName("internalSWContextWithOptions:"), opts);
    if (!rep) {
        ((void(*)(id, SEL))objc_msgSend)(obj, sel_registerName("release"));
        return nil;
    }
    obj = ((id(*)(id, SEL, void *))objc_msgSend)(
        obj, sel_registerName("_initWithInternalRepresentation:"), rep);
    if (p_CIObjectUnref) p_CIObjectUnref(rep);
    return obj;
}

static id zcam_cicontext_init_opts(id self, SEL _cmd, id opts) {
    if (p_CIMetalDeviceIsSupported && p_CIMetalDeviceIsSupported()) {
        return gOrigCiInitOpts ? ((id(*)(id, SEL, id))gOrigCiInitOpts)(self, _cmd, opts) : nil;
    }
    return zcam_create_sw_cicontext(self, opts);
}

static void zizhan_ci_sw_policy(void) {
    if (ci_sw_policy_installed || boot_option("zizhan_avcapture=0")) return;
    Class ciCls = objc_lookUpClass("CIContext");
    if (!ciCls) return;
    void *ciHdl = dlopen("/System/Library/Frameworks/CoreImage.framework/CoreImage", RTLD_LAZY | RTLD_NOLOAD);
    if (!ciHdl) ciHdl = dlopen("/System/Library/Frameworks/CoreImage.framework/CoreImage", RTLD_LAZY);
    if (ciHdl) {
        p_CIMetalDeviceIsSupported = (int(*)(void))dlsym(ciHdl, "_CIMetalDeviceIsSupported");
        p_CIObjectUnref = (void(*)(void *))dlsym(ciHdl, "_ZNK2CI6Object5unrefEv");
    }
    Method m = class_getInstanceMethod(ciCls, sel_registerName("initWithOptions:"));
    if (m) {
        gOrigCiInitOpts = method_setImplementation(m, (IMP)zcam_cicontext_init_opts);
        ci_sw_policy_installed = true;
    }
}

static id zizhan_nsstr(const char *s) {
    Class cls = objc_lookUpClass("NSString");
    return (cls && s) ? ((id(*)(Class, SEL, const char *))objc_msgSend)(
        cls, sel_registerName("stringWithUTF8String:"), s) : nil;
}

static bool zizhan_is_video_media(id mediaType) {
    if (!mediaType) return true;
    const char *u = ((const char *(*)(id, SEL))objc_msgSend)(mediaType, sel_registerName("UTF8String"));
    return u && (!strcmp(u, "vide") || !strcmp(u, "muxx"));
}

static int zizhan_cam_ctl_write(int cmd) {
    return sysctlbyname("kern.zizhan_cam_ctrl", NULL, NULL, &cmd, sizeof(cmd));
}

static int zizhan_cam_ctl_query(void) {
    int cmd = ZIZHAN_CAM_CMD_QUERY;
    int out = 0;
    size_t out_sz = sizeof(out);
    if (sysctlbyname("kern.zizhan_cam_ctrl", &out, &out_sz, &cmd, sizeof(cmd)) == 0) return out;
    return 0;
}

static id zcam_nsobj_init(id self, SEL _cmd) {
    return gNsObjectInitImp ? ((id(*)(id, SEL))gNsObjectInitImp)(self, _cmd) : self;
}

static void zcam_nsobj_dealloc(id self, SEL _cmd) {
    if (gNsObjectDeallocImp) ((void(*)(id, SEL))gNsObjectDeallocImp)(self, _cmd);
}

/* --- Virtual AVCaptureDevice subclass --- */
static id zcam_dev_unique_id(id self, SEL _cmd) {
    (void)_cmd;
    long pos = (long)(intptr_t)objc_getAssociatedObject(self, &kKeyDevPos);
    if (pos == 2) return zizhan_nsstr("com.zizhan.camera.front");
    if (pos == 4) return zizhan_nsstr("com.zizhan.camera.ultrawide");
    return zizhan_nsstr("com.zizhan.camera.back");
}
static id zcam_dev_model_id(id self, SEL _cmd) { (void)self; (void)_cmd; return zizhan_nsstr("Xiaomi MIX Fold 2 Camera"); }
static id zcam_dev_manufacturer(id self, SEL _cmd) { (void)self; (void)_cmd; return zizhan_nsstr("Xiaomi / UniXNU"); }
static id zcam_dev_localized_name(id self, SEL _cmd) {
    (void)_cmd;
    long pos = (long)(intptr_t)objc_getAssociatedObject(self, &kKeyDevPos);
    if (pos == 2) return zizhan_nsstr("Front Camera (IMX596)");
    if (pos == 4) return zizhan_nsstr("Back Ultra-Wide 0.6x (OV13B10)");
    return zizhan_nsstr("Back Camera 1x (IMX766 50MP)");
}
static long zcam_dev_position(id self, SEL _cmd) {
    (void)_cmd;
    long pos = (long)(intptr_t)objc_getAssociatedObject(self, &kKeyDevPos);
    return pos == 2 ? 2 : 1;
}
static id zcam_dev_type(id self, SEL _cmd) {
    (void)_cmd;
    long pos = (long)(intptr_t)objc_getAssociatedObject(self, &kKeyDevPos);
    if (pos == 4) return zizhan_nsstr("AVCaptureDeviceTypeBuiltInUltraWideCamera");
    return zizhan_nsstr("AVCaptureDeviceTypeBuiltInWideAngleCamera");
}
static BOOL zcam_dev_yes(id self, SEL _cmd) { (void)self; (void)_cmd; return YES; }
static BOOL zcam_dev_no(id self, SEL _cmd) { (void)self; (void)_cmd; return NO; }
static BOOL zcam_dev_no_arg(id self, SEL _cmd, long mode) { (void)self; (void)_cmd; (void)mode; return NO; }
static BOOL zcam_dev_focus_sup(id self, SEL _cmd, long mode) {
    (void)_cmd;
    long pos = (long)(intptr_t)objc_getAssociatedObject(self, &kKeyDevPos);
    return (pos == 1 && mode >= 0 && mode <= 2) ? YES : (mode == 0 ? YES : NO);
}
static BOOL zcam_dev_focus_poi_sup(id self, SEL _cmd) {
    (void)_cmd;
    long pos = (long)(intptr_t)objc_getAssociatedObject(self, &kKeyDevPos);
    return pos == 1 ? YES : NO;
}
static long zcam_dev_focus_mode(id self, SEL _cmd) {
    (void)_cmd;
    long pos = (long)(intptr_t)objc_getAssociatedObject(self, &kKeyDevPos);
    return pos == 1 ? 2 : 0;
}
static void zcam_dev_set_focus_mode(id self, SEL _cmd, long mode) {
    (void)_cmd;
    long pos = (long)(intptr_t)objc_getAssociatedObject(self, &kKeyDevPos);
    if (pos == 1 && mode != 0) (void)zizhan_cam_ctl_write(ZIZHAN_CAM_CMD_AF);
}
static void zcam_dev_set_focus_poi(id self, SEL _cmd, CGPoint pt) {
    (void)_cmd; (void)pt;
    long pos = (long)(intptr_t)objc_getAssociatedObject(self, &kKeyDevPos);
    if (pos == 1) (void)zizhan_cam_ctl_write(ZIZHAN_CAM_CMD_AF);
}
static BOOL zcam_dev_has_media(id self, SEL _cmd, id media) { (void)self; (void)_cmd; return zizhan_is_video_media(media); }
static BOOL zcam_dev_lock_cfg(id self, SEL _cmd, id *err) { (void)self; (void)_cmd; if (err) *err = nil; return YES; }
static void zcam_dev_noop(id self, SEL _cmd) { (void)self; (void)_cmd; }
static void zcam_dev_noop_long(id self, SEL _cmd, long v) { (void)self; (void)_cmd; (void)v; }
static void zcam_dev_noop_bool(id self, SEL _cmd, BOOL b) { (void)self; (void)_cmd; (void)b; }
static id zcam_dev_empty_array(id self, SEL _cmd) {
    (void)self; (void)_cmd;
    Class a = objc_lookUpClass("NSArray");
    return a ? ((id(*)(Class, SEL))objc_msgSend)(a, sel_registerName("array")) : nil;
}
static double zcam_dev_zoom_one(id self, SEL _cmd) { (void)self; (void)_cmd; return 1.0; }

static void zizhan_ensure_devices(void) {
    if (gBackDeviceSingleton && gUltraDeviceSingleton && gFrontDeviceSingleton) return;
    if (!gZizhanCamDeviceCls || !gNsObjectInitImp) return;
    if (!gBackDeviceSingleton) {
        id b = ((id(*)(Class, SEL))objc_msgSend)(gZizhanCamDeviceCls, sel_registerName("alloc"));
        b = ((id(*)(id, SEL))gNsObjectInitImp)(b, sel_registerName("init"));
        objc_setAssociatedObject(b, &kKeyDevPos, (id)(intptr_t)1, OBJC_ASSOCIATION_ASSIGN);
        gBackDeviceSingleton = b;
    }
    if (!gUltraDeviceSingleton) {
        id u = ((id(*)(Class, SEL))objc_msgSend)(gZizhanCamDeviceCls, sel_registerName("alloc"));
        u = ((id(*)(id, SEL))gNsObjectInitImp)(u, sel_registerName("init"));
        objc_setAssociatedObject(u, &kKeyDevPos, (id)(intptr_t)4, OBJC_ASSOCIATION_ASSIGN);
        gUltraDeviceSingleton = u;
    }
    if (!gFrontDeviceSingleton) {
        id f = ((id(*)(Class, SEL))objc_msgSend)(gZizhanCamDeviceCls, sel_registerName("alloc"));
        f = ((id(*)(id, SEL))gNsObjectInitImp)(f, sel_registerName("init"));
        objc_setAssociatedObject(f, &kKeyDevPos, (id)(intptr_t)2, OBJC_ASSOCIATION_ASSIGN);
        gFrontDeviceSingleton = f;
    }
}

static long zcam_cls_auth_status(id cls, SEL _cmd, id media) {
    if (zizhan_is_video_media(media)) return 3; /* AVAuthorizationStatusAuthorized */
    return gOrigAuthStatus ? ((long(*)(id, SEL, id))gOrigAuthStatus)(cls, _cmd, media) : 3;
}

static void zcam_cls_req_access(id cls, SEL _cmd, id media, void (^handler)(BOOL)) {
    if (zizhan_is_video_media(media)) {
        if (handler) handler(YES);
        return;
    }
    if (gOrigReqAccess) ((void(*)(id, SEL, id, void (^)(BOOL)))gOrigReqAccess)(cls, _cmd, media, handler);
    else if (handler) handler(YES);
}

static id zcam_cls_default_dev3(id cls, SEL _cmd, id dtype, id media, long pos) {
    if (zizhan_is_video_media(media)) {
        zizhan_ensure_devices();
        const char *dt = dtype ? ((const char *(*)(id, SEL))objc_msgSend)(dtype, sel_registerName("UTF8String")) : NULL;
        if (dt && strstr(dt, "UltraWide")) return gUltraDeviceSingleton;
        return (pos == 2) ? gFrontDeviceSingleton : gBackDeviceSingleton;
    }
    return gOrigDefaultDev3 ? ((id(*)(id, SEL, id, id, long))gOrigDefaultDev3)(cls, _cmd, dtype, media, pos) : nil;
}

static id zcam_cls_default_dev1(id cls, SEL _cmd, id media) {
    if (zizhan_is_video_media(media)) {
        zizhan_ensure_devices();
        return gBackDeviceSingleton;
    }
    return gOrigDefaultDev1 ? ((id(*)(id, SEL, id))gOrigDefaultDev1)(cls, _cmd, media) : nil;
}

static id zcam_cls_devices_media(id cls, SEL _cmd, id media) {
    if (zizhan_is_video_media(media)) {
        zizhan_ensure_devices();
        Class arr = objc_lookUpClass("NSArray");
        const id objs[3] = {gBackDeviceSingleton, gUltraDeviceSingleton, gFrontDeviceSingleton};
        return arr ? ((id(*)(Class, SEL, const id *, unsigned long))objc_msgSend)(
            arr, sel_registerName("arrayWithObjects:count:"), objs, 3ul) : nil;
    }
    return gOrigDevicesMedia ? ((id(*)(id, SEL, id))gOrigDevicesMedia)(cls, _cmd, media) : nil;
}

static id zcam_cls_devices_all(id cls, SEL _cmd) {
    (void)cls; (void)_cmd;
    zizhan_ensure_devices();
    Class arr = objc_lookUpClass("NSArray");
    const id objs[3] = {gBackDeviceSingleton, gUltraDeviceSingleton, gFrontDeviceSingleton};
    return arr ? ((id(*)(Class, SEL, const id *, unsigned long))objc_msgSend)(
        arr, sel_registerName("arrayWithObjects:count:"), objs, 3ul) : nil;
}

static id zcam_cls_dev_with_uid(id cls, SEL _cmd, id uid) {
    if (uid) {
        const char *u = ((const char *(*)(id, SEL))objc_msgSend)(uid, sel_registerName("UTF8String"));
        if (u && !strcmp(u, "com.zizhan.camera.front"))     { zizhan_ensure_devices(); return gFrontDeviceSingleton; }
        if (u && !strcmp(u, "com.zizhan.camera.ultrawide")) { zizhan_ensure_devices(); return gUltraDeviceSingleton; }
        if (u && !strcmp(u, "com.zizhan.camera.back"))      { zizhan_ensure_devices(); return gBackDeviceSingleton; }
    }
    return gOrigDevWithUid ? ((id(*)(id, SEL, id))gOrigDevWithUid)(cls, _cmd, uid) : nil;
}

static id zcam_disc_devices(id self, SEL _cmd) {
    id orig = gOrigDiscDevices ? ((id(*)(id, SEL))gOrigDiscDevices)(self, _cmd) : nil;
    if (orig && ((unsigned long(*)(id, SEL))objc_msgSend)(orig, sel_registerName("count")) > 0) return orig;
    zizhan_ensure_devices();
    Class arr = objc_lookUpClass("NSArray");
    const id objs[3] = {gBackDeviceSingleton, gUltraDeviceSingleton, gFrontDeviceSingleton};
    return arr ? ((id(*)(Class, SEL, const id *, unsigned long))objc_msgSend)(
        arr, sel_registerName("arrayWithObjects:count:"), objs, 3ul) : orig;
}

/* --- Virtual AVCaptureDeviceInput subclass --- */
static id zcam_input_device(id self, SEL _cmd) { (void)_cmd; return objc_getAssociatedObject(self, &kKeyInputDev); }
static id zcam_input_nil(id self, SEL _cmd) { (void)self; (void)_cmd; return nil; }

static id zcam_make_input(id dev) {
    if (!gZizhanCamInputCls || !gNsObjectInitImp) return nil;
    id inp = ((id(*)(Class, SEL))objc_msgSend)(gZizhanCamInputCls, sel_registerName("alloc"));
    inp = ((id(*)(id, SEL))gNsObjectInitImp)(inp, sel_registerName("init"));
    objc_setAssociatedObject(inp, &kKeyInputDev, dev, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    return inp;
}

static id zcam_input_cls_create(id cls, SEL _cmd, id dev, id *err) {
    if (dev && object_getClass(dev) == gZizhanCamDeviceCls) {
        if (err) *err = nil;
        id inp = zcam_make_input(dev);
        return inp ? ((id(*)(id, SEL))objc_msgSend)(inp, sel_registerName("autorelease")) : nil;
    }
    return gOrigInputClsCreate ? ((id(*)(id, SEL, id, id *))gOrigInputClsCreate)(cls, _cmd, dev, err) : nil;
}

static id zcam_input_init(id self, SEL _cmd, id dev, id *err) {
    if (dev && object_getClass(dev) == gZizhanCamDeviceCls && gZizhanCamInputCls && gNsObjectInitImp) {
        if (err) *err = nil;
        object_setClass(self, gZizhanCamInputCls);
        self = ((id(*)(id, SEL))gNsObjectInitImp)(self, sel_registerName("init"));
        objc_setAssociatedObject(self, &kKeyInputDev, dev, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        return self;
    }
    return gOrigInputInit ? ((id(*)(id, SEL, id, id *))gOrigInputInit)(self, _cmd, dev, err) : nil;
}

/* --- Virtual AVCapturePhoto, AVCapturePhotoSettings & AVMetadataMachineReadableCodeObject --- */
static id zcam_photo_file_data(id self, SEL _cmd) { (void)_cmd; return objc_getAssociatedObject(self, &kKeyPhotoJpeg); }
static CGImageRef zcam_photo_cgimage(id self, SEL _cmd) {
    (void)_cmd;
    return (CGImageRef)objc_getAssociatedObject(self, &kKeyPhotoCgImg);
}

static id zcam_ps_make(id cls, SEL _cmd) {
    (void)cls; (void)_cmd;
    if (!gZizhanCamPhotoSettingsCls || !gNsObjectInitImp) return nil;
    id s = ((id(*)(Class, SEL))objc_msgSend)(gZizhanCamPhotoSettingsCls, sel_registerName("alloc"));
    s = ((id(*)(id, SEL))gNsObjectInitImp)(s, sel_registerName("init"));
    return s ? ((id(*)(id, SEL))objc_msgSend)(s, sel_registerName("autorelease")) : nil;
}
static id zcam_ps_make_arg(id cls, SEL _cmd, id fmt) { (void)fmt; return zcam_ps_make(cls, _cmd); }
static long long zcam_ps_uid(id self, SEL _cmd) {
    (void)self; (void)_cmd;
    static volatile int64_t gUid = 100;
    return __atomic_add_fetch(&gUid, 1, __ATOMIC_RELAXED);
}

static id zcam_qr_type(id self, SEL _cmd) { (void)self; (void)_cmd; return zizhan_nsstr("org.iso.QRCode"); }
static id zcam_qr_string(id self, SEL _cmd) { (void)_cmd; return objc_getAssociatedObject(self, &kKeyQrString); }
static CGRect zcam_qr_bounds(id self, SEL _cmd) {
    (void)_cmd;
    id val = objc_getAssociatedObject(self, &kKeyQrBounds);
    if (val) return ((CGRect(*)(id, SEL))objc_msgSend)(val, sel_registerName("CGRectValue"));
    return CGRectMake(0.65, 0.60, 0.30, 0.35);
}

/* --- Session Context & 20 fps Frame Pump --- */
static ZizhanSessionCtx *zcam_get_sess_ctx(id session, bool create) {
    id box = objc_getAssociatedObject(session, &kKeySessionState);
    if (box) {
        return (ZizhanSessionCtx *)((void *(*)(id, SEL))objc_msgSend)(box, sel_registerName("pointerValue"));
    }
    if (!create) return NULL;
    ZizhanSessionCtx *ctx = (ZizhanSessionCtx *)calloc(1, sizeof(ZizhanSessionCtx));
    if (!ctx) return NULL;
    ctx->position = 1;
    ctx->queue = dispatch_queue_create("com.zizhan.avcapture.pump", DISPATCH_QUEUE_SERIAL);
    ctx->inputs = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    ctx->outputs = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    ctx->preview_layers = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    ctx->frame_buf = (uint8_t *)malloc(ZIZHAN_CAM_TOTAL_BYTES);
    Class valCls = objc_lookUpClass("NSValue");
    id nsv = valCls ? ((id(*)(Class, SEL, const void *))objc_msgSend)(
        valCls, sel_registerName("valueWithPointer:"), ctx) : nil;
    if (nsv) objc_setAssociatedObject(session, &kKeySessionState, nsv, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    return ctx;
}

typedef struct { id delegate; dispatch_queue_t queue; } ZizhanOutDelegate;
static ZizhanOutDelegate *zcam_out_del(id outObj, const void *key, bool create) {
    id box = objc_getAssociatedObject(outObj, key);
    if (box) return (ZizhanOutDelegate *)((void *(*)(id, SEL))objc_msgSend)(box, sel_registerName("pointerValue"));
    if (!create) return NULL;
    ZizhanOutDelegate *d = (ZizhanOutDelegate *)calloc(1, sizeof(ZizhanOutDelegate));
    Class valCls = objc_lookUpClass("NSValue");
    id nsv = valCls ? ((id(*)(Class, SEL, const void *))objc_msgSend)(
        valCls, sel_registerName("valueWithPointer:"), d) : nil;
    if (nsv) objc_setAssociatedObject(outObj, key, nsv, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    return d;
}

static CGImageRef zcam_make_cgimage_from_bgra(const uint8_t *bgra, uint16_t w, uint16_t h, uint32_t stride) {
    const size_t bytes = (size_t)stride * (size_t)h;
    CFDataRef data = CFDataCreate(NULL, bgra, (CFIndex)bytes);
    if (!data) return NULL;
    CGDataProviderRef prov = CGDataProviderCreateWithCFData(data);
    CFRelease(data);
    if (!prov) return NULL;
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGImageRef img = CGImageCreate(
        w, h, 8, 32, stride, cs,
        (CGBitmapInfo)kCGBitmapByteOrder32Little | (CGBitmapInfo)kCGImageAlphaPremultipliedFirst,
        prov, NULL, false, kCGRenderingIntentDefault);
    CGColorSpaceRelease(cs);
    CGDataProviderRelease(prov);
    return img;
}

static id zcam_encode_jpeg_from_cgimage(CGImageRef img) {
    zcam_resolve_media_syms();
    if (!img || !p_CGImageDestinationCreateWithData || !p_CGImageDestinationAddImage || !p_CGImageDestinationFinalize) return nil;
    CFMutableDataRef mdata = CFDataCreateMutable(NULL, 0);
    if (!mdata) return nil;
    CFTypeRef dest = p_CGImageDestinationCreateWithData(mdata, CFSTR("public.jpeg"), 1, NULL);
    if (!dest) { CFRelease(mdata); return nil; }
    p_CGImageDestinationAddImage(dest, img, NULL);
    const bool ok = p_CGImageDestinationFinalize(dest);
    CFRelease(dest);
    if (!ok) { CFRelease(mdata); return nil; }
    return ((id(*)(id, SEL))objc_msgSend)((id)mdata, sel_registerName("autorelease"));
}

static int zcam_pos_to_cmd(int pos) {
    if (pos == 2) return ZIZHAN_CAM_CMD_FRONT;
    if (pos == 4) return ZIZHAN_CAM_CMD_ULTRA;
    return boot_option("zizhan_cam_qr_overlay=1") ? ZIZHAN_CAM_CMD_BACK_QR : ZIZHAN_CAM_CMD_BACK;
}

static void zcam_pump_tick(ZizhanSessionCtx *ctx) {
    if (!ctx || !ctx->running || !ctx->frame_buf) return;

    /* Renew kernel camera lease once per second so sensor stays active while running */
    const uint64_t now_ns = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    if (now_ns - ctx->last_lease_ns >= 900000000ull) {
        const int cmd = zcam_pos_to_cmd(ctx->position);
        (void)zizhan_cam_ctl_write(cmd);
        ctx->last_lease_ns = now_ns;
    }

    size_t got = ZIZHAN_CAM_TOTAL_BYTES;
    if (sysctlbyname("kern.zizhan_cam_frame", ctx->frame_buf, &got, NULL, 0) != 0 ||
        got < sizeof(ZizhanCamFrameHeader) + 64u) {
        return;
    }
    const ZizhanCamFrameHeader *hdr = (const ZizhanCamFrameHeader *)ctx->frame_buf;
    if (hdr->magic != ZIZHAN_CAM_MAGIC || hdr->seq == 0 || hdr->seq == ctx->last_seq) return;
    if ((size_t)sizeof(ZizhanCamFrameHeader) + (size_t)hdr->stride * hdr->height > got) return;
    ctx->last_seq = hdr->seq;
    ++ctx->frames_delivered;

    const uint8_t *bgra = ctx->frame_buf + sizeof(ZizhanCamFrameHeader);
    CGImageRef cgImg = zcam_make_cgimage_from_bgra(bgra, hdr->width, hdr->height, hdr->stride);
    if (!cgImg) return;

    if (ctx->frames_delivered == 1 || (ctx->frames_delivered % 40u) == 0u) {
        fprintf(stderr, "Zizhan avcap18: frame#%u seq=%u pos=%u %ux%u luma=%u exp=%u gain_q8=%u af_dac=%u flags=0x%x\n",
                ctx->frames_delivered, hdr->seq, hdr->sensor_pos, hdr->width, hdr->height,
                hdr->mean_luma8, hdr->exposure_lines, hdr->analog_gain_q8, hdr->af_dac, hdr->flags);
        fflush(stderr);
    }

    /* 1. Push CGImage to all attached AVCaptureVideoPreviewLayers on main queue */
    const CFIndex nPrev = CFArrayGetCount(ctx->preview_layers);
    for (CFIndex i = 0; i < nPrev; ++i) {
        id layer = (id)CFArrayGetValueAtIndex(ctx->preview_layers, i);
        if (!layer) continue;
        ((id(*)(id, SEL))objc_msgSend)(layer, sel_registerName("retain"));
        CGImageRetain(cgImg);
        dispatch_async(dispatch_get_main_queue(), ^{
            ((void(*)(id, SEL, id))objc_msgSend)(layer, sel_registerName("setContents:"), (id)cgImg);
            CGImageRelease(cgImg);
            ((void(*)(id, SEL))objc_msgSend)(layer, sel_registerName("release"));
        });
    }

    /* 2. Deliver to attached outputs (VideoDataOutput, MetadataOutput QR) */
    Class voutCls = objc_lookUpClass("AVCaptureVideoDataOutput");
    Class moutCls = objc_lookUpClass("AVCaptureMetadataOutput");
    const CFIndex nOut = CFArrayGetCount(ctx->outputs);

    for (CFIndex i = 0; i < nOut; ++i) {
        id outObj = (id)CFArrayGetValueAtIndex(ctx->outputs, i);
        if (!outObj) continue;

        /* 2a. AVCaptureVideoDataOutput -> CMSampleBufferRef */
        if (voutCls && ((BOOL(*)(id, SEL, Class))objc_msgSend)(outObj, sel_registerName("isKindOfClass:"), voutCls)) {
            zcam_resolve_media_syms();
            ZizhanOutDelegate *vd = zcam_out_del(outObj, &kKeyVoutState, false);
            SEL sCb = sel_registerName("captureOutput:didOutputSampleBuffer:fromConnection:");
            if (vd && vd->delegate && p_CVPixelBufferCreate && p_CMVideoFormatDescriptionCreateForImageBuffer &&
                p_CMSampleBufferCreateReadyWithImageBuffer &&
                ((BOOL(*)(id, SEL, SEL))objc_msgSend)(vd->delegate, sel_registerName("respondsToSelector:"), sCb)) {
                ZCamCVPixelBufferRef pxBuf = NULL;
                if (p_CVPixelBufferCreate(NULL, hdr->width, hdr->height, 0x42475241u, NULL, &pxBuf) == 0 && pxBuf) {
                    p_CVPixelBufferLockBaseAddress(pxBuf, 0);
                    uint8_t *dst = (uint8_t *)p_CVPixelBufferGetBaseAddress(pxBuf);
                    const size_t dstStride = p_CVPixelBufferGetBytesPerRow(pxBuf);
                    for (uint16_t y = 0; y < hdr->height; ++y) {
                        memcpy(dst + y * dstStride, bgra + (size_t)y * hdr->stride, (size_t)hdr->width * 4u);
                    }
                    p_CVPixelBufferUnlockBaseAddress(pxBuf, 0);
                    ZCamCMVideoFormatDescRef fmt = NULL;
                    if (p_CMVideoFormatDescriptionCreateForImageBuffer(NULL, pxBuf, &fmt) == 0 && fmt) {
                        ZCamCMSampleTimingInfo timing = {
                            .duration = {1, 20, 1u, 0},
                            .presentationTimeStamp = {(int64_t)hdr->timestamp_ns, 1000000000, 1u, 0},
                            .decodeTimeStamp = {0, 0, 0u, 0}
                        };
                        ZCamCMSampleBufferRef sbuf = NULL;
                        if (p_CMSampleBufferCreateReadyWithImageBuffer(NULL, pxBuf, fmt, &timing, &sbuf) == 0 && sbuf) {
                            dispatch_queue_t q = vd->queue ? vd->queue : dispatch_get_main_queue();
                            id del = vd->delegate;
                            dispatch_async(q, ^{
                                ((void(*)(id, SEL, id, ZCamCMSampleBufferRef, id))objc_msgSend)(del, sCb, outObj, sbuf, nil);
                                CFRelease((CFTypeRef)sbuf);
                            });
                        }
                        CFRelease((CFTypeRef)fmt);
                    }
                    CFRelease((CFTypeRef)pxBuf);
                }
            }
        }

        /* 2b. AVCaptureMetadataOutput -> fast ISO-18004 C QR scan (~0.2 ms) + CoreImage fallback at ~5 Hz */
        if (moutCls && (ctx->frames_delivered % 4u) == 1u &&
            ((BOOL(*)(id, SEL, Class))objc_msgSend)(outObj, sel_registerName("isKindOfClass:"), moutCls)) {
            ZizhanOutDelegate *md = zcam_out_del(outObj, &kKeyMoutState, false);
            SEL sMetaCb = sel_registerName("captureOutput:didOutputMetadataObjects:fromConnection:");
            if (md && md->delegate && ((BOOL(*)(id, SEL, SEL))objc_msgSend)(md->delegate, sel_registerName("respondsToSelector:"), sMetaCb)) {
                Class mutArrCls = objc_lookUpClass("NSMutableArray");
                id metaArr = (mutArrCls && gZizhanCamQrMetaCls)
                             ? ((id(*)(Class, SEL))objc_msgSend)(mutArrCls, sel_registerName("array")) : nil;

                /* Fast path (< 0.3 ms, never stalls the 20 fps preview queue): ISO-18004 V1..V6 scanner */
                if (metaArr) {
                    char qr_str[256] = {0};
                    uint16_t qr_box[4] = {0};
                    if (zizhan_qr_scan_bgra(bgra, hdr->width, hdr->height, hdr->stride,
                                            qr_str, sizeof(qr_str), qr_box)) {
                        id msg = zizhan_nsstr(qr_str);
                        if (msg) {
                            id qrObj = ((id(*)(Class, SEL))objc_msgSend)(gZizhanCamQrMetaCls, sel_registerName("alloc"));
                            qrObj = ((id(*)(id, SEL))gNsObjectInitImp)(qrObj, sel_registerName("init"));
                            objc_setAssociatedObject(qrObj, &kKeyQrString, msg, OBJC_ASSOCIATION_COPY_NONATOMIC);
                            Class valCls = objc_lookUpClass("NSValue");
                            SEL sRect = sel_registerName("valueWithCGRect:");
                            if (valCls && hdr->width && hdr->height &&
                                ((BOOL(*)(Class, SEL, SEL))objc_msgSend)(valCls, sel_registerName("respondsToSelector:"), sRect)) {
                                CGRect norm = CGRectMake((double)qr_box[0] / hdr->width,
                                                         (double)qr_box[1] / hdr->height,
                                                         (double)qr_box[2] / hdr->width,
                                                         (double)qr_box[3] / hdr->height);
                                id rv = ((id(*)(Class, SEL, CGRect))objc_msgSend)(valCls, sRect, norm);
                                if (rv) objc_setAssociatedObject(qrObj, &kKeyQrBounds, rv, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
                            }
                            ((void(*)(id, SEL, id))objc_msgSend)(metaArr, sel_registerName("addObject:"), qrObj);
                            ((void(*)(id, SEL))objc_msgSend)(qrObj, sel_registerName("release"));
                        }
                    }
                }

                /* CoreImage CIDetectorTypeQRCode fallback (sampled every 20th frame for V7..V40 dense QRs) */
                if (metaArr && ((unsigned long(*)(id, SEL))objc_msgSend)(metaArr, sel_registerName("count")) == 0ul &&
                    (ctx->frames_delivered % 20u) == 1u) {
                    if (!ctx->qr_detector) {
                        void *ciHdl = dlopen("/System/Library/Frameworks/CoreImage.framework/CoreImage", RTLD_LAZY);
                        zizhan_ci_sw_policy();
                        const CFStringRef *kSwKey = ciHdl ? (const CFStringRef *)dlsym(ciHdl, "kCIContextUseSoftwareRenderer") : NULL;
                        CFStringRef swKey = (kSwKey && *kSwKey) ? *kSwKey : CFSTR("use_software_renderer");
                        const void *keys[1] = { swKey };
                        const void *vals[1] = { kCFBooleanTrue };
                        CFDictionaryRef swOpts = CFDictionaryCreate(NULL, keys, vals, 1,
                            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
                        Class ciCtxCls = objc_lookUpClass("CIContext");
                        Class ciDetCls = objc_lookUpClass("CIDetector");
                        if (ciCtxCls && ciDetCls && swOpts) {
                            ctx->ci_context = zcam_create_sw_cicontext(nil, (id)swOpts);
                            ctx->qr_detector = ctx->ci_context ? ((id(*)(Class, SEL, id, id, id))objc_msgSend)(
                                ciDetCls, sel_registerName("detectorOfType:context:options:"),
                                zizhan_nsstr("CIDetectorTypeQRCode"), ctx->ci_context, (id)swOpts) : nil;
                            if (ctx->qr_detector) ((id(*)(id, SEL))objc_msgSend)(ctx->qr_detector, sel_registerName("retain"));
                        }
                        if (swOpts) CFRelease(swOpts);
                    }
                    Class ciImgCls = objc_lookUpClass("CIImage");
                    if (ctx->qr_detector && ciImgCls) {
                        id ciImg = ((id(*)(Class, SEL, CGImageRef))objc_msgSend)(ciImgCls, sel_registerName("imageWithCGImage:"), cgImg);
                        id feats = ciImg ? ((id(*)(id, SEL, id))objc_msgSend)(ctx->qr_detector, sel_registerName("featuresInImage:"), ciImg) : nil;
                        unsigned long fcnt = feats ? ((unsigned long(*)(id, SEL))objc_msgSend)(feats, sel_registerName("count")) : 0ul;
                        for (unsigned long k = 0; k < fcnt; ++k) {
                            id feat = ((id(*)(id, SEL, unsigned long))objc_msgSend)(feats, sel_registerName("objectAtIndex:"), k);
                            SEL sMsg = sel_registerName("messageString");
                            id msg = (feat && ((BOOL(*)(id, SEL, SEL))objc_msgSend)(feat, sel_registerName("respondsToSelector:"), sMsg))
                                     ? ((id(*)(id, SEL))objc_msgSend)(feat, sMsg) : nil;
                            if (!msg) continue;
                            id qrObj = ((id(*)(Class, SEL))objc_msgSend)(gZizhanCamQrMetaCls, sel_registerName("alloc"));
                            qrObj = ((id(*)(id, SEL))gNsObjectInitImp)(qrObj, sel_registerName("init"));
                            objc_setAssociatedObject(qrObj, &kKeyQrString, msg, OBJC_ASSOCIATION_COPY_NONATOMIC);
                            ((void(*)(id, SEL, id))objc_msgSend)(metaArr, sel_registerName("addObject:"), qrObj);
                            ((void(*)(id, SEL))objc_msgSend)(qrObj, sel_registerName("release"));
                        }
                    }
                }
                if (metaArr && ((unsigned long(*)(id, SEL))objc_msgSend)(metaArr, sel_registerName("count")) > 0ul) {
                    ((id(*)(id, SEL))objc_msgSend)(metaArr, sel_registerName("retain"));
                    dispatch_queue_t q = md->queue ? md->queue : dispatch_get_main_queue();
                    id del = md->delegate;
                    dispatch_async(q, ^{
                        ((void(*)(id, SEL, id, id, id))objc_msgSend)(del, sMetaCb, outObj, metaArr, nil);
                        ((void(*)(id, SEL))objc_msgSend)(metaArr, sel_registerName("release"));
                    });
                }
            }
        }
    }

    CGImageRelease(cgImg);
}

/* --- AVCaptureSession method hooks --- */
static void zcam_sess_stop(id self, SEL _cmd);

static void zcam_sess_dealloc(id self, SEL _cmd) {
    ZizhanSessionCtx *ctx = zcam_get_sess_ctx(self, false);
    if (ctx) {
        if (ctx->running) zcam_sess_stop(self, sel_registerName("stopRunning"));
        if (ctx->inputs) { CFRelease(ctx->inputs); ctx->inputs = NULL; }
        if (ctx->outputs) { CFRelease(ctx->outputs); ctx->outputs = NULL; }
        if (ctx->preview_layers) { CFRelease(ctx->preview_layers); ctx->preview_layers = NULL; }
        /* Free pump buffers asynchronously on serial pump queue so any final block finishes cleanly */
        uint8_t *fbuf = ctx->frame_buf;
        id cictx = ctx->ci_context;
        id qrdet = ctx->qr_detector;
        dispatch_queue_t q = ctx->queue;
        ctx->frame_buf = NULL;
        ctx->ci_context = nil;
        ctx->qr_detector = nil;
        objc_setAssociatedObject(self, &kKeySessionState, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        if (q) {
            dispatch_async(q, ^{
                if (qrdet) ((void(*)(id, SEL))objc_msgSend)(qrdet, sel_registerName("release"));
                if (cictx) ((void(*)(id, SEL))objc_msgSend)(cictx, sel_registerName("release"));
                free(fbuf);
                free(ctx);
            });
            dispatch_release(q);
        } else {
            free(fbuf);
            free(ctx);
        }
    }
    if (gNsObjectDeallocImp) ((void(*)(id, SEL))gNsObjectDeallocImp)(self, _cmd);
}

static BOOL zcam_sess_can_add_in(id self, SEL _cmd, id inp) {
    (void)self; (void)_cmd;
    return inp != nil ? YES : NO;
}

static void zcam_sess_add_in(id self, SEL _cmd, id inp) {
    (void)_cmd;
    if (!inp) return;
    ZizhanSessionCtx *ctx = zcam_get_sess_ctx(self, true);
    if (ctx) {
        CFArrayRemoveAllValues(ctx->inputs);
        CFArrayAppendValue(ctx->inputs, (const void *)inp);
        id dev = objc_getAssociatedObject(inp, &kKeyInputDev);
        long pos = dev ? (long)(intptr_t)objc_getAssociatedObject(dev, &kKeyDevPos) : 1;
        ctx->position = (pos == 2) ? 2 : (pos == 4 ? 4 : 1);
        if (ctx->running) {
            ctx->last_lease_ns = 0;
            (void)zizhan_cam_ctl_write(zcam_pos_to_cmd(ctx->position));
        }
    }
}

static void zcam_sess_rem_in(id self, SEL _cmd, id inp) {
    (void)_cmd; (void)inp;
    ZizhanSessionCtx *ctx = zcam_get_sess_ctx(self, false);
    if (ctx && ctx->inputs) CFArrayRemoveAllValues(ctx->inputs);
}

static id zcam_sess_inputs(id self, SEL _cmd) {
    (void)_cmd;
    ZizhanSessionCtx *ctx = zcam_get_sess_ctx(self, true);
    return ctx ? (id)ctx->inputs : zcam_dev_empty_array(self, _cmd);
}

static BOOL zcam_sess_can_add_out(id self, SEL _cmd, id outObj) {
    (void)_cmd; (void)self;
    return outObj != nil ? YES : NO;
}

static void zcam_sess_add_out(id self, SEL _cmd, id outObj) {
    (void)_cmd;
    ZizhanSessionCtx *ctx = zcam_get_sess_ctx(self, true);
    if (ctx && outObj) CFArrayAppendValue(ctx->outputs, (const void *)outObj);
}

static void zcam_sess_rem_out(id self, SEL _cmd, id outObj) {
    (void)_cmd;
    ZizhanSessionCtx *ctx = zcam_get_sess_ctx(self, false);
    if (!ctx || !ctx->outputs || !outObj) return;
    for (CFIndex i = CFArrayGetCount(ctx->outputs) - 1; i >= 0; --i) {
        if (CFArrayGetValueAtIndex(ctx->outputs, i) == (const void *)outObj)
            CFArrayRemoveValueAtIndex(ctx->outputs, i);
    }
}

static id zcam_sess_outputs(id self, SEL _cmd) {
    (void)_cmd;
    ZizhanSessionCtx *ctx = zcam_get_sess_ctx(self, true);
    return ctx ? (id)ctx->outputs : zcam_dev_empty_array(self, _cmd);
}

static BOOL zcam_sess_can_preset(id self, SEL _cmd, id preset) { (void)self; (void)_cmd; (void)preset; return YES; }
static void zcam_sess_set_preset(id self, SEL _cmd, id preset) { (void)self; (void)_cmd; (void)preset; }
static id zcam_sess_get_preset(id self, SEL _cmd) { (void)self; (void)_cmd; return zizhan_nsstr("AVCaptureSessionPresetHigh"); }
static void zcam_sess_cfg_noop(id self, SEL _cmd) { (void)self; (void)_cmd; }

static BOOL zcam_sess_is_running(id self, SEL _cmd) {
    (void)_cmd;
    ZizhanSessionCtx *ctx = zcam_get_sess_ctx(self, false);
    return (ctx && ctx->running) ? YES : NO;
}

static void zcam_sess_start(id self, SEL _cmd) {
    (void)_cmd;
    ZizhanSessionCtx *ctx = zcam_get_sess_ctx(self, true);
    if (!ctx || ctx->running) return;
    ctx->running = 1;
    ctx->last_lease_ns = 0;
    const int cmd = zcam_pos_to_cmd(ctx->position);
    int rc = zizhan_cam_ctl_write(cmd);
    fprintf(stderr, "Zizhan avcap18: startRunning (%s) pos=%d cmd=%d ctl_rc=%d\n",
            getprogname() ? getprogname() : "?", ctx->position, cmd, rc);
    fflush(stderr);

    dispatch_source_t t = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, ctx->queue);
    dispatch_source_set_timer(t, dispatch_time(DISPATCH_TIME_NOW, 40 * NSEC_PER_MSEC),
                              50 * NSEC_PER_MSEC, 5 * NSEC_PER_MSEC);
    dispatch_source_set_event_handler(t, ^{
        zcam_pump_tick(ctx);
    });
    ctx->timer = t;
    dispatch_resume(t);
}

static void zcam_sess_stop(id self, SEL _cmd) {
    (void)_cmd;
    ZizhanSessionCtx *ctx = zcam_get_sess_ctx(self, false);
    if (!ctx || !ctx->running) return;
    ctx->running = 0;
    if (ctx->timer) {
        dispatch_source_cancel(ctx->timer);
        dispatch_release(ctx->timer);
        ctx->timer = NULL;
    }
    (void)zizhan_cam_ctl_write(ZIZHAN_CAM_CMD_OFF);
    fprintf(stderr, "Zizhan avcap18: stopRunning (%s) delivered=%u\n",
            getprogname() ? getprogname() : "?", ctx->frames_delivered);
    fflush(stderr);
}

/* --- Preview Layer (PongZizhanPreviewLayer18 : AVCaptureVideoPreviewLayer with CALayer init/dealloc) --- */
static void zcam_attach_preview(id layer, id session) {
    if (!layer) return;
    objc_setAssociatedObject(layer, &kKeyPreviewSession, session, OBJC_ASSOCIATION_ASSIGN);
    ((void(*)(id, SEL, id))objc_msgSend)(layer, sel_registerName("setContentsGravity:"), zizhan_nsstr("resizeAspectFill"));
    if (session) {
        ZizhanSessionCtx *ctx = zcam_get_sess_ctx(session, true);
        if (ctx && ctx->preview_layers) {
            const CFIndex n = CFArrayGetCount(ctx->preview_layers);
            for (CFIndex i = 0; i < n; ++i) {
                if (CFArrayGetValueAtIndex(ctx->preview_layers, i) == (const void *)layer) return;
            }
            CFArrayAppendValue(ctx->preview_layers, (const void *)layer);
        }
    }
}

static void zcam_prev_set_gravity(id self, SEL _cmd, id grav) {
    (void)_cmd;
    objc_setAssociatedObject(self, &kKeyPreviewGravity, grav, OBJC_ASSOCIATION_COPY_NONATOMIC);
    const char *g = grav ? ((const char *(*)(id, SEL))objc_msgSend)(grav, sel_registerName("UTF8String")) : NULL;
    const char *caGrav = "resizeAspectFill";
    if (g && strstr(g, "ResizeAspect") && !strstr(g, "Fill")) caGrav = "resizeAspect";
    else if (g && !strcmp(g, "AVLayerVideoGravityResize")) caGrav = "resize";
    ((void(*)(id, SEL, id))objc_msgSend)(self, sel_registerName("setContentsGravity:"), zizhan_nsstr(caGrav));
}

static id zcam_prev_get_gravity(id self, SEL _cmd) {
    (void)_cmd;
    id g = objc_getAssociatedObject(self, &kKeyPreviewGravity);
    return g ? g : zizhan_nsstr("AVLayerVideoGravityResizeAspectFill");
}

static id zcam_prev_transform_meta(id self, SEL _cmd, id metaObj) {
    (void)self; (void)_cmd;
    return metaObj;
}

static CGPoint zcam_prev_pt(id self, SEL _cmd, CGPoint pt) {
    (void)self; (void)_cmd;
    return pt;
}

static BOOL zcam_prev_is_kind(id self, SEL _cmd, Class cls) {
    if (cls && cls == gOrigPrevCls) return YES;
    return gNsObjectIsKindImp ? ((BOOL(*)(id, SEL, Class))gNsObjectIsKindImp)(self, _cmd, cls) : NO;
}

static id zcam_prev_alloc_zone(id cls, SEL _cmd, void *zone) {
    (void)cls;
    return gZizhanPreviewLayerCls
        ? ((id(*)(Class, SEL, void *))objc_msgSend)(gZizhanPreviewLayerCls, _cmd, zone)
        : nil;
}

static id zcam_prev_layer_with_sess(id cls, SEL _cmd, id session) {
    (void)cls; (void)_cmd;
    if (!gZizhanPreviewLayerCls || !gCaLayerInitImp) return nil;
    id layer = ((id(*)(Class, SEL))objc_msgSend)(gZizhanPreviewLayerCls, sel_registerName("alloc"));
    layer = ((id(*)(id, SEL))gCaLayerInitImp)(layer, sel_registerName("init"));
    zcam_attach_preview(layer, session);
    return layer ? ((id(*)(id, SEL))objc_msgSend)(layer, sel_registerName("autorelease")) : nil;
}

static id zcam_prev_init_with_sess(id self, SEL _cmd, id session) {
    (void)_cmd;
    id obj = gCaLayerInitImp ? ((id(*)(id, SEL))gCaLayerInitImp)(self, sel_registerName("init")) : self;
    zcam_attach_preview(obj, session);
    return obj;
}

static void zcam_prev_set_session(id self, SEL _cmd, id session) {
    (void)_cmd;
    zcam_attach_preview(self, session);
}

static id zcam_prev_session(id self, SEL _cmd) {
    (void)_cmd;
    return objc_getAssociatedObject(self, &kKeyPreviewSession);
}

/* --- Output hooks (VideoDataOutput, MetadataOutput, PhotoOutput) --- */
static void zcam_vout_set_del(id self, SEL _cmd, id del, dispatch_queue_t q) {
    (void)_cmd;
    ZizhanOutDelegate *d = zcam_out_del(self, &kKeyVoutState, true);
    if (d) { d->delegate = del; d->queue = q; }
}

static void zcam_vout_set_settings(id self, SEL _cmd, id dict) {
    (void)_cmd;
    objc_setAssociatedObject(self, &kKeyVoutSettings, dict, OBJC_ASSOCIATION_COPY_NONATOMIC);
}

static id zcam_vout_get_settings(id self, SEL _cmd) {
    (void)_cmd;
    return objc_getAssociatedObject(self, &kKeyVoutSettings);
}

static void zcam_mout_set_del(id self, SEL _cmd, id del, dispatch_queue_t q) {
    (void)_cmd;
    ZizhanOutDelegate *d = zcam_out_del(self, &kKeyMoutState, true);
    if (d) { d->delegate = del; d->queue = q; }
}

static id zcam_mout_avail_types(id self, SEL _cmd) {
    (void)self; (void)_cmd;
    Class arr = objc_lookUpClass("NSArray");
    id qr = zizhan_nsstr("org.iso.QRCode");
    return (arr && qr) ? ((id(*)(Class, SEL, id))objc_msgSend)(arr, sel_registerName("arrayWithObject:"), qr) : nil;
}

static void zcam_mout_set_types(id self, SEL _cmd, id types) { (void)self; (void)_cmd; (void)types; }

static void zcam_pout_capture(id self, SEL _cmd, id settings, id delegate) {
    (void)_cmd;
    if (!delegate || !gZizhanCamPhotoCls || !gNsObjectInitImp) return;
    uint8_t *buf = (uint8_t *)malloc(ZIZHAN_CAM_TOTAL_BYTES);
    size_t got = ZIZHAN_CAM_TOTAL_BYTES;
    CGImageRef cgImg = NULL;
    if (buf && sysctlbyname("kern.zizhan_cam_frame", buf, &got, NULL, 0) == 0 &&
        got >= sizeof(ZizhanCamFrameHeader) + 64u) {
        const ZizhanCamFrameHeader *hdr = (const ZizhanCamFrameHeader *)buf;
        if (hdr->magic == ZIZHAN_CAM_MAGIC && (size_t)sizeof(ZizhanCamFrameHeader) + (size_t)hdr->stride * hdr->height <= got) {
            cgImg = zcam_make_cgimage_from_bgra(buf + sizeof(ZizhanCamFrameHeader), hdr->width, hdr->height, hdr->stride);
        }
    }
    free(buf);
    if (!cgImg) {
        /* Session might still be warming up first frame (~150 ms); retry once after 450 ms */
        if (settings) {
            ((id(*)(id, SEL))objc_msgSend)(delegate, sel_registerName("retain"));
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 450 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                zcam_pout_capture(self, sel_registerName("capturePhotoWithSettings:delegate:"), nil, delegate);
                ((void(*)(id, SEL))objc_msgSend)(delegate, sel_registerName("release"));
            });
        }
        return;
    }
    id jpeg = zcam_encode_jpeg_from_cgimage(cgImg);
    id photo = ((id(*)(Class, SEL))objc_msgSend)(gZizhanCamPhotoCls, sel_registerName("alloc"));
    photo = ((id(*)(id, SEL))gNsObjectInitImp)(photo, sel_registerName("init"));
    if (jpeg) objc_setAssociatedObject(photo, &kKeyPhotoJpeg, jpeg, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    objc_setAssociatedObject(photo, &kKeyPhotoCgImg, (id)cgImg, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    CGImageRelease(cgImg);

    ((id(*)(id, SEL))objc_msgSend)(delegate, sel_registerName("retain"));
    SEL sPhotoCb = sel_registerName("captureOutput:didFinishProcessingPhoto:error:");
    dispatch_async(dispatch_get_main_queue(), ^{
        if (((BOOL(*)(id, SEL, SEL))objc_msgSend)(delegate, sel_registerName("respondsToSelector:"), sPhotoCb)) {
            ((void(*)(id, SEL, id, id, id))objc_msgSend)(delegate, sPhotoCb, self, photo, nil);
        }
        ((void(*)(id, SEL))objc_msgSend)(photo, sel_registerName("release"));
        ((void(*)(id, SEL))objc_msgSend)(delegate, sel_registerName("release"));
    });
}

static void zcam_replace_inst(Class cls, const char *selName, IMP imp, const char *types) {
    if (!cls) return;
    SEL s = sel_registerName(selName);
    Method m = class_getInstanceMethod(cls, s);
    if (m && class_getInstanceMethod(class_getSuperclass(cls), s) != m) {
        (void)method_setImplementation(m, imp);
    } else {
        (void)class_addMethod(cls, s, imp, types);
    }
}

static void zizhan_avcapture_policy(void) {
    if (avcap_policy_installed || boot_option("zizhan_avcapture=0")) return;
    Class devCls  = objc_lookUpClass("AVCaptureDevice");
    Class discCls = objc_lookUpClass("AVCaptureDeviceDiscoverySession");
    Class inpCls  = objc_lookUpClass("AVCaptureDeviceInput");
    Class sessCls = objc_lookUpClass("AVCaptureSession");
    Class prevCls = objc_lookUpClass("AVCaptureVideoPreviewLayer");
    Class voutCls = objc_lookUpClass("AVCaptureVideoDataOutput");
    Class moutCls = objc_lookUpClass("AVCaptureMetadataOutput");
    Class poutCls = objc_lookUpClass("AVCapturePhotoOutput");
    Class psetCls = objc_lookUpClass("AVCapturePhotoSettings");
    Class phoCls  = objc_lookUpClass("AVCapturePhoto");
    Class qrCls   = objc_lookUpClass("AVMetadataMachineReadableCodeObject");
    Class caCls   = objc_lookUpClass("CALayer");
    Class nsoCls  = objc_lookUpClass("NSObject");
    if (!devCls || !inpCls || !sessCls || !prevCls || !caCls || !nsoCls) return;

    gOrigPrevCls        = prevCls;
    gNsObjectInitImp    = class_getMethodImplementation(nsoCls, sel_registerName("init"));
    gNsObjectDeallocImp = class_getMethodImplementation(nsoCls, sel_registerName("dealloc"));
    gNsObjectDescImp    = class_getMethodImplementation(nsoCls, sel_registerName("description"));
    gNsObjectIsKindImp  = class_getMethodImplementation(nsoCls, sel_registerName("isKindOfClass:"));
    gCaLayerInitImp     = class_getMethodImplementation(caCls, sel_registerName("init"));
    gCaLayerDeallocImp  = class_getMethodImplementation(caCls, sel_registerName("dealloc"));
    if (!gNsObjectInitImp || !gNsObjectDeallocImp || !gCaLayerInitImp || !gCaLayerDeallocImp) return;

    /* 1. Allocate safe subclasses with NSObject/CALayer dealloc so _internal == NULL never faults */
    if (!gZizhanCamDeviceCls) {
        Class c = objc_allocateClassPair(devCls, "PongZizhanCaptureDevice18", 0);
        if (c) {
            class_addMethod(c, sel_registerName("dealloc"), gNsObjectDeallocImp, "v@:");
            if (gNsObjectDescImp) class_addMethod(c, sel_registerName("description"), gNsObjectDescImp, "@@:");
            class_addMethod(c, sel_registerName("uniqueID"), (IMP)zcam_dev_unique_id, "@@:");
            class_addMethod(c, sel_registerName("modelID"), (IMP)zcam_dev_model_id, "@@:");
            class_addMethod(c, sel_registerName("manufacturer"), (IMP)zcam_dev_manufacturer, "@@:");
            class_addMethod(c, sel_registerName("localizedName"), (IMP)zcam_dev_localized_name, "@@:");
            class_addMethod(c, sel_registerName("position"), (IMP)zcam_dev_position, "q@:");
            class_addMethod(c, sel_registerName("deviceType"), (IMP)zcam_dev_type, "@@:");
            class_addMethod(c, sel_registerName("isConnected"), (IMP)zcam_dev_yes, "B@:");
            class_addMethod(c, sel_registerName("isSuspended"), (IMP)zcam_dev_no, "B@:");
            class_addMethod(c, sel_registerName("hasMediaType:"), (IMP)zcam_dev_has_media, "B@:@");
            class_addMethod(c, sel_registerName("lockForConfiguration:"), (IMP)zcam_dev_lock_cfg, "B@:^@");
            class_addMethod(c, sel_registerName("unlockForConfiguration"), (IMP)zcam_dev_noop, "v@:");
            class_addMethod(c, sel_registerName("formats"), (IMP)zcam_dev_empty_array, "@@:");
            class_addMethod(c, sel_registerName("activeFormat"), (IMP)zcam_input_nil, "@@:");
            class_addMethod(c, sel_registerName("videoZoomFactor"), (IMP)zcam_dev_zoom_one, "d@:");
            class_addMethod(c, sel_registerName("isFocusModeSupported:"), (IMP)zcam_dev_focus_sup, "B@:q");
            class_addMethod(c, sel_registerName("isFocusPointOfInterestSupported"), (IMP)zcam_dev_focus_poi_sup, "B@:");
            class_addMethod(c, sel_registerName("focusMode"), (IMP)zcam_dev_focus_mode, "q@:");
            class_addMethod(c, sel_registerName("setFocusMode:"), (IMP)zcam_dev_set_focus_mode, "v@:q");
            class_addMethod(c, sel_registerName("setFocusPointOfInterest:"), (IMP)zcam_dev_set_focus_poi, "v@:{CGPoint=dd}");
            class_addMethod(c, sel_registerName("isAdjustingFocus"), (IMP)zcam_dev_no, "B@:");
            class_addMethod(c, sel_registerName("isExposureModeSupported:"), (IMP)zcam_dev_no_arg, "B@:q");
            class_addMethod(c, sel_registerName("isWhiteBalanceModeSupported:"), (IMP)zcam_dev_no_arg, "B@:q");
            class_addMethod(c, sel_registerName("isTorchModeSupported:"), (IMP)zcam_dev_no_arg, "B@:q");
            class_addMethod(c, sel_registerName("isFlashModeSupported:"), (IMP)zcam_dev_no_arg, "B@:q");
            class_addMethod(c, sel_registerName("hasTorch"), (IMP)zcam_dev_no, "B@:");
            class_addMethod(c, sel_registerName("hasFlash"), (IMP)zcam_dev_no, "B@:");
            objc_registerClassPair(c);
            gZizhanCamDeviceCls = c;
        }
    }
    if (!gZizhanCamInputCls) {
        Class c = objc_allocateClassPair(inpCls, "PongZizhanCaptureDeviceInput18", 0);
        if (c) {
            class_addMethod(c, sel_registerName("dealloc"), gNsObjectDeallocImp, "v@:");
            if (gNsObjectDescImp) class_addMethod(c, sel_registerName("description"), gNsObjectDescImp, "@@:");
            class_addMethod(c, sel_registerName("device"), (IMP)zcam_input_device, "@@:");
            class_addMethod(c, sel_registerName("ports"), (IMP)zcam_dev_empty_array, "@@:");
            class_addMethod(c, sel_registerName("notReadyError"), (IMP)zcam_input_nil, "@@:");
            objc_registerClassPair(c);
            gZizhanCamInputCls = c;
        }
    }
    if (!gZizhanCamPhotoCls && phoCls) {
        Class c = objc_allocateClassPair(phoCls, "PongZizhanCapturePhoto18", 0);
        if (c) {
            class_addMethod(c, sel_registerName("dealloc"), gNsObjectDeallocImp, "v@:");
            if (gNsObjectDescImp) class_addMethod(c, sel_registerName("description"), gNsObjectDescImp, "@@:");
            class_addMethod(c, sel_registerName("fileDataRepresentation"), (IMP)zcam_photo_file_data, "@@:");
            class_addMethod(c, sel_registerName("cgImageRepresentation"), (IMP)zcam_photo_cgimage, "^{CGImage=}@:");
            objc_registerClassPair(c);
            gZizhanCamPhotoCls = c;
        }
    }
    if (!gZizhanCamPhotoSettingsCls && psetCls) {
        Class c = objc_allocateClassPair(psetCls, "PongZizhanPhotoSettings18", 0);
        if (c) {
            class_addMethod(c, sel_registerName("init"), gNsObjectInitImp, "@@:");
            class_addMethod(c, sel_registerName("dealloc"), gNsObjectDeallocImp, "v@:");
            if (gNsObjectDescImp) class_addMethod(c, sel_registerName("description"), gNsObjectDescImp, "@@:");
            class_addMethod(c, sel_registerName("uniqueID"), (IMP)zcam_ps_uid, "q@:");
            objc_registerClassPair(c);
            gZizhanCamPhotoSettingsCls = c;
        }
    }
    if (!gZizhanCamQrMetaCls && qrCls) {
        Class c = objc_allocateClassPair(qrCls, "PongZizhanQRMetadata18", 0);
        if (c) {
            class_addMethod(c, sel_registerName("dealloc"), gNsObjectDeallocImp, "v@:");
            if (gNsObjectDescImp) class_addMethod(c, sel_registerName("description"), gNsObjectDescImp, "@@:");
            class_addMethod(c, sel_registerName("type"), (IMP)zcam_qr_type, "@@:");
            class_addMethod(c, sel_registerName("stringValue"), (IMP)zcam_qr_string, "@@:");
            class_addMethod(c, sel_registerName("bounds"), (IMP)zcam_qr_bounds, "{CGRect={CGPoint=dd}{CGSize=dd}}@:");
            class_addMethod(c, sel_registerName("corners"), (IMP)zcam_dev_empty_array, "@@:");
            objc_registerClassPair(c);
            gZizhanCamQrMetaCls = c;
        }
    }
    /* Subclass CALayer directly so NO AVCaptureVideoPreviewLayer override (setBounds:, actionForKey:,
     * layerDidBecomeVisible:, sublayers, observeValueForKeyPath:) ever touches _internal == NULL! */
    if (!gZizhanPreviewLayerCls) {
        Class c = objc_allocateClassPair(caCls, "PongZizhanPreviewLayer18", 0);
        if (c) {
            class_addMethod(c, sel_registerName("isKindOfClass:"), (IMP)zcam_prev_is_kind, "B@:#");
            class_addMethod(c, sel_registerName("initWithSession:"), (IMP)zcam_prev_init_with_sess, "@@:@");
            class_addMethod(c, sel_registerName("initWithSessionWithNoConnection:"), (IMP)zcam_prev_init_with_sess, "@@:@");
            class_addMethod(c, sel_registerName("session"), (IMP)zcam_prev_session, "@@:");
            class_addMethod(c, sel_registerName("setSession:"), (IMP)zcam_prev_set_session, "v@:@");
            class_addMethod(c, sel_registerName("setSessionWithNoConnection:"), (IMP)zcam_prev_set_session, "v@:@");
            class_addMethod(c, sel_registerName("videoGravity"), (IMP)zcam_prev_get_gravity, "@@:");
            class_addMethod(c, sel_registerName("setVideoGravity:"), (IMP)zcam_prev_set_gravity, "v@:@");
            class_addMethod(c, sel_registerName("connection"), (IMP)zcam_input_nil, "@@:");
            class_addMethod(c, sel_registerName("isPreviewing"), (IMP)zcam_dev_yes, "B@:");
            class_addMethod(c, sel_registerName("transformedMetadataObjectForMetadataObject:"), (IMP)zcam_prev_transform_meta, "@@:@");
            class_addMethod(c, sel_registerName("captureDevicePointOfInterestForPoint:"), (IMP)zcam_prev_pt, "{CGPoint=dd}@:{CGPoint=dd}");
            class_addMethod(c, sel_registerName("pointForCaptureDevicePointOfInterest:"), (IMP)zcam_prev_pt, "{CGPoint=dd}@:{CGPoint=dd}");
            objc_registerClassPair(c);
            gZizhanPreviewLayerCls = c;
        }
    }

    /* 2. Swizzle AVCaptureDevice discovery & authorization */
    Method m;
    if ((m = class_getClassMethod(devCls, sel_registerName("authorizationStatusForMediaType:"))))
        gOrigAuthStatus = method_setImplementation(m, (IMP)zcam_cls_auth_status);
    if ((m = class_getClassMethod(devCls, sel_registerName("requestAccessForMediaType:completionHandler:"))))
        gOrigReqAccess = method_setImplementation(m, (IMP)zcam_cls_req_access);
    if ((m = class_getClassMethod(devCls, sel_registerName("defaultDeviceWithDeviceType:mediaType:position:"))))
        gOrigDefaultDev3 = method_setImplementation(m, (IMP)zcam_cls_default_dev3);
    if ((m = class_getClassMethod(devCls, sel_registerName("defaultDeviceWithMediaType:"))))
        gOrigDefaultDev1 = method_setImplementation(m, (IMP)zcam_cls_default_dev1);
    if ((m = class_getClassMethod(devCls, sel_registerName("devicesWithMediaType:"))))
        gOrigDevicesMedia = method_setImplementation(m, (IMP)zcam_cls_devices_media);
    if ((m = class_getClassMethod(devCls, sel_registerName("devices"))))
        gOrigDevicesAll = method_setImplementation(m, (IMP)zcam_cls_devices_all);
    if ((m = class_getClassMethod(devCls, sel_registerName("deviceWithUniqueID:"))))
        gOrigDevWithUid = method_setImplementation(m, (IMP)zcam_cls_dev_with_uid);
    if (discCls && (m = class_getInstanceMethod(discCls, sel_registerName("devices"))))
        gOrigDiscDevices = method_setImplementation(m, (IMP)zcam_disc_devices);

    /* 3. Swizzle AVCaptureDeviceInput */
    if ((m = class_getClassMethod(inpCls, sel_registerName("deviceInputWithDevice:error:"))))
        gOrigInputClsCreate = method_setImplementation(m, (IMP)zcam_input_cls_create);
    if ((m = class_getInstanceMethod(inpCls, sel_registerName("initWithDevice:error:"))))
        gOrigInputInit = method_setImplementation(m, (IMP)zcam_input_init);

    /* 4. Replace AVCaptureSession init/dealloc & session lifecycle (zero FigCapture XPC dependency) */
    zcam_replace_inst(sessCls, "init", (IMP)zcam_nsobj_init, "@@:");
    zcam_replace_inst(sessCls, "dealloc", (IMP)zcam_sess_dealloc, "v@:");
    if (gNsObjectDescImp) zcam_replace_inst(sessCls, "description", gNsObjectDescImp, "@@:");
    zcam_replace_inst(sessCls, "canAddInput:", (IMP)zcam_sess_can_add_in, "B@:@");
    zcam_replace_inst(sessCls, "addInput:", (IMP)zcam_sess_add_in, "v@:@");
    zcam_replace_inst(sessCls, "removeInput:", (IMP)zcam_sess_rem_in, "v@:@");
    zcam_replace_inst(sessCls, "inputs", (IMP)zcam_sess_inputs, "@@:");
    zcam_replace_inst(sessCls, "canAddOutput:", (IMP)zcam_sess_can_add_out, "B@:@");
    zcam_replace_inst(sessCls, "addOutput:", (IMP)zcam_sess_add_out, "v@:@");
    zcam_replace_inst(sessCls, "removeOutput:", (IMP)zcam_sess_rem_out, "v@:@");
    zcam_replace_inst(sessCls, "outputs", (IMP)zcam_sess_outputs, "@@:");
    zcam_replace_inst(sessCls, "canSetSessionPreset:", (IMP)zcam_sess_can_preset, "B@:@");
    zcam_replace_inst(sessCls, "setSessionPreset:", (IMP)zcam_sess_set_preset, "v@:@");
    zcam_replace_inst(sessCls, "sessionPreset", (IMP)zcam_sess_get_preset, "@@:");
    zcam_replace_inst(sessCls, "beginConfiguration", (IMP)zcam_sess_cfg_noop, "v@:");
    zcam_replace_inst(sessCls, "commitConfiguration", (IMP)zcam_sess_cfg_noop, "v@:");
    zcam_replace_inst(sessCls, "isRunning", (IMP)zcam_sess_is_running, "B@:");
    zcam_replace_inst(sessCls, "isInterrupted", (IMP)zcam_dev_no, "B@:");
    zcam_replace_inst(sessCls, "startRunning", (IMP)zcam_sess_start, "v@:");
    zcam_replace_inst(sessCls, "stopRunning", (IMP)zcam_sess_stop, "v@:");

    /* 5. Redirect AVCaptureVideoPreviewLayer allocWithZone: & factories to PongZizhanPreviewLayer18 (: CALayer) */
    Class prevMeta = object_getClass((id)prevCls);
    if (prevMeta) {
        zcam_replace_inst(prevMeta, "allocWithZone:", (IMP)zcam_prev_alloc_zone, "@@:^{_NSZone=}");
    }
    if ((m = class_getClassMethod(prevCls, sel_registerName("layerWithSession:"))))
        (void)method_setImplementation(m, (IMP)zcam_prev_layer_with_sess);
    if ((m = class_getClassMethod(prevCls, sel_registerName("layerWithSessionWithNoConnection:"))))
        (void)method_setImplementation(m, (IMP)zcam_prev_layer_with_sess);

    /* 6. Replace Outputs & PhotoSettings with clean NSObject init/dealloc/description + delegate hooks */
    if (voutCls) {
        zcam_replace_inst(voutCls, "init", (IMP)zcam_nsobj_init, "@@:");
        zcam_replace_inst(voutCls, "dealloc", (IMP)zcam_nsobj_dealloc, "v@:");
        if (gNsObjectDescImp) zcam_replace_inst(voutCls, "description", gNsObjectDescImp, "@@:");
        zcam_replace_inst(voutCls, "setSampleBufferDelegate:queue:", (IMP)zcam_vout_set_del, "v@:@@");
        zcam_replace_inst(voutCls, "setVideoSettings:", (IMP)zcam_vout_set_settings, "v@:@");
        zcam_replace_inst(voutCls, "videoSettings", (IMP)zcam_vout_get_settings, "@@:");
        zcam_replace_inst(voutCls, "setAlwaysDiscardsLateVideoFrames:", (IMP)zcam_dev_noop_bool, "v@:B");
        zcam_replace_inst(voutCls, "connections", (IMP)zcam_dev_empty_array, "@@:");
        zcam_replace_inst(voutCls, "connectionWithMediaType:", (IMP)zcam_input_nil, "@@:@");
    }
    if (moutCls) {
        zcam_replace_inst(moutCls, "init", (IMP)zcam_nsobj_init, "@@:");
        zcam_replace_inst(moutCls, "dealloc", (IMP)zcam_nsobj_dealloc, "v@:");
        if (gNsObjectDescImp) zcam_replace_inst(moutCls, "description", gNsObjectDescImp, "@@:");
        zcam_replace_inst(moutCls, "setMetadataObjectsDelegate:queue:", (IMP)zcam_mout_set_del, "v@:@@");
        zcam_replace_inst(moutCls, "availableMetadataObjectTypes", (IMP)zcam_mout_avail_types, "@@:");
        zcam_replace_inst(moutCls, "metadataObjectTypes", (IMP)zcam_mout_avail_types, "@@:");
        zcam_replace_inst(moutCls, "setMetadataObjectTypes:", (IMP)zcam_mout_set_types, "v@:@");
        zcam_replace_inst(moutCls, "connections", (IMP)zcam_dev_empty_array, "@@:");
        zcam_replace_inst(moutCls, "connectionWithMediaType:", (IMP)zcam_input_nil, "@@:@");
    }
    if (poutCls) {
        zcam_replace_inst(poutCls, "init", (IMP)zcam_nsobj_init, "@@:");
        zcam_replace_inst(poutCls, "dealloc", (IMP)zcam_nsobj_dealloc, "v@:");
        if (gNsObjectDescImp) zcam_replace_inst(poutCls, "description", gNsObjectDescImp, "@@:");
        zcam_replace_inst(poutCls, "capturePhotoWithSettings:delegate:", (IMP)zcam_pout_capture, "v@:@@");
        zcam_replace_inst(poutCls, "setHighResolutionCaptureEnabled:", (IMP)zcam_dev_noop_bool, "v@:B");
        zcam_replace_inst(poutCls, "setMaxPhotoQualityPrioritization:", (IMP)zcam_dev_noop_long, "v@:q");
        zcam_replace_inst(poutCls, "connections", (IMP)zcam_dev_empty_array, "@@:");
        zcam_replace_inst(poutCls, "connectionWithMediaType:", (IMP)zcam_input_nil, "@@:@");
    }
    if (psetCls && gZizhanCamPhotoSettingsCls) {
        if ((m = class_getClassMethod(psetCls, sel_registerName("photoSettings"))))
            (void)method_setImplementation(m, (IMP)zcam_ps_make);
        if ((m = class_getClassMethod(psetCls, sel_registerName("photoSettingsWithFormat:"))))
            (void)method_setImplementation(m, (IMP)zcam_ps_make_arg);
    }

    avcap_policy_installed = true;
    fprintf(stderr, "Zizhan avcap18: AVCaptureSession bridge installed (%s)\n",
            getprogname() ? getprogname() : "?");
    fflush(stderr);
}

/* --- Stock Camera.app (/Applications/Camera.app) Live 960x720 3-Camera Viewfinder Overlay --- */
static bool gCamAppUiInstalled;
static id gCamAppOverlayView;
static id gCamAppSession;
static id gCamAppPhotoOut;
static id gCamAppHudLabel;
static id gCamAppDelegateObj;
static int gCamAppCurPos = 1;
static unsigned gCamAppShotSeq = 0;

static void zcam_app_update_hud(const char *extra) {
    if (!gCamAppHudLabel) return;
    const char *lens = (gCamAppCurPos == 2) ? "前置 IMX596" : (gCamAppCurPos == 4 ? "0.6x 超广角 OV13B10" : "1x 主摄 IMX766 50MP");
    char msg[256];
    snprintf(msg, sizeof(msg), "MIX Fold 2 · %s · %ux%u%s%s",
             lens, ZIZHAN_CAM_OUT_W, ZIZHAN_CAM_OUT_H,
             (extra && extra[0]) ? " | " : "", (extra && extra[0]) ? extra : "");
    ((void(*)(id, SEL, id))objc_msgSend)(gCamAppHudLabel, sel_registerName("setText:"), zizhan_nsstr(msg));
}

static void zcam_app_switch_pos(int newPos) {
    if (!gCamAppSession) return;
    zizhan_ensure_devices();
    id dev = (newPos == 2) ? gFrontDeviceSingleton : (newPos == 4 ? gUltraDeviceSingleton : gBackDeviceSingleton);
    if (!dev) return;
    gCamAppCurPos = newPos;
    id err = nil;
    id inp = zcam_make_input(dev);
    if (inp) {
        ((void(*)(id, SEL, id))objc_msgSend)(gCamAppSession, sel_registerName("addInput:"), inp);
        ((void(*)(id, SEL))objc_msgSend)(inp, sel_registerName("release"));
    }
    (void)err;
    (void)zizhan_cam_ctl_write(0x104); /* subtle selection haptic (dry-safe) */
    zcam_app_update_hud("切换镜头");
}

static void zcam_app_act_1x(id self, SEL _cmd) { (void)self; (void)_cmd; zcam_app_switch_pos(1); }
static void zcam_app_act_ultra(id self, SEL _cmd) { (void)self; (void)_cmd; zcam_app_switch_pos(4); }
static void zcam_app_act_front(id self, SEL _cmd) { (void)self; (void)_cmd; zcam_app_switch_pos(2); }
static void zcam_app_act_af(id self, SEL _cmd) {
    (void)self; (void)_cmd;
    (void)zizhan_cam_ctl_write(ZIZHAN_CAM_CMD_AF);
    (void)zizhan_cam_ctl_write(0x104);
    zcam_app_update_hud("DW9800V 自动对焦完成");
}
static void zcam_app_act_shutter(id self, SEL _cmd) {
    (void)self; (void)_cmd;
    if (!gCamAppPhotoOut || !gCamAppDelegateObj) return;
    (void)zizhan_cam_ctl_write(0x102);
    Class psetCls = objc_lookUpClass("AVCapturePhotoSettings");
    id set = psetCls ? ((id(*)(Class, SEL))objc_msgSend)(psetCls, sel_registerName("photoSettings")) : nil;
    ((void(*)(id, SEL, id, id))objc_msgSend)(
        gCamAppPhotoOut, sel_registerName("capturePhotoWithSettings:delegate:"), set, gCamAppDelegateObj);
}

static void zcam_app_did_finish_photo(id self, SEL _cmd, id outObj, id photo, id err) {
    (void)self; (void)_cmd; (void)outObj; (void)err;
    if (!photo) return;
    id jpeg = ((id(*)(id, SEL))objc_msgSend)(photo, sel_registerName("fileDataRepresentation"));
    unsigned long len = jpeg ? ((unsigned long(*)(id, SEL))objc_msgSend)(jpeg, sel_registerName("length")) : 0ul;
    if (!jpeg || len == 0) return;

    (void)mkdir("/private/var/mobile/Media", 0755);
    (void)mkdir("/private/var/mobile/Media/DCIM", 0755);
    (void)mkdir("/private/var/mobile/Media/DCIM/100APPLE", 0755);
    char path[160];
    snprintf(path, sizeof(path), "/private/var/mobile/Media/DCIM/100APPLE/IMG_%04u.JPG", ++gCamAppShotSeq);
    BOOL wrote = ((BOOL(*)(id, SEL, id, BOOL))objc_msgSend)(
        jpeg, sel_registerName("writeToFile:atomically:"), zizhan_nsstr(path), YES);

    Class imgCls = objc_lookUpClass("UIImage");
    void (*savePhotos)(id, id, SEL, void *) = (void (*)(id, id, SEL, void *))dlsym(RTLD_DEFAULT, "UIImageWriteToSavedPhotosAlbum");
    if (imgCls && savePhotos) {
        id uiImg = ((id(*)(Class, SEL, id))objc_msgSend)(imgCls, sel_registerName("imageWithData:"), jpeg);
        if (uiImg) savePhotos(uiImg, nil, NULL, NULL);
    }
    fprintf(stderr, "ZIZHAN_CAMERA_APP: SHUTTER_SAVED path=%s bytes=%lu dcim_ok=%d\n", path, len, (int)wrote);
    fflush(stderr);
    char note[96];
    snprintf(note, sizeof(note), "已保存照片 (%lu KB)", len / 1024ul);
    zcam_app_update_hud(note);
}

static void zcam_app_did_meta(id self, SEL _cmd, id outObj, id objs, id conn) {
    (void)self; (void)_cmd; (void)outObj; (void)conn;
    unsigned long n = objs ? ((unsigned long(*)(id, SEL))objc_msgSend)(objs, sel_registerName("count")) : 0ul;
    if (!n) return;
    id first = ((id(*)(id, SEL, unsigned long))objc_msgSend)(objs, sel_registerName("objectAtIndex:"), 0ul);
    id sv = first ? ((id(*)(id, SEL))objc_msgSend)(first, sel_registerName("stringValue")) : nil;
    const char *s = sv ? ((const char *(*)(id, SEL))objc_msgSend)(sv, sel_registerName("UTF8String")) : NULL;
    if (s && s[0]) {
        char buf[128];
        snprintf(buf, sizeof(buf), "QR: %.80s", s);
        zcam_app_update_hud(buf);
    }
}

static id zcam_app_make_btn(Class btnCls, Class colCls, id target, SEL action, const char *title, CGRect frame, double alpha) {
    id b = ((id(*)(Class, SEL, long))objc_msgSend)(btnCls, sel_registerName("buttonWithType:"), 1l);
    if (!b) return nil;
    ((void(*)(id, SEL, CGRect))objc_msgSend)(b, sel_registerName("setFrame:"), frame);
    ((void(*)(id, SEL, id, unsigned long))objc_msgSend)(b, sel_registerName("setTitle:forState:"), zizhan_nsstr(title), 0ul);
    id white = ((id(*)(Class, SEL))objc_msgSend)(colCls, sel_registerName("whiteColor"));
    id bg = ((id(*)(Class, SEL, double, double))objc_msgSend)(colCls, sel_registerName("colorWithWhite:alpha:"), 0.12, alpha);
    ((void(*)(id, SEL, id, unsigned long))objc_msgSend)(b, sel_registerName("setTitleColor:forState:"), white, 0ul);
    ((void(*)(id, SEL, id))objc_msgSend)(b, sel_registerName("setBackgroundColor:"), bg);
    id layer = ((id(*)(id, SEL))objc_msgSend)(b, sel_registerName("layer"));
    if (layer) ((void(*)(id, SEL, double))objc_msgSend)(layer, sel_registerName("setCornerRadius:"), frame.size.height * 0.5);
    ((void(*)(id, SEL, id, SEL, unsigned long))objc_msgSend)(b, sel_registerName("addTarget:action:forControlEvents:"), target, action, 1ul << 6);
    return b;
}

static void zcam_app_try_attach_window(void *unused) {
    (void)unused;
    if (gCamAppOverlayView) return;
    Class appCls = objc_lookUpClass("UIApplication");
    Class winCls = objc_lookUpClass("UIWindow");
    Class viewCls = objc_lookUpClass("UIView");
    Class lblCls = objc_lookUpClass("UILabel");
    Class btnCls = objc_lookUpClass("UIButton");
    Class colCls = objc_lookUpClass("UIColor");
    if (!appCls || !winCls || !viewCls || !lblCls || !btnCls || !colCls) return;

    id app = ((id(*)(Class, SEL))objc_msgSend)(appCls, sel_registerName("sharedApplication"));
    SEL sAllW = sel_registerName("allWindowsIncludingInternalWindows:onlyVisibleWindows:");
    id wins = (app && ((BOOL(*)(Class, SEL, SEL))objc_msgSend)(winCls, sel_registerName("respondsToSelector:"), sAllW))
              ? ((id(*)(Class, SEL, BOOL, BOOL))objc_msgSend)(winCls, sAllW, NO, YES)
              : (app ? ((id(*)(id, SEL))objc_msgSend)(app, sel_registerName("windows")) : nil);
    unsigned long nw = wins ? ((unsigned long(*)(id, SEL))objc_msgSend)(wins, sel_registerName("count")) : 0ul;
    id targetWin = nil;
    for (unsigned long i = 0; i < nw; ++i) {
        id w = ((id(*)(id, SEL, unsigned long))objc_msgSend)(wins, sel_registerName("objectAtIndex:"), i);
        CGRect b = ((CGRect(*)(id, SEL))objc_msgSend)(w, sel_registerName("bounds"));
        if (b.size.width >= 320.0 && b.size.height >= 320.0) { targetWin = w; break; }
    }
    if (!targetWin) {
        static unsigned retries;
        if (++retries < 16) {
            dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW, 800 * NSEC_PER_MSEC),
                             dispatch_get_main_queue(), NULL, zcam_app_try_attach_window);
        }
        return;
    }

    if (!gCamAppDelegateObj) {
        Class dcls = objc_allocateClassPair(objc_lookUpClass("NSObject"), "PongZizhanCameraAppHandler18", 0);
        if (dcls) {
            class_addMethod(dcls, sel_registerName("act1x"), (IMP)zcam_app_act_1x, "v@:");
            class_addMethod(dcls, sel_registerName("actUltra"), (IMP)zcam_app_act_ultra, "v@:");
            class_addMethod(dcls, sel_registerName("actFront"), (IMP)zcam_app_act_front, "v@:");
            class_addMethod(dcls, sel_registerName("actAf"), (IMP)zcam_app_act_af, "v@:");
            class_addMethod(dcls, sel_registerName("actShutter"), (IMP)zcam_app_act_shutter, "v@:");
            class_addMethod(dcls, sel_registerName("captureOutput:didFinishProcessingPhoto:error:"), (IMP)zcam_app_did_finish_photo, "v@:@@@");
            class_addMethod(dcls, sel_registerName("captureOutput:didOutputMetadataObjects:fromConnection:"), (IMP)zcam_app_did_meta, "v@:@@@");
            objc_registerClassPair(dcls);
            gCamAppDelegateObj = ((id(*)(Class, SEL))objc_msgSend)(dcls, sel_registerName("new"));
        }
    }
    if (!gCamAppDelegateObj) return;

    CGRect wb = ((CGRect(*)(id, SEL))objc_msgSend)(targetWin, sel_registerName("bounds"));
    id root = ((id(*)(id, SEL, CGRect))objc_msgSend)(
        ((id(*)(Class, SEL))objc_msgSend)(viewCls, sel_registerName("alloc")),
        sel_registerName("initWithFrame:"), wb);
    ((void(*)(id, SEL, unsigned long))objc_msgSend)(root, sel_registerName("setAutoresizingMask:"), 18ul); /* W | H */
    id black = ((id(*)(Class, SEL))objc_msgSend)(colCls, sel_registerName("blackColor"));
    ((void(*)(id, SEL, id))objc_msgSend)(root, sel_registerName("setBackgroundColor:"), black);

    /* Build AVCaptureSession + 1x IMX766 input + PhotoOutput + MetadataOutput + PreviewLayer */
    zizhan_ensure_devices();
    Class sessCls = objc_lookUpClass("AVCaptureSession");
    Class poutCls = objc_lookUpClass("AVCapturePhotoOutput");
    Class moutCls = objc_lookUpClass("AVCaptureMetadataOutput");
    Class prevCls = objc_lookUpClass("AVCaptureVideoPreviewLayer");
    gCamAppSession = ((id(*)(Class, SEL))objc_msgSend)(sessCls, sel_registerName("new"));
    id inp = zcam_make_input(gBackDeviceSingleton);
    if (inp) {
        ((void(*)(id, SEL, id))objc_msgSend)(gCamAppSession, sel_registerName("addInput:"), inp);
        ((void(*)(id, SEL))objc_msgSend)(inp, sel_registerName("release"));
    }
    gCamAppPhotoOut = poutCls ? ((id(*)(Class, SEL))objc_msgSend)(poutCls, sel_registerName("new")) : nil;
    if (gCamAppPhotoOut) ((void(*)(id, SEL, id))objc_msgSend)(gCamAppSession, sel_registerName("addOutput:"), gCamAppPhotoOut);
    id mout = moutCls ? ((id(*)(Class, SEL))objc_msgSend)(moutCls, sel_registerName("new")) : nil;
    if (mout) {
        ((void(*)(id, SEL, id, dispatch_queue_t))objc_msgSend)(
            mout, sel_registerName("setMetadataObjectsDelegate:queue:"), gCamAppDelegateObj, dispatch_get_main_queue());
        ((void(*)(id, SEL, id))objc_msgSend)(gCamAppSession, sel_registerName("addOutput:"), mout);
    }

    id prevLayer = ((id(*)(Class, SEL, id))objc_msgSend)(prevCls, sel_registerName("layerWithSession:"), gCamAppSession);
    if (prevLayer) {
        CGRect pf = CGRectMake(20.0, 68.0, wb.size.width - 40.0, wb.size.height - 196.0);
        ((void(*)(id, SEL, CGRect))objc_msgSend)(prevLayer, sel_registerName("setFrame:"), pf);
        ((void(*)(id, SEL, id))objc_msgSend)(prevLayer, sel_registerName("setContentsGravity:"), zizhan_nsstr("resizeAspect"));
        id rlayer = ((id(*)(id, SEL))objc_msgSend)(root, sel_registerName("layer"));
        ((void(*)(id, SEL, id))objc_msgSend)(rlayer, sel_registerName("addSublayer:"), prevLayer);
    }

    gCamAppHudLabel = ((id(*)(id, SEL, CGRect))objc_msgSend)(
        ((id(*)(Class, SEL))objc_msgSend)(lblCls, sel_registerName("alloc")),
        sel_registerName("initWithFrame:"), CGRectMake(24.0, 26.0, wb.size.width - 48.0, 34.0));
    id white = ((id(*)(Class, SEL))objc_msgSend)(colCls, sel_registerName("whiteColor"));
    ((void(*)(id, SEL, id))objc_msgSend)(gCamAppHudLabel, sel_registerName("setTextColor:"), white);
    ((void(*)(id, SEL, long))objc_msgSend)(gCamAppHudLabel, sel_registerName("setTextAlignment:"), 1l);
    zcam_app_update_hud("实时光学预览就绪");
    ((void(*)(id, SEL, id))objc_msgSend)(root, sel_registerName("addSubview:"), gCamAppHudLabel);

    const double cy = wb.size.height - 110.0;
    const double mid = wb.size.width * 0.5;
    id b06 = zcam_app_make_btn(btnCls, colCls, gCamAppDelegateObj, sel_registerName("actUltra"), "0.6x 超广角", CGRectMake(mid - 230.0, cy, 104.0, 38.0), 0.78);
    id b1x = zcam_app_make_btn(btnCls, colCls, gCamAppDelegateObj, sel_registerName("act1x"), "1x 主摄", CGRectMake(mid - 116.0, cy, 96.0, 38.0), 0.88);
    id bfr = zcam_app_make_btn(btnCls, colCls, gCamAppDelegateObj, sel_registerName("actFront"), "前置", CGRectMake(mid - 10.0, cy, 88.0, 38.0), 0.78);
    id baf = zcam_app_make_btn(btnCls, colCls, gCamAppDelegateObj, sel_registerName("actAf"), "AF 对焦", CGRectMake(mid + 88.0, cy, 108.0, 38.0), 0.78);
    id bsh = zcam_app_make_btn(btnCls, colCls, gCamAppDelegateObj, sel_registerName("actShutter"), "拍照保存", CGRectMake(mid - 72.0, wb.size.height - 62.0, 144.0, 48.0), 0.92);
    if (b06) ((void(*)(id, SEL, id))objc_msgSend)(root, sel_registerName("addSubview:"), b06);
    if (b1x) ((void(*)(id, SEL, id))objc_msgSend)(root, sel_registerName("addSubview:"), b1x);
    if (bfr) ((void(*)(id, SEL, id))objc_msgSend)(root, sel_registerName("addSubview:"), bfr);
    if (baf) ((void(*)(id, SEL, id))objc_msgSend)(root, sel_registerName("addSubview:"), baf);
    if (bsh) ((void(*)(id, SEL, id))objc_msgSend)(root, sel_registerName("addSubview:"), bsh);

    ((void(*)(id, SEL, id))objc_msgSend)(targetWin, sel_registerName("addSubview:"), root);
    gCamAppOverlayView = root;
    ((void(*)(id, SEL))objc_msgSend)(gCamAppSession, sel_registerName("startRunning"));
    fprintf(stderr, "ZIZHAN_CAMERA_APP: READY %ux%u lenses=3 window=%.0fx%.0f\n",
            ZIZHAN_CAM_OUT_W, ZIZHAN_CAM_OUT_H, wb.size.width, wb.size.height);
    fflush(stderr);

    if (boot_option("zizhan_cam_autoshutter=1")) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 3500 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
            zcam_app_act_af(nil, NULL);
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 900 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                zcam_app_act_shutter(nil, NULL);
            });
        });
    }
}

static void zizhan_camera_app_bridge_init(void) {
    if (gCamAppUiInstalled || boot_option("zizhan_camera_app_ui=0")) return;
    const char *prog = getprogname();
    if (!prog || strcmp(prog, "Camera") != 0) return;
    gCamAppUiInstalled = true;
    dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW, 1200 * NSEC_PER_MSEC),
                     dispatch_get_main_queue(), NULL, zcam_app_try_attach_window);
}

static void zizhan_avcapture_image(const struct mach_header *header, intptr_t slide) {
    (void)header; (void)slide;
    zizhan_ci_sw_policy();
    zizhan_avcapture_policy();
    zizhan_camera_app_bridge_init();
}

#endif /* ZIZHAN_AVCAPTURE18_H */
