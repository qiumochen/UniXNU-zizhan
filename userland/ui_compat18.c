/* SPDX-License-Identifier: MIT
 * iPadOS 18 runtime adapter. No 20G81 QuartzCore/Settings addresses or vtables.
 * Device input reuses the actual Pong kernel event queue and runtime HID APIs.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <AudioToolbox/AudioQueue.h>
#include <dispatch/dispatch.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <libkern/OSCacheControl.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <math.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#include <sys/ucontext.h>
#include <unistd.h>
#include <notify.h>
#include "touch.h"
#include "zizhan-bringup18.h"
#include "zizhan-app-diagnostic18.h"
#include "zizhan-network-observer18.h"
#include "zizhan-weather-observer18.h"
#include "zizhan-statusbar18.h"
#include "zizhan-scene-orient18.h"
#include "zizhan-cllocation18.h"
#include "zizhan-ipa-install18.h"

static bool software_composition;
static bool scanout_format;
static bool renderer_installed;
static bool format_installed;
static bool personas_installed;
static bool springboard_process;
static bool phone_ui_process;
static bool photos_process;
/* Processes whose MobileGestalt answers this file adjusts: the phone UI processes and the render server (backboardd). */
static bool gestalt_process;
static bool skip_setup;
static IMP setup_reason_original;
static bool activation_installed;
static ptrdiff_t activation_offset;
static bool launchservices_process, launchservices_installed;
static bool installd_process, installd_directory_installed;
static bool pluginkit_process, pluginkit_directory_attempted;
static CFURLRef ls_base, ls_system, ls_user, ls_shared;
static CFURLRef installd_library;
static CFURLRef pluginkit_container;
static IMP battery_original;
static struct {Class cls;IMP original;} statusbar_factories[5];
static unsigned statusbar_factory_count;
static Class statusbar_provider,zizhan_pad_provider;
static bool statusbar_fallback_bridge_installed;
static bool statusbar_diagnostic_item_removed;
static char statusbar_delegate_key;
static char statusbar_rebuild_key;
static IMP statusbar_fallback_get,statusbar_fallback_set,statusbar_fallback_responds,statusbar_fallback_setup_original;
static Class (*phone_selector_original)(id,id);
static bool phone_selector_installed;
static IMP phone_prepare_original;
static Ivar phone_provider_ivar;
static IMP phone_layout_original;
static bool (*phone_gestalt_bool_original)(CFStringRef);
static CFMutableDictionaryRef (*service_matching)(const char *);
static uint32_t (*matching_service)(uint32_t,CFDictionaryRef);
static int (*release_service)(uint32_t);
static CFURLRef container_root, container_library;
static void launchservices_directories(void);
static void installd_directory(void);
static void installd_codesign_policy(void);
static void pluginkit_directory(void);
static void phone_selector_image(const struct mach_header *,intptr_t);
static void phone_gestalt_image(const struct mach_header *,intptr_t);
static void phone_repair_actual_bar(void *);
static void stui_repair_bar(id);
static bool boot_option(const char *option);
static bool own_directory(const char *path);
void pong_boot_snapshot_start(const char *name);
void pong_settings18_start(void);

static char abi_type_kind(const char *type)
{
    /* Objective-C qualifiers describe ownership/direction, not the register
     * ABI. Darwin24 declares NSError ** as o^@; checking its first byte
     * against '^' silently disabled the PosterBoard storage adapter. */
    if(!type)return 0;
    while(*type && strchr("rnNoORV",*type))type++;
    return *type;
}
static bool method_abi(Method method,const char *result,unsigned argc,const char *arguments)
{
    if(!method || method_getNumberOfArguments(method)!=argc+2)return false;
    char *type=method_copyReturnType(method);
    char kind=abi_type_kind(type);
    bool valid=kind && strchr(result,kind);
    free(type);
    for(unsigned i=0;valid && i<argc;i++) {
        type=method_copyArgumentType(method,i+2);
        valid=abi_type_kind(type)==arguments[i];
        free(type);
    }
    return valid;
}

/* The adapted launch cache has one canonical LaunchDaemons directory. That
 * loses launchd's Angel classification for PosterBoard: RunningBoard chose
 * the SystemXPCService 6 MiB defaults instead of its stock Angel policy.
 * Use an identity view ONLY for this synchronous policy lookup. The original
 * identity, bundle, PID, entitlements and service registration stay native.
 * RunningBoard still constructs its own limits from the matching board plist.
 */
static IMP poster_jetsam_original;
static Class poster_jetsam_identity;
static char poster_jetsam_source_key;
static id poster_jetsam_source(id self)
{ return objc_getAssociatedObject(self,&poster_jetsam_source_key); }
static unsigned char poster_jetsam_service_type(id self,SEL selector)
{ (void)self;(void)selector;return 3; /* native osServiceType: Angel */ }
static BOOL poster_jetsam_is_application(id self,SEL selector)
{ (void)self;(void)selector;return NO; }
static id poster_jetsam_label(id self,SEL selector)
{ return ((id(*)(id,SEL))objc_msgSend)(poster_jetsam_source(self),selector); }
static id poster_jetsam_forward(id self,SEL selector,SEL query)
{ (void)selector;(void)query;return poster_jetsam_source(self); }
static BOOL poster_jetsam_responds(id self,SEL selector,SEL query)
{ (void)selector;return ((BOOL(*)(id,SEL,SEL))objc_msgSend)(poster_jetsam_source(self),sel_registerName("respondsToSelector:"),query); }
static id poster_jetsam_properties(id self,SEL selector,int pid,id identity,id bundle,BOOL platform)
{
    SEL label_query=sel_registerName("consistentLaunchdJobLabel");
    Method label=identity?class_getInstanceMethod(object_getClass(identity),label_query):NULL;
    if(method_abi(label,"@",0,"")) {
        id value=((id(*)(id,SEL))objc_msgSend)(identity,label_query);
        if(value && CFEqual((CFTypeRef)value,CFSTR("com.apple.PosterBoard"))) {
            id view=((id(*)(id,SEL))objc_msgSend)((id)poster_jetsam_identity,sel_registerName("new"));
            if(view) {
                objc_setAssociatedObject(view,&poster_jetsam_source_key,identity,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
                id result=((id(*)(id,SEL,int,id,id,BOOL))poster_jetsam_original)(self,selector,pid,view,bundle,platform);
                ((void(*)(id,SEL))objc_msgSend)(view,sel_registerName("release"));
                static unsigned reported;
                if(__atomic_fetch_add(&reported,1,__ATOMIC_RELAXED)<3)
                    fprintf(stderr,"Pong Poster18: native Angel memory policy selected for PosterBoard\n");
                return result;
            }
        }
    }
    return ((id(*)(id,SEL,int,id,id,BOOL))poster_jetsam_original)(self,selector,pid,identity,bundle,platform);
}
static void poster_jetsam_image(const struct mach_header *header,intptr_t slide)
{
    (void)header;(void)slide;
    if(poster_jetsam_original)return;
    Class manager=objc_lookUpClass("RBJetsamPropertyManager");
    Class identity=objc_lookUpClass("RBSProcessIdentity");
    SEL query=sel_registerName("jetsamPropertiesForProcess:identity:bundleProperties:isPlatformBinary:");
    Method method=manager?class_getInstanceMethod(manager,query):NULL;
    Method type=identity?class_getInstanceMethod(identity,sel_registerName("osServiceType")):NULL;
    if(!method_abi(method,"@",4,"i@@B") || !method_abi(type,"C",0,""))return;
    Class view=objc_allocateClassPair(objc_lookUpClass("NSObject"),"PongPosterJetsamIdentity18",0);
    if(!view)return;
    if(!class_addMethod(view,sel_registerName("osServiceType"),(IMP)poster_jetsam_service_type,method_getTypeEncoding(type)) ||
       !class_addMethod(view,sel_registerName("isApplication"),(IMP)poster_jetsam_is_application,"B16@0:8") ||
       !class_addMethod(view,sel_registerName("consistentLaunchdJobLabel"),(IMP)poster_jetsam_label,"@16@0:8") ||
       !class_addMethod(view,sel_registerName("forwardingTargetForSelector:"),(IMP)poster_jetsam_forward,"@24@0:8:16") ||
       !class_addMethod(view,sel_registerName("respondsToSelector:"),(IMP)poster_jetsam_responds,"B24@0:8:16")) {
        objc_disposeClassPair(view);return;
    }
    objc_registerClassPair(view);poster_jetsam_identity=view;
    poster_jetsam_original=method_setImplementation(method,(IMP)poster_jetsam_properties);
}

/* Phone identity still uses Pong's real screen. The native provider factory
 * falls back to a diagnostic label for an unlisted width/height tuple. Reuse
 * its real 1080px phone layout only for this main display, with the factory's
 * normal initialization and the supplied 22H374 Objective-C signatures. */
static double phone_reference_scale(id self,SEL selector)
{
    (void)self;(void)selector;
    /* This class method also runs during UIKit's screen/provider discovery.
     * Reentering +mainScreen here can wait on that same initialization. Pong's
     * ADT and framebuffer use scale 3; no UIKit calls are needed to report it. */
    return 3.0;
}
/* 22H374 can retain a diagnostic provider selected before +load completes.
 * Give that provider a real, separately allocated phone layout. Its 24-byte
 * instance cannot be changed into a 240-byte DynamicSplit object in place.
 * All regions, placements, clocks and battery views still come from UIKit;
 * none are drawn by this adapter and no hardware values are synthesized. */
static id phone_fallback_delegate(id self)
{
    id delegate=objc_getAssociatedObject(self,&statusbar_delegate_key);
    if(delegate || !statusbar_provider)return delegate;
    delegate=((id(*)(id,SEL))objc_msgSend)((id)statusbar_provider,sel_registerName("new"));
    if(!delegate)return nil;
    id bar=((id(*)(id,SEL))statusbar_fallback_get)(self,sel_registerName("statusBar"));
    ((void(*)(id,SEL,id))objc_msgSend)(delegate,sel_registerName("setStatusBar:"),bar);
    objc_setAssociatedObject(self,&statusbar_delegate_key,delegate,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    ((void(*)(id,SEL))objc_msgSend)(delegate,sel_registerName("release"));
    // This weak statusBar link also finds server-owned bars absent from
    // UIApplication.windows. Rebuild once after the current native update
    // unwinds; swapping an in-use provider synchronously is unsafe.
    if(bar && !objc_getAssociatedObject(bar,&statusbar_rebuild_key)) {
        objc_setAssociatedObject(bar,&statusbar_rebuild_key,(id)kCFBooleanTrue,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        id retained=((id(*)(id,SEL))objc_msgSend)(bar,sel_registerName("retain"));
        dispatch_async_f(dispatch_get_main_queue(),retained,phone_repair_actual_bar);
    }
    return objc_getAssociatedObject(self,&statusbar_delegate_key);
}
static void phone_fallback_set_bar(id self,SEL selector,id bar)
{
    ((void(*)(id,SEL,id))statusbar_fallback_set)(self,selector,bar);
    id delegate=phone_fallback_delegate(self);
    if(delegate)((void(*)(id,SEL,id))objc_msgSend)(delegate,selector,bar);
}
static id phone_fallback_setup(id self,SEL selector,id container)
{
    id delegate=phone_fallback_delegate(self);
    if(!delegate)return ((id(*)(id,SEL,id))statusbar_fallback_setup_original)(self,selector,container);
    id regions=((id(*)(id,SEL,id))objc_msgSend)(delegate,selector,container);
    static unsigned reported;
    if(__atomic_fetch_add(&reported,1,__ATOMIC_RELAXED)<3)
        fprintf(stderr,"Pong UI18: diagnostic provider replaced by native phone regions\n");
    return regions;
}
static id phone_fallback_placements(id self,SEL selector,id region)
{ return ((id(*)(id,SEL,id))objc_msgSend)(phone_fallback_delegate(self),selector,region); }
static id phone_fallback_style(id self,SEL selector,long long style)
{ return ((id(*)(id,SEL,long long))objc_msgSend)(phone_fallback_delegate(self),selector,style); }
static id phone_fallback_clock(id self,SEL selector)
{ return ((id(*)(id,SEL))objc_msgSend)(phone_fallback_delegate(self),selector); }
static BOOL phone_fallback_responds(id self,SEL selector,SEL query)
{
    if(((BOOL(*)(id,SEL,SEL))statusbar_fallback_responds)(self,selector,query))return YES;
    return ((BOOL(*)(id,SEL,SEL))objc_msgSend)(phone_fallback_delegate(self),selector,query);
}
static id phone_fallback_forward(id self,SEL selector,SEL query)
{
    (void)selector;
    id delegate=phone_fallback_delegate(self);
    return delegate && ((BOOL(*)(id,SEL,SEL))objc_msgSend)(delegate,sel_registerName("respondsToSelector:"),query)?delegate:nil;
}
static BOOL phone_fallback_class_flag(id self,SEL selector)
{ (void)self;return ((BOOL(*)(id,SEL))objc_msgSend)((id)statusbar_provider,selector); }
static CGSize phone_fallback_class_size(id self,SEL selector,long long orientation)
{ (void)self;return ((CGSize(*)(id,SEL,long long))objc_msgSend)((id)statusbar_provider,selector,orientation); }
static void phone_fallback_policy(void)
{
    if(statusbar_fallback_bridge_installed || !statusbar_provider)return;
    Class fallback=objc_lookUpClass("_UIStatusBarVisualProvider_Fallback");
    const char *names[]={"statusBar","setStatusBar:","setupInContainerView:",
        "orderedDisplayItemPlacementsInRegionWithIdentifier:","styleAttributesForStyle:","clockFont"};
    const char *results[]={"@","v","@","@","@","@"};
    const unsigned counts[]={0,1,1,1,1,0};
    const char *args[]={"","@","@","@","q",""};
    Method methods[6];
    for(unsigned i=0;i<6;i++) {
        SEL sel=sel_registerName(names[i]);
        methods[i]=fallback?class_getInstanceMethod(fallback,sel):NULL;
        if(!method_abi(methods[i],results[i],counts[i],args[i]) ||
           !method_abi(class_getInstanceMethod(statusbar_provider,sel),results[i],counts[i],args[i]))return;
    }
    SEL responds=sel_registerName("respondsToSelector:"),forward=sel_registerName("forwardingTargetForSelector:");
    Method rm=class_getInstanceMethod(fallback,responds),fm=class_getInstanceMethod(fallback,forward);
    if(!method_abi(rm,"Bc",1,":") || !method_abi(fm,"@",1,":"))return;
    Class meta=object_getClass((id)fallback);
    const char *flags[]={"scalesWithScreenNativeScale","requiresIterativeOverflowLayout"};
    for(unsigned i=0;i<2;i++)if(!method_abi(class_getClassMethod(fallback,sel_registerName(flags[i])),"Bc",0,"") ||
        !method_abi(class_getClassMethod(statusbar_provider,sel_registerName(flags[i])),"Bc",0,""))return;
    SEL size=sel_registerName("intrinsicContentSizeForOrientation:");
    Method sm=class_getClassMethod(fallback,size),ps=class_getClassMethod(statusbar_provider,size);
    if(!method_abi(sm,"{",1,"q") || !method_abi(ps,"{",1,"q"))return;
    statusbar_fallback_get=method_getImplementation(methods[0]);
    statusbar_fallback_set=method_setImplementation(methods[1],(IMP)phone_fallback_set_bar);
    statusbar_fallback_setup_original=method_setImplementation(methods[2],(IMP)phone_fallback_setup);
    method_setImplementation(methods[3],(IMP)phone_fallback_placements);
    method_setImplementation(methods[4],(IMP)phone_fallback_style);
    method_setImplementation(methods[5],(IMP)phone_fallback_clock);
    statusbar_fallback_responds=method_getImplementation(rm);
    class_addMethod(fallback,responds,(IMP)phone_fallback_responds,method_getTypeEncoding(rm));
    class_addMethod(fallback,forward,(IMP)phone_fallback_forward,method_getTypeEncoding(fm));
    for(unsigned i=0;i<2;i++)method_setImplementation(class_getClassMethod(fallback,sel_registerName(flags[i])),(IMP)phone_fallback_class_flag);
    method_setImplementation(sm,(IMP)phone_fallback_class_size);
    class_addMethod(meta,sel_registerName("referenceScreenScale"),(IMP)phone_reference_scale,"d16@0:8");
    statusbar_fallback_bridge_installed=true;
    fprintf(stderr,"Pong UI18: rdar diagnostic layout removed; native clock/battery layout connected\n");
}
static BOOL phone_diagnostic_item_enabled(id self,SEL selector,id item,id data)
{ (void)self;(void)selector;(void)item;(void)data;return NO; }
static void phone_remove_diagnostic_item(void)
{
    if(statusbar_diagnostic_item_removed)return;
    Class cls=objc_lookUpClass("_UIStatusBarFallbackItem");
    Method method=cls?class_getInstanceMethod(cls,sel_registerName("canEnableDisplayItem:fromData:")):NULL;
    if(!method_abi(method,"Bc",2,"@@"))return;
    method_setImplementation(method,(IMP)phone_diagnostic_item_enabled);
    statusbar_diagnostic_item_removed=true;
}
/* zizhan has an unfolded2160x1916 panel atscale2. A nil-screen selector
 * call previously inherited Pong's phone/defaultscale3 candidate. Select
 * the OS's full-width Pad provider instead of moving battery subviews or
 * substituting battery values. Actualpanel gate and explicit=0 rollback;
 * non-zizhan screens keep their original factory/provider result. */
static bool zizhan_statusbar_enabled(void)
{ return !boot_option("zizhan_statusbar=0"); }
static bool zizhan_statusbar_screen(id screen)
{
    if(!zizhan_statusbar_enabled())return false;
    static bool board;
    if(!board) {
        CFMutableDictionaryRef (*matching)(const char *)=dlsym(RTLD_DEFAULT,"IOServiceMatching");
        uint32_t (*get)(uint32_t,CFDictionaryRef)=dlsym(RTLD_DEFAULT,"IOServiceGetMatchingService");
        int (*release)(uint32_t)=dlsym(RTLD_DEFAULT,"IOObjectRelease");
        if(!matching||!get||!release)return false;
        CFMutableDictionaryRef dict=matching("IOService");
        const void *keys[]={CFSTR("PongPanel"),CFSTR("backlight-control")};
        const void *values[]={CFSTR("zizhan L18 primary / SM8475 DSI0"),kCFBooleanTrue};
        CFDictionaryRef properties=CFDictionaryCreate(NULL,keys,values,2,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
        if(!dict||!properties){if(dict)CFRelease(dict);if(properties)CFRelease(properties);return false;}
        CFDictionarySetValue(dict,CFSTR("IOPropertyMatch"),properties);CFRelease(properties);
        uint32_t service=get(0,dict);if(!service)return false;release(service);board=true;
    }
    CGRect bounds=CGRectZero;double scale=0;
    if(screen) {
        bounds=((CGRect(*)(id,SEL))objc_msgSend)(screen,sel_registerName("nativeBounds"));
        scale=((double(*)(id,SEL))objc_msgSend)(screen,sel_registerName("nativeScale"));
    }
    return zizhan_statusbar_native_pad(1,board,screen!=nil,bounds.size.width,bounds.size.height,scale);
}
static double zizhan_statusbar_scale(id self,SEL selector)
{ (void)self;(void)selector;return 2.0; }
static Class zizhan_statusbar_provider(const char *base_name,const char *name)
{
    if(!zizhan_statusbar_enabled())return Nil;
    Class base=objc_lookUpClass(base_name);if(!base)return Nil;
    const char *names[]={"init","setupInContainerView:","sizeUpdatedFromSize:"};
    if(!method_abi(class_getInstanceMethod(base,sel_registerName(names[0])),"@",0,"") ||
       !method_abi(class_getInstanceMethod(base,sel_registerName(names[1])),"@",1,"@"))return Nil;
    Class result=objc_allocateClassPair(base,name,0);if(!result)return Nil;
    if(!class_addMethod(object_getClass((id)result),sel_registerName("referenceScreenScale"),(IMP)zizhan_statusbar_scale,"d16@0:8")) {
        objc_disposeClassPair(result);return Nil;
    }
    objc_registerClassPair(result);return result;
}
static void zizhan_statusbar_geometry(id view,unsigned depth,unsigned *remaining)
{
    if(!view||!depth||!*remaining)return;--*remaining;
    const char *name=object_getClassName(view);
    if(name&&(strstr(name,"StatusBar")||strstr(name,"Battery"))) {
        Method frame=class_getInstanceMethod(object_getClass(view),sel_registerName("frame"));
        Method bounds=class_getInstanceMethod(object_getClass(view),sel_registerName("bounds"));
        if(method_abi(frame,"{",0,"")&&method_abi(bounds,"{",0,"")) {
            CGRect f=((CGRect(*)(id,SEL))objc_msgSend)(view,sel_registerName("frame"));
            CGRect b=((CGRect(*)(id,SEL))objc_msgSend)(view,sel_registerName("bounds"));
            fprintf(stderr,"Zizhan statusbar18: class=%s frame=%.2f/%.2f/%.2f/%.2f bounds=%.2f/%.2f/%.2f/%.2f\n",name,f.origin.x,f.origin.y,f.size.width,f.size.height,b.origin.x,b.origin.y,b.size.width,b.size.height);
        }
    }
    id children=((id(*)(id,SEL))objc_msgSend)(view,sel_registerName("subviews"));
    if(!children)return;CFIndex count=CFArrayGetCount((CFArrayRef)children);
    for(CFIndex i=0;i<count&&*remaining;++i)zizhan_statusbar_geometry((id)CFArrayGetValueAtIndex((CFArrayRef)children,i),depth-1,remaining);
}
static void zizhan_statusbar_late_geometry(void *context)
{
    id view=(id)context;unsigned budget=256;
    fprintf(stderr,"Zizhan statusbar18: late_geometry begin budget=256 readonly=1\n");
    zizhan_statusbar_geometry(view,16,&budget);
    fprintf(stderr,"Zizhan statusbar18: late_geometry end visited=%u\n",256-budget);
    ((void(*)(id,SEL))objc_msgSend)(view,sel_registerName("release"));
}
static Class phone_statusbar(unsigned index,id self,SEL selector,id screen,id info)
{
    if(zizhan_pad_provider&&zizhan_statusbar_screen(screen))return zizhan_pad_provider;
    /* Resolve the known Pong screen before entering the native candidate
     * enumerator, matching the order used before services42. */
    // UIKit uses nil for its default display during screen construction.
    // This adapter is already restricted to Pong; do not reenter mainScreen.
    if(!screen && statusbar_provider)return statusbar_provider;
    if(screen && statusbar_provider) {
        CGRect bounds=((CGRect(*)(id,SEL))objc_msgSend)(screen,sel_registerName("nativeBounds"));
        double scale=((double(*)(id,SEL))objc_msgSend)(screen,sel_registerName("nativeScale"));
        if(((bounds.size.width==1080 && bounds.size.height==2412) ||
            (bounds.size.height==1080 && bounds.size.width==2412)) && scale>0 && scale<=4) {
            static unsigned reported;
            if(__atomic_fetch_add(&reported,1,__ATOMIC_RELAXED)<3)
                fprintf(stderr,"Pong UI18: actual 1080x2412 phone status bar selected\n");
            return statusbar_provider;
        }
    }
    return ((Class(*)(id,SEL,id,id))statusbar_factories[index].original)(self,selector,screen,info);
}
/* Darwin24 compares a candidate's IMP with its parent's IMP. Each factory
 * must have a distinct wrapper, even though the Pong screen policy is shared. */
#define PHONE_FACTORY(N) static Class phone_factory##N(id s,SEL c,id screen,id info) \
    { return phone_statusbar(N,s,c,screen,info); }
PHONE_FACTORY(0)
PHONE_FACTORY(1)
PHONE_FACTORY(2)
PHONE_FACTORY(3)
#undef PHONE_FACTORY
static void phone_presentation(void)
{
    if(!phone_ui_process || statusbar_factory_count)return;
    char product[64]={0};size_t size=sizeof(product);
    if(sysctlbyname("hw.product",product,&size,NULL,0) || strcmp(product,"iPhone17,1"))return;
    phone_remove_diagnostic_item();
    zizhan_pad_provider=zizhan_statusbar_provider("_UIStatusBarVisualProvider_Pad","ZizhanStatusBar18InnerPad");
    Class base=objc_lookUpClass("_UIStatusBarVisualProvider_DynamicSplitMedium");
    if(!base)base=objc_lookUpClass("_UIStatusBarVisualProvider_Split1080");
    if(!base)return;
    // DynamicSplitMedium does not declare referenceScreenScale in 22H374.
    // It is an optional class query, which this concrete Pong subclass adds.
    // Requiring it on the base silently skipped the entire provider adapter.
    Class provider=objc_allocateClassPair(base,"PongStatusBar18Phone1080",0);
    if(!provider)return;
    if(!class_addMethod(object_getClass((id)provider),sel_registerName("referenceScreenScale"),(IMP)phone_reference_scale,"d16@0:8")) {
        objc_disposeClassPair(provider);return;
    }
    objc_registerClassPair(provider);statusbar_provider=provider;
    phone_fallback_policy();
    // Keep _prepareVisualProviderIfNeeded's native class selection branch:
    // it initializes scale/reference/overflow flags before provider init.
    // Darwin24's C selector compares each subclass IMP with its parent.
    // Hooking every class to the same IMP makes it discard all candidates.
    // Keep the root implementation intact and use distinct child IMPs.
    const char *names[]={"_UIStatusBarVisualProvider_iOS","_UIStatusBarVisualProvider_Phone",
        "_UIStatusBarVisualProvider_Split","_UIStatusBarVisualProvider_DynamicSplit"};
    const IMP replacements[]={(IMP)phone_factory0,(IMP)phone_factory1,(IMP)phone_factory2,(IMP)phone_factory3};
    SEL selector=sel_registerName("visualProviderSubclassForScreen:visualProviderInfo:");
    /* services43 rendered with only the iOS factory adapted. The broader
     * services44 overrides ran before any scanout and never completed the
     * main-queue wake. Keep child factory discovery native during boot;
     * the extended candidate policy remains an explicit development option. */
    unsigned count=boot_option("pong_statusbar_factories=1")?4:1;
    for(unsigned i=0;i<count;i++) {
        Class factory=objc_lookUpClass(names[i]);
        Method method=factory?class_getClassMethod(factory,selector):NULL;
        if(!method_abi(method,"#",2,"@@"))continue;
        // Do not mutate an inherited method through a child class handle.
        unsigned n=0;Method *own=class_copyMethodList(object_getClass((id)factory),&n);
        bool declared=false;
        for(unsigned j=0;j<n;j++)if(method_getName(own[j])==selector)declared=true;
        free(own);if(!declared)continue;
        statusbar_factories[i].cls=factory;
        statusbar_factories[i].original=method_setImplementation(method,replacements[i]);
        statusbar_factory_count++;
    }
    fprintf(stderr,"Pong UI18: native phone status bar factories installed=%u\n",
        statusbar_factory_count);
}

/* This restore supplies iPad artwork. Use 22H374 WallpaperKit's own
 * compatibility selectors for its real catalogs on Pong, rather than clone
 * collection UUIDs or change the user's selected iPhone identity. */
static bool wallpaper_process,wallpaper_paths_process,wallpaper_policy_installed;
static BOOL (*wallpaper_original_collection)(id,SEL,id);
static BOOL wallpaper_collection_path(id self,SEL selector,id url)
{
    if(!wallpaper_original_collection(self,selector,url))return NO;
    if(!url || CFGetTypeID((CFTypeRef)url)!=CFURLGetTypeID())return YES;
    CFStringRef path=CFURLCopyFileSystemPath((CFURLRef)url,kCFURLPOSIXPathStyle);
    char original[PATH_MAX],phone[PATH_MAX];
    bool usable=path && CFStringGetCString(path,original,sizeof original,kCFStringEncodingUTF8);
    if(path)CFRelease(path);
    if(!usable)return YES;
    const char *prefix="/Library/Wallpaper/Collections/";
    if(strncmp(original,prefix,strlen(prefix)))return YES;
    char *suffix=strstr(original,"~ipad.wallpaperCollection");
    if(!suffix || strcmp(suffix,"~ipad.wallpaperCollection"))return YES;
    // _loadSystemWallpaperCollections keys by UUID. An iPad directory
    // scanned after its phone wrapper overwrote the adapted screen variant.
    // Only exclude the duplicate when the matching real wrapper is present.
    int n=snprintf(phone,sizeof phone,"%.*s~iphone.wallpaperCollection/WallpaperCollection.plist",
        (int)(suffix-original),original);
    return n>0 && (size_t)n<sizeof phone && access(phone,R_OK)==0?NO:YES;
}
static BOOL wallpaper_resource_compat(id self,SEL selector)
{ (void)self;(void)selector;return YES; }
static void wallpaper_resource_policy(void)
{
    if(wallpaper_paths_process && !wallpaper_original_collection) {
        Class cls=objc_lookUpClass("WKWallpaperRepresentingCollection");
        Method method=cls?class_getClassMethod(cls,sel_registerName("shouldLoadWallpaperCollectionAtURL:")):NULL;
        if(method_abi(method,"Bc",1,"@")) {
            wallpaper_original_collection=(BOOL (*)(id,SEL,id))method_setImplementation(method,(IMP)wallpaper_collection_path);
            fprintf(stderr,"Pong UI18: native wallpaper scan prefers installed phone path per UUID\n");
        }
    }
    if(!wallpaper_process || wallpaper_policy_installed)return;
    Class cls=objc_lookUpClass("NSUserDefaults");
    Method product=cls?class_getInstanceMethod(cls,sel_registerName("wk_ignoreProductTypesForWallpaperBundle")):NULL;
    Method screen=cls?class_getInstanceMethod(cls,sel_registerName("wk_ignoreLogicalScreenClassForWallpaperBundle")):NULL;
    if(!method_abi(product,"Bc",0,"") || !method_abi(screen,"Bc",0,""))return;
    method_setImplementation(product,(IMP)wallpaper_resource_compat);
    method_setImplementation(screen,(IMP)wallpaper_resource_compat);
    wallpaper_policy_installed=true;
    fprintf(stderr,"Pong UI18: original 22H374 wallpaper catalogs enabled for Pong screen\n");
}

/* Same single-user container policy as the working iOS16 port. Native
 * UID containers remain in use. Do not disable launchd's Darwin24 proxy
 * personas, which are separate from per-user MCM persona containers. */
static BOOL no_container_personas(id self,SEL selector)
{ (void)self;(void)selector;return NO; }

/* 22H374 SBLockdownManager still stores its local UI state as an int.
 * Its original method maps Activated/FactoryActivated to 2. Discover the
 * ivar at runtime, rather than copying the old OS's +8 offset. This is a
 * development SpringBoard setup policy, not a mobileactivationd/SEP reply. */
static void setup_activation(id self,SEL selector)
{
    (void)selector;
    const int state=2;
    memcpy((char *)self+activation_offset,&state,sizeof(state));
    static unsigned reported;
    if(__atomic_fetch_add(&reported,1,__ATOMIC_RELAXED)==0)
        fprintf(stderr,"Pong UI18: development SpringBoard activation UI selected\n");
}

/* The supplied 22H374 method is B24@0:8Q16, as on 20G81. Keep its actual
 * transition/notification implementation and return value, passing the
 * no-assistant reason requested by the Pong bring-up boot option. */
static BOOL setup_reason(id self,SEL selector,unsigned long long reason)
{
    static unsigned reported;
    if(reason && __atomic_fetch_add(&reported,1,__ATOMIC_RELAXED)<3)
        fprintf(stderr,"Pong UI18: development setup reason %llu skipped\n",reason);
    return ((BOOL(*)(id,SEL,unsigned long long))setup_reason_original)(self,selector,0);
}

static void springboard_setup_policy(void)
{
    if(!springboard_process || !skip_setup)return;
    if(!activation_installed) {
        Class cls=objc_lookUpClass("SBLockdownManager");
        Method method=cls?class_getInstanceMethod(cls,sel_registerName("_setupActivationState")):NULL;
        Ivar ivar=cls?class_getInstanceVariable(cls,"_state"):NULL;
        const char *type=ivar?ivar_getTypeEncoding(ivar):NULL;
        ptrdiff_t offset=ivar?ivar_getOffset(ivar):-1;
        if(method_abi(method,"v",0,"") && type && type[0] && strchr("iI",type[0]) &&
           offset>=(ptrdiff_t)sizeof(void *) &&
           (size_t)offset+sizeof(int)<=class_getInstanceSize(cls)) {
            activation_offset=offset;
            method_setImplementation(method,(IMP)setup_activation);
            activation_installed=true;
            fprintf(stderr,"Pong UI18: 22H374 activation UI policy installed\n");
        }
    }
    if(!setup_reason_original) {
        Class cls=objc_lookUpClass("SBSetupManager");
        Method method=cls?class_getInstanceMethod(cls,sel_registerName("_setSetupRequiredReason:")):NULL;
        if(method_abi(method,"B",1,"Q")) {
            setup_reason_original=method_setImplementation(method,(IMP)setup_reason);
            fprintf(stderr,"Pong UI18: 22H374 Setup Assistant UI policy installed\n");
        }
    }
}

/* Reuse the working iOS16 policy, with 22H374's actual method ABI.
 * The method normally queries connectedDevices before returning nil when
 * there is no internal battery. Avoid its powerd wait only while this port
 * has no published physical power source. Keep the original path whenever
 * an IOPMPowerSource is present; never invent charge/health values. */
static id internal_battery(id self,SEL selector)
{
    CFMutableDictionaryRef match=service_matching("IOPMPowerSource");
    if(!match)return ((id(*)(id,SEL))battery_original)(self,selector);
    uint32_t source=matching_service(0,match); /* consumes match */
    if(source) {
        release_service(source);
        return ((id(*)(id,SEL))battery_original)(self,selector);
    }
    static unsigned reported;
    if(__atomic_fetch_add(&reported,1,__ATOMIC_RELAXED)==0)
        fprintf(stderr,"Pong UI18: no physical power source; omit unavailable battery status\n");
    return nil;
}

static void springboard_platform_policy(void)
{
    if(!springboard_process || battery_original)return;
    Class cls=objc_lookUpClass("BCBatteryDeviceController");
    Method method=cls?class_getInstanceMethod(cls,sel_registerName("sb_deviceInternalBattery")):NULL;
    if(!method_abi(method,"@",0,""))return;
    service_matching=(__typeof__(service_matching))dlsym(RTLD_DEFAULT,"IOServiceMatching");
    matching_service=(__typeof__(matching_service))dlsym(RTLD_DEFAULT,"IOServiceGetMatchingService");
    release_service=(__typeof__(release_service))dlsym(RTLD_DEFAULT,"IOObjectRelease");
    if(!service_matching || !matching_service || !release_service)return;
    battery_original=method_setImplementation(method,(IMP)internal_battery);
}

static void container_persona_policy(void)
{
    if(personas_installed)return;
    Class cls=objc_lookUpClass("MCMUserIdentityCache");
    Method method=cls?class_getClassMethod(cls,sel_registerName("personasAreSupported")):NULL;
    if(!method_abi(method,"Bc",0,""))return;
    method_setImplementation(method,(IMP)no_container_personas);
    personas_installed=true;
    fprintf(stderr,"Pong UI18: native single-user container paths selected\n");
}

/* A new unencrypted Data has no persona.kb. In 22H374 --init can finish
 * without creating it, but the daemon's disk loader then aborts on ENOENT.
 * Let the matching executable's own platform class create and serialize
 * its empty initial registry. No plist schema, users, key material or
 * persona IDs are fabricated; existing registries are never replaced. */
static void initialize_persona_manifest(void)
{
    const char *path="/private/var/keybags/persona.kb";
    struct stat st;
    if(geteuid()!=0 || lstat(path,&st)==0 || errno!=ENOENT)return;
    if(lstat("/private/var/keybags",&st)!=0 || !S_ISDIR(st.st_mode) ||
       st.st_uid!=0 || (st.st_mode&0077))return;
    Class cls=objc_lookUpClass("UMDPersonaManifest");
    SEL make=sel_registerName("manifest"),encode=sel_registerName("dataValue");
    if(!method_abi(cls?class_getClassMethod(cls,make):NULL,"@",0,""))return;
    id manifest=((id(*)(id,SEL))objc_msgSend)((id)cls,make);
    if(!manifest || !method_abi(class_getInstanceMethod(object_getClass(manifest),encode),"@",0,""))return;
    id data=((id(*)(id,SEL))objc_msgSend)(manifest,encode);
    if(!data || CFGetTypeID((CFTypeRef)data)!=CFDataGetTypeID())return;
    CFIndex length=CFDataGetLength((CFDataRef)data);
    if(length<=0 || length>4*1024*1024)return;
    /* Publish only a complete registry. A crash during the old direct
     * O_EXCL write left a truncated persona.kb that all later boots kept. */
    char temporary[]="/private/var/keybags/.persona.kb.pong.XXXXXX";
    int fd=mkstemp(temporary);
    if(fd<0)return;
    (void)fcntl(fd,F_SETFD,FD_CLOEXEC);
    const UInt8 *bytes=CFDataGetBytePtr((CFDataRef)data);
    size_t written=0;
    while(written<(size_t)length) {
        ssize_t n=write(fd,bytes+written,(size_t)length-written);
        if(n<0 && errno==EINTR)continue;
        if(n<=0)break;
        written+=(size_t)n;
    }
    int saved=0;
    if(written!=(size_t)length || fsync(fd)!=0)saved=errno?errno:EIO;
    if(close(fd)!=0 && !saved)saved=errno;
    bool published=false;
    if(!saved) {
        if(link(temporary,path)==0)published=true;
        else if(errno!=EEXIST)saved=errno;
    }
    (void)unlink(temporary);
    if(saved) {
        fprintf(stderr,"Pong UI18: native persona registry write failed errno=%d\n",saved);
    } else if(published) {
        int directory=open("/private/var/keybags",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        if(directory>=0) {
            if(fsync(directory)!=0)
                fprintf(stderr,"Pong UI18: persona registry published; directory sync errno=%d\n",errno);
            (void)close(directory);
        }
        fprintf(stderr,"Pong UI18: 22H374 serialized initial persona registry (%ld bytes)\n",(long)length);
    }
}

static bool executable_image_range(const struct mach_header_64 *header,intptr_t slide,
                                   uintptr_t target,size_t bytes)
{
    if(!bytes || (uintptr_t)(header+1)>UINTPTR_MAX-header->sizeofcmds)return false;
    const struct load_command *lc=(const void *)(header+1);
    const char *end=(const char *)lc+header->sizeofcmds;
    for(uint32_t i=0;i<header->ncmds;i++) {
        if((const char *)lc+sizeof(*lc)>end || lc->cmdsize<sizeof(*lc) ||
           lc->cmdsize>(size_t)(end-(const char *)lc))return false;
        if(lc->cmd==LC_SEGMENT_64 && lc->cmdsize>=sizeof(struct segment_command_64)) {
            const struct segment_command_64 *segment=(const void *)lc;
            const vm_prot_t needed=VM_PROT_READ|VM_PROT_EXECUTE;
            uintptr_t start;
            if((segment->initprot&needed)==needed) {
                if(slide>=0) {
                    if(segment->vmaddr>UINTPTR_MAX-(uintptr_t)slide)return false;
                    start=(uintptr_t)segment->vmaddr+(uintptr_t)slide;
                } else {
                    uintptr_t amount=0-(uintptr_t)slide;
                    if(segment->vmaddr<amount)return false;
                    start=(uintptr_t)segment->vmaddr-amount;
                }
                if(segment->vmsize<=UINTPTR_MAX-start && target>=start &&
                   target-start<=segment->vmsize && bytes<=segment->vmsize-(target-start))return true;
            }
        }
        lc=(const void *)((const char *)lc+lc->cmdsize);
    }
    return false;
}

static bool install_compositor_code(const struct mach_header *header,intptr_t slide,uintptr_t offset,
    const uint32_t *expected,const uint32_t *replacement,size_t bytes,const char *label)
{
    if(offset>UINTPTR_MAX-(uintptr_t)header || !bytes || bytes>vm_page_size)return false;
    uintptr_t target=(uintptr_t)header+offset;
    if(!executable_image_range((const void *)header,slide,target,bytes)) {
        fprintf(stderr,"Pong UI18: %s outside executable image range; SKIP\n",label);return false;
    }
    if(memcmp((const void *)target,expected,bytes)) {
        fprintf(stderr,"Pong UI18: 22H374 %s entry differs; SKIP\n",label);return false;
    }
    const vm_address_t page=target&~((vm_address_t)vm_page_size-1);
    if(target-page>vm_page_size-bytes)return false;
    kern_return_t kr=vm_protect(mach_task_self(),page,vm_page_size,FALSE,
                               VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY);
    if(kr!=KERN_SUCCESS) {
        fprintf(stderr,"Pong UI18: %s protection failed %d\n",label,kr);return false;
    }
    memcpy((void *)target,replacement,bytes);
    sys_icache_invalidate((void *)target,bytes);
    kr=vm_protect(mach_task_self(),page,vm_page_size,FALSE,VM_PROT_READ|VM_PROT_EXECUTE);
    if(kr!=KERN_SUCCESS) {
        memcpy((void *)target,expected,bytes);
        sys_icache_invalidate((void *)target,bytes);
        (void)vm_protect(mach_task_self(),page,vm_page_size,FALSE,VM_PROT_READ|VM_PROT_EXECUTE);
        fprintf(stderr,"Pong UI18: %s RX restore failed %d; original restored\n",label,kr);
        return false;
    }
    fprintf(stderr,"Pong UI18: 22H374 %s selected\n",label);return true;
}

/* 22H374 CoreBrightness calls slider conversion before allocating its own
 * uncalibrated fallback curve. g355 captures the same null-map fault as Pong
 * services84. Use ONLY this exact image's built-in software defaults while
 * its mapping is null; existing mappings call the original function.
 * These defaults are not measured L18 panel photometry. Adapted from
 * UniXNU-pong e0ee6df109b4b5dc706cac660d4f8210dc911dac, services85.
 * The service gate below uses verified zizhan hardware, not Pong identity. */
static float (*brightness18_slider_original)(void *,float);
static float brightness18_defaults[3];
static float brightness18_slider(void *display,float slider)
{
    uintptr_t curve=0;
    if(display)memcpy(&curve,(const char *)display+0x48,sizeof(curve));
    if(display && !curve) {
        if(!(slider>=0.0f))slider=0.0f;
        if(slider>1.0f)slider=1.0f;
        const float position=slider*2.0f;
        const unsigned lower=position>=1.0f?1:0;
        static unsigned reported;
        if(__atomic_fetch_add(&reported,1,__ATOMIC_RELAXED)==0)
            fprintf(stderr,"Pong brightness18: native software curve used before mapping initialization\n");
        return brightness18_defaults[lower]+(position-(float)lower)*
            (brightness18_defaults[lower+1]-brightness18_defaults[lower]);
    }
    return brightness18_slider_original(display,slider);
}
static void brightness18_image(const struct mach_header *header,intptr_t slide)
{
    if(brightness18_slider_original || strcmp(getprogname(),"backboardd") || header->magic!=MH_MAGIC_64)return;
    static const uint8_t uuid[16]={0x0a,0x02,0x14,0x08,0xa8,0xf4,0x37,0x2c,
                                 0xb5,0xd5,0x5f,0xdf,0xcc,0x83,0xc3,0x4a};
    const struct mach_header_64 *h=(const void *)header;
    const struct load_command *lc=(const void *)(h+1);
    const char *end=(const char *)lc+h->sizeofcmds;bool matched=false;
    for(uint32_t i=0;i<h->ncmds;i++) {
        if((const char *)lc+sizeof(*lc)>end || lc->cmdsize<sizeof(*lc) ||
           lc->cmdsize>(size_t)(end-(const char *)lc))return;
        if(lc->cmd==LC_UUID && lc->cmdsize==sizeof(struct uuid_command) &&
           !memcmp(((const struct uuid_command *)lc)->uuid,uuid,sizeof(uuid)))matched=true;
        lc=(const void *)((const char *)lc+lc->cmdsize);
    }
    if(!matched)return;
    // Require this board's actual DCS service, published only after hardware readback.
    CFMutableDictionaryRef (*matching)(const char *)=dlsym(RTLD_DEFAULT,"IOServiceMatching");
    uint32_t (*get)(uint32_t,CFDictionaryRef)=dlsym(RTLD_DEFAULT,"IOServiceGetMatchingService");
    int (*release)(uint32_t)=dlsym(RTLD_DEFAULT,"IOObjectRelease");
    if(!matching||!get||!release)return;
    CFMutableDictionaryRef dict=matching("IOService");
    const void *keys[]={CFSTR("PongPanel"),CFSTR("backlight-control")};
    const void *values[]={CFSTR("zizhan L18 primary / SM8475 DSI0"),kCFBooleanTrue};
    CFDictionaryRef properties=CFDictionaryCreate(NULL,keys,values,2,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    if(!dict || !properties){if(dict)CFRelease(dict);if(properties)CFRelease(properties);return;}
    CFDictionarySetValue(dict,CFSTR("IOPropertyMatch"),properties);CFRelease(properties);
    uint32_t service=get(0,dict);if(!service)return;release(service);
    const uintptr_t offset=0x144f5c,entry=(uintptr_t)header+offset;
    const uintptr_t defaults=(uintptr_t)header+0x1bead4;
    static const uint32_t expected[]={0xd10103ff,0xa9037bfd,0x9100c3fd,0xf81f83a0};
    if(!executable_image_range(h,slide,entry,sizeof(expected)) ||
       !executable_image_range(h,slide,defaults,sizeof(brightness18_defaults)) ||
       memcmp((const void *)entry,expected,sizeof(expected)))return;
    memcpy(brightness18_defaults,(const void *)defaults,sizeof(brightness18_defaults));
    if(!(brightness18_defaults[0]>0.0f && brightness18_defaults[0]<brightness18_defaults[1] &&
         brightness18_defaults[1]<brightness18_defaults[2] && brightness18_defaults[2]<100000.0f))return;
    vm_address_t trampoline=0;
    if(vm_allocate(mach_task_self(),&trampoline,vm_page_size,VM_FLAGS_ANYWHERE)!=KERN_SUCCESS)return;
    uint32_t back[]={0x58000050,0xd61f0200,(uint32_t)(entry+16),(uint32_t)((entry+16)>>32)};
    memcpy((void *)trampoline,expected,sizeof(expected));memcpy((void *)(trampoline+16),back,sizeof(back));
    sys_icache_invalidate((void *)trampoline,32);
    if(vm_protect(mach_task_self(),trampoline,vm_page_size,FALSE,VM_PROT_READ|VM_PROT_EXECUTE)!=KERN_SUCCESS) {
        (void)vm_deallocate(mach_task_self(),trampoline,vm_page_size);return;
    }
    const uintptr_t replacement=(uintptr_t)brightness18_slider;
    const uint32_t jump[]={0x58000050,0xd61f0200,(uint32_t)replacement,(uint32_t)(replacement>>32)};
    brightness18_slider_original=(float(*)(void *,float))trampoline;
    if(!install_compositor_code(header,slide,offset,expected,jump,sizeof(expected),"CoreBrightness fallback mapping initialization")) {
        brightness18_slider_original=NULL;(void)vm_deallocate(mach_task_self(),trampoline,vm_page_size);
    }
}

/* This build's generated deviceSupportsDynamicIsland getter calls
 * MGGetBoolAnswer with this exact key. Keep every other answer native: in
 * particular this does not advertise Face ID, telephony or camera hardware.
 * Identity/artwork/home gestures are provided by the ADT, not by this hook.
 */
/* zizhan (docs/progress/2026-11-01-120hz-and-cadence.md section 12): MobileGestalt answers DeviceSupportsASTC (hashed
 * ji56BO1mUeT7Qg9RO7Er9w) false for this identity, so QuartzCore's CADeviceSupportsASTC is false: CompressedImage refuses ASTC
 * texture data and every CA::Render::Texture is flagged for CPU decoding, and SpringBoard decoded ASTC icons and snapshots on its
 * main thread through ImageIO's ATXReadPlugin during app-open zooms. The GPU (Adreno 730, Turnip) samples ASTC LDR natively and
 * metal2vulkan maps the ASTC pixel formats. BUT with the answer true in SpringBoard and backboardd every app drew black (user v4,
 * screenshots g193/g194: Weather's sky with the native answer, black with it true), so it is opt-in: boot-arg pong_astc=1 answers
 * true in the UI processes and the render server, pong_astc=2 in SpringBoard/Preferences only (experiment); default native. */
/* 1 = iPhone17,1, 2 = iPad7,12 (set by phone_gestalt_image). UX-06 (ux-s2, 2026-11-23): the hook used to install for
 * iPhone17,1 only, so after the iPad7,12 identity became the default pong_astc=1 silently did nothing and SpringBoard was back to
 * decoding ASTC icons and snapshots on its main thread during every app-open zoom (~140 ms CPU-bound, 200 ms without a frame).
 * On the iPad identity only the ASTC and battery answers apply; the Dynamic Island one stays iPhone-only. */
static int gestalt_identity;
static bool phone_gestalt_bool(CFStringRef key)
{
    if(phone_ui_process && gestalt_identity==1 && key && (CFEqual(key,CFSTR("YlEtTtHlNesRBMal1CqRaA")) ||
               CFEqual(key,CFSTR("DeviceSupportsDynamicIsland"))))return true;
    /* Battery authentication (hashed D6/BMDrlb8V3WSiqL8gL+w; BatteryUsageUI +[BatteryUIResourceClass genuineBatteryStatus] asks it
     * first): this cell has no Apple authentication chip, so no AppleAuthCPRelay ever answers isTrustedForUI and Settings > Battery
     * Health spun forever (11-01 g229, status -1). false = authentication not applicable (status 0), the truth for this battery. */
    if(phone_ui_process && key && CFEqual(key,CFSTR("D6/BMDrlb8V3WSiqL8gL+w")))return false;
    if(key && (CFEqual(key,CFSTR("ji56BO1mUeT7Qg9RO7Er9w")) || CFEqual(key,CFSTR("DeviceSupportsASTC"))) &&
       (boot_option("pong_astc=1") || (phone_ui_process && boot_option("pong_astc=2"))))return true;
    return phone_gestalt_bool_original(key);
}
static void phone_gestalt_image(const struct mach_header *header,intptr_t slide)
{
    if(!gestalt_process || phone_gestalt_bool_original || header->magic!=MH_MAGIC_64)return;
    static const uint8_t uuid[16]={0xbe,0xed,0x77,0xde,0x98,0x7a,0x34,0x7e,
                                 0xad,0xa9,0x88,0x63,0x4a,0x6b,0xe5,0x6a};
    const struct mach_header_64 *h=(const void *)header;
    const struct load_command *lc=(const void *)(h+1);
    const char *end=(const char *)lc+h->sizeofcmds;bool matched=false;
    for(uint32_t i=0;i<h->ncmds;i++) {
        if((const char *)lc+sizeof(*lc)>end || lc->cmdsize<sizeof(*lc) ||
           lc->cmdsize>(size_t)(end-(const char *)lc))return;
        if(lc->cmd==LC_UUID && lc->cmdsize==sizeof(struct uuid_command) &&
           !memcmp(((const struct uuid_command *)lc)->uuid,uuid,16))matched=true;
        lc=(const void *)((const char *)lc+lc->cmdsize);
    }
    if(!matched)return;
    char product[64]={0};size_t size=sizeof(product);
    if(sysctlbyname("hw.product",product,&size,NULL,0))return;
    gestalt_identity=!strcmp(product,"iPhone17,1")?1:(!strcmp(product,"iPad7,12")?2:0);
    if(!gestalt_identity)return;
    const uintptr_t offset=0xf00,entry=(uintptr_t)header+offset;
    // Four position-independent prologue instructions from MGGetBoolAnswer.
    static const uint32_t expected[]={0xd100c3ff,0xa9014ff4,0xa9027bfd,0x910083fd};
    if(!executable_image_range((const void *)header,slide,entry,sizeof(expected)) ||
       memcmp((const void *)entry,expected,sizeof(expected)))return;
    vm_address_t trampoline=0;
    if(vm_allocate(mach_task_self(),&trampoline,vm_page_size,VM_FLAGS_ANYWHERE)!=KERN_SUCCESS)return;
    uint32_t back[]={0x58000050,0xd61f0200,(uint32_t)(entry+16),(uint32_t)((entry+16)>>32)};
    memcpy((void *)trampoline,expected,16);memcpy((void *)(trampoline+16),back,16);
    sys_icache_invalidate((void *)trampoline,32);
    if(vm_protect(mach_task_self(),trampoline,vm_page_size,FALSE,VM_PROT_READ|VM_PROT_EXECUTE)!=KERN_SUCCESS) {
        (void)vm_deallocate(mach_task_self(),trampoline,vm_page_size);return;
    }
    const uintptr_t replacement=(uintptr_t)phone_gestalt_bool;
    uint32_t jump[]={0x58000050,0xd61f0200,(uint32_t)replacement,(uint32_t)(replacement>>32)};
    phone_gestalt_bool_original=(bool(*)(CFStringRef))trampoline;
    if(!install_compositor_code(header,slide,offset,expected,jump,sizeof(expected),"MobileGestalt Dynamic Island")) {
        phone_gestalt_bool_original=NULL;(void)vm_deallocate(mach_task_self(),trampoline,vm_page_size);
    }
}

/* 22H374 _prepareVisualProviderIfNeeded directly calls this C selector,
 * bypassing the iOS class factory used by 20G81. Adapt that selection point
 * before UIKit computes provider flags/scale, preserving the native result
 * for supported layouts and every screen other than Pong's real panel. */
static Class phone_selected_class(id screen,id info)
{
    if(zizhan_pad_provider&&zizhan_statusbar_screen(screen)) {
        static unsigned reported;if(__atomic_fetch_add(&reported,1,__ATOMIC_RELAXED)<3)
            fprintf(stderr,"Zizhan statusbar18: UIKit native Pad selected scale=2 default_screen=%u\n",screen?0u:1u);
        return zizhan_pad_provider;
    }
    Class selected=phone_selector_original(screen,info);
    Class fallback=objc_lookUpClass("_UIStatusBarVisualProvider_Fallback");
    static unsigned decisions;
    if(__atomic_fetch_add(&decisions,1,__ATOMIC_RELAXED)<2)
        fprintf(stderr,"Pong UI18: native provider decision=%s screen=%s\n",
            selected?class_getName(selected):"nil",screen?object_getClassName(screen):"default");
    if((selected && selected!=fallback) || !statusbar_provider)return selected;
    /* UIKit also asks for its default display with nil while UIScreen is
     * being initialized. Calling mainScreen here would recurse into that
     * initialization; use the same Pong-only default as the class factory. */
    if(screen) {
        CGRect bounds=((CGRect(*)(id,SEL))objc_msgSend)(screen,sel_registerName("nativeBounds"));
        double scale=((double(*)(id,SEL))objc_msgSend)(screen,sel_registerName("nativeScale"));
        if(!((bounds.size.width==1080 && bounds.size.height==2412) ||
             (bounds.size.width==2412 && bounds.size.height==1080)) || scale!=3.0)return selected;
    }
    static unsigned reported;
    if(__atomic_fetch_add(&reported,1,__ATOMIC_RELAXED)<3)
        fprintf(stderr,"Pong UI18: Darwin24 selector chose native 1080 phone provider\n");
    return statusbar_provider;
}
static void phone_prepare_provider(id self,SEL selector)
{
    /* A class assigned before the selector was adapted skips the selector
     * entirely in Darwin24. Re-run native discovery only for an unbuilt
     * diagnostic provider; do not replace a live provider or pre-set its
     * scale/overflow flags. Both accessors and the ivar are from this OS's
     * runtime metadata, not a 20G81 offset. */
    Class assigned=((Class(*)(id,SEL))objc_msgSend)(self,sel_registerName("_visualProviderClass"));
    Class fallback=objc_lookUpClass("_UIStatusBarVisualProvider_Fallback");
    id current=object_getIvar(self,phone_provider_ivar);
    if(current && (object_getClass(current)==fallback || (object_getClass(current)==statusbar_provider && zizhan_pad_provider && zizhan_statusbar_screen(nil)))) {
        // Native callers inside UIKit can invoke this entry directly. Tear
        // down an already built diagnostic region before native discovery
        // recomputes scale/overflow flags and constructs the phone provider.
        ((void(*)(id,SEL))objc_msgSend)(self,sel_registerName("resetVisualProvider"));
        object_setIvarWithStrongDefault(self,phone_provider_ivar,nil);
        ((void(*)(id,SEL,Class))objc_msgSend)(self,sel_registerName("_setVisualProviderClass:"),Nil);
    }
    if(assigned==fallback && !object_getIvar(self,phone_provider_ivar)) {
        ((void(*)(id,SEL,Class))objc_msgSend)(self,sel_registerName("_setVisualProviderClass:"),Nil);
    }
    ((void(*)(id,SEL))phone_prepare_original)(self,selector);
}
static void phone_repair_actual_bar(void *context)
{
    id bar=(id)context;
    Class cls=objc_lookUpClass("_UIStatusBar");
    if(phone_prepare_original && phone_provider_ivar && cls &&
       ((BOOL(*)(id,SEL,Class))objc_msgSend)(bar,sel_registerName("isKindOfClass:"),cls)) {
        id current=object_getIvar(bar,phone_provider_ivar);
        if(current && (object_getClass(current)==objc_lookUpClass("_UIStatusBarVisualProvider_Fallback") || (object_getClass(current)==statusbar_provider && zizhan_pad_provider && zizhan_statusbar_screen(nil)))) {
            phone_prepare_provider(bar,sel_registerName("_prepareVisualProviderIfNeeded"));
            ((void(*)(id,SEL))objc_msgSend)(bar,sel_registerName("setNeedsLayout"));
            current=object_getIvar(bar,phone_provider_ivar);
            fprintf(stderr,"Pong UI18: actual status bar rebuilt provider=%s\n",current?object_getClassName(current):"nil");
        }
    }
    ((void(*)(id,SEL))objc_msgSend)(bar,sel_registerName("release"));
}
static void phone_layout_provider(id self,SEL selector)
{
    // A status-bar server window need not belong to UIApplication.windows.
    // Rebuild an early fallback at its first layout, on the actual view,
    // using native teardown and preparation rather than changing its class.
    id current=object_getIvar(self,phone_provider_ivar);
    Class fallback=objc_lookUpClass("_UIStatusBarVisualProvider_Fallback");
    if(current && (object_getClass(current)==fallback || (object_getClass(current)==statusbar_provider && zizhan_pad_provider && zizhan_statusbar_screen(nil)))) {
        SEL reset=sel_registerName("resetVisualProvider");
        ((void(*)(id,SEL))objc_msgSend)(self,reset);
        object_setIvarWithStrongDefault(self,phone_provider_ivar,nil);
        ((void(*)(id,SEL,Class))objc_msgSend)(self,sel_registerName("_setVisualProviderClass:"),Nil);
        ((void(*)(id,SEL))phone_prepare_original)(self,sel_registerName("_prepareVisualProviderIfNeeded"));
        current=object_getIvar(self,phone_provider_ivar);
        fprintf(stderr,"Pong UI18: status layout rebuilt provider=%s\n",current?object_getClassName(current):"nil");
    }
    ((void(*)(id,SEL))phone_layout_original)(self,selector);
}
static void phone_prepare_policy(const struct mach_header *header,intptr_t slide)
{
    if(phone_prepare_original || !phone_selector_installed)return;
    Class cls=objc_lookUpClass("_UIStatusBar");
    Method prepare=cls?class_getInstanceMethod(cls,sel_registerName("_prepareVisualProviderIfNeeded")):NULL;
    Method get=cls?class_getInstanceMethod(cls,sel_registerName("_visualProviderClass")):NULL;
    Method set=cls?class_getInstanceMethod(cls,sel_registerName("_setVisualProviderClass:")):NULL;
    Ivar ivar=cls?class_getInstanceVariable(cls,"_visualProvider"):NULL;
    const char *type=ivar?ivar_getTypeEncoding(ivar):NULL;
    if(!method_abi(prepare,"v",0,"") || !method_abi(get,"#",0,"") ||
       !method_abi(set,"v",1,"#") || !type || type[0]!='@')return;
    // In 22H374 UIKit's own status paths also call the native IMP directly;
    // a method-table replacement alone leaves those cached callers intact.
    // This exact four-instruction prologue contains no PC-relative operand.
    const uintptr_t offset=0x1860f2fa0ULL-0x184f96000ULL;
    const uintptr_t entry=(uintptr_t)header+offset;
    static const uint32_t expected[]={0xd102c3ff,0x6d0623e9,0xa9075ff8,0xa90857f6};
    if(!executable_image_range((const void *)header,slide,entry,sizeof(expected)) ||
       memcmp((const void *)entry,expected,sizeof(expected)))return;
    vm_address_t trampoline=0;
    if(vm_allocate(mach_task_self(),&trampoline,vm_page_size,VM_FLAGS_ANYWHERE)!=KERN_SUCCESS)return;
    uint32_t back[]={0x58000050,0xd61f0200,(uint32_t)(entry+16),(uint32_t)((entry+16)>>32)};
    memcpy((void *)trampoline,expected,16);memcpy((void *)(trampoline+16),back,16);
    sys_icache_invalidate((void *)trampoline,32);
    if(vm_protect(mach_task_self(),trampoline,vm_page_size,FALSE,VM_PROT_READ|VM_PROT_EXECUTE)!=KERN_SUCCESS) {
        (void)vm_deallocate(mach_task_self(),trampoline,vm_page_size);return;
    }
    const uintptr_t replacement=(uintptr_t)phone_prepare_provider;
    uint32_t jump[]={0x58000050,0xd61f0200,(uint32_t)replacement,(uint32_t)(replacement>>32)};
    phone_provider_ivar=ivar;
    phone_prepare_original=(IMP)trampoline;
    if(!install_compositor_code(header,slide,offset,expected,jump,sizeof(expected),"phone provider prepare entry")) {
        phone_prepare_original=NULL;(void)vm_deallocate(mach_task_self(),trampoline,vm_page_size);return;
    }
    method_setImplementation(prepare,(IMP)phone_prepare_provider);
    SEL layout=sel_registerName("layoutSubviews"),reset=sel_registerName("resetVisualProvider");
    Method lm=class_getInstanceMethod(cls,layout),rm=class_getInstanceMethod(cls,reset);
    if(method_abi(lm,"v",0,"") && method_abi(rm,"v",0,"")) {
        phone_layout_original=method_getImplementation(lm);
        if(!class_addMethod(cls,layout,(IMP)phone_layout_provider,method_getTypeEncoding(lm)))
            method_setImplementation(lm,(IMP)phone_layout_provider);
    }
}

/* A status view may be built by UIKit/SpringBoard +load before the injected
 * runtime's constructor. Factory hooks cannot change that existing object.
 * On the main queue, rebuild only a real fallback provider using 22H374's
 * native region teardown, strong ivar ownership and initialization path.
 * Native scale/reference/overflow flags are computed by UIKit again.
 */
static void phone_repair_view(id view,unsigned depth,unsigned *remaining)
{
    if(!view || !depth || !*remaining)return;
    --*remaining;
    stui_repair_bar(view);
    Class bar=objc_lookUpClass("_UIStatusBar");
    if(bar && ((BOOL(*)(id,SEL,Class))objc_msgSend)(view,sel_registerName("isKindOfClass:"),bar) &&
       phone_prepare_original && phone_provider_ivar) {
        id current=object_getIvar(view,phone_provider_ivar);
        Class fallback=objc_lookUpClass("_UIStatusBarVisualProvider_Fallback");
        if(current && (object_getClass(current)==fallback || (object_getClass(current)==statusbar_provider && zizhan_pad_provider && zizhan_statusbar_screen(nil)))) {
            SEL reset=sel_registerName("resetVisualProvider"),prepare=sel_registerName("_prepareVisualProviderIfNeeded");
            Method teardown=class_getInstanceMethod(bar,reset);
            if(method_abi(teardown,"v",0,"")) {
                ((void(*)(id,SEL))objc_msgSend)(view,reset);
                object_setIvarWithStrongDefault(view,phone_provider_ivar,nil);
                ((void(*)(id,SEL,Class))objc_msgSend)(view,sel_registerName("_setVisualProviderClass:"),Nil);
                ((void(*)(id,SEL))phone_prepare_original)(view,prepare);
                ((void(*)(id,SEL))objc_msgSend)(view,sel_registerName("setNeedsLayout"));
                id installed=object_getIvar(view,phone_provider_ivar);
                fprintf(stderr,"Pong UI18: existing status provider rebuilt %s -> %s\n",
                    class_getName(fallback),installed?object_getClassName(installed):"nil");
            }
        }
    }
    id children=((id(*)(id,SEL))objc_msgSend)(view,sel_registerName("subviews"));
    if(!children)return;
    CFIndex count=CFArrayGetCount((CFArrayRef)children);
    for(CFIndex i=0;i<count && *remaining;i++)
        phone_repair_view((id)CFArrayGetValueAtIndex((CFArrayRef)children,i),depth-1,remaining);
}
static void phone_repair_existing(void *unused)
{
    (void)unused;
    if(!phone_ui_process || !phone_selector_installed)return;
    Class cls=objc_lookUpClass("UIApplication");
    id app=cls?((id(*)(id,SEL))objc_msgSend)((id)cls,sel_registerName("sharedApplication")):nil;
    id windows=app?((id(*)(id,SEL))objc_msgSend)(app,sel_registerName("windows")):nil;
    if(!windows)return;
    unsigned remaining=2048;CFIndex count=CFArrayGetCount((CFArrayRef)windows);
    for(CFIndex i=0;i<count && remaining;i++)
        phone_repair_view((id)CFArrayGetValueAtIndex((CFArrayRef)windows,i),16,&remaining);
    if(boot_option("zizhan_statusbar_diag=1")) {
        unsigned budget=256;
        for(CFIndex i=0;i<count&&budget;++i)zizhan_statusbar_geometry((id)CFArrayGetValueAtIndex((CFArrayRef)windows,i),16,&budget);
    }
}
static void phone_selector_image(const struct mach_header *header,intptr_t slide)
{
    static const struct mach_header *uikit;
    static intptr_t uikit_slide;
    static bool attempted;
    if(!phone_ui_process || phone_selector_installed || attempted || header->magic!=MH_MAGIC_64)return;
    static const uint8_t uuid[16]={0x15,0x0b,0x91,0xd9,0xf3,0x7e,0x35,0x9f,
                                 0xad,0x9d,0x95,0x26,0xa4,0x3b,0x81,0xaa};
    const struct mach_header_64 *h=(const void *)header;
    const struct load_command *lc=(const void *)(h+1);
    const char *end=(const char *)lc+h->sizeofcmds;
    for(uint32_t i=0;i<h->ncmds;i++) {
        if((const char *)lc+sizeof(*lc)>end || lc->cmdsize<sizeof(*lc) ||
           lc->cmdsize>(size_t)(end-(const char *)lc))return;
        if(lc->cmd==LC_UUID && lc->cmdsize==sizeof(struct uuid_command) &&
           !memcmp(((const struct uuid_command *)lc)->uuid,uuid,16)){uikit=header;uikit_slide=slide;}
        lc=(const void *)((const char *)lc+lc->cmdsize);
    }
    if(!uikit || !statusbar_provider)return;
    attempted=true;
    const uintptr_t offset=0x1860cbb90ULL-0x184f96000ULL;
    const uintptr_t entry=(uintptr_t)uikit+offset;
    // These four instructions only adjust SP/save registers; no PC-relative
    // operand is relocated. The rest of the original selector runs unchanged.
    static const uint32_t expected[]={0xd10443ff,0x6d0a23e9,0xa90b6ffc,0xa90c67fa};
    if(!executable_image_range((const void *)uikit,uikit_slide,entry,sizeof(expected)) ||
       memcmp((const void *)entry,expected,sizeof(expected)))return;
    vm_address_t trampoline=0;
    if(vm_allocate(mach_task_self(),&trampoline,vm_page_size,VM_FLAGS_ANYWHERE)!=KERN_SUCCESS)return;
    uint32_t back[]={0x58000050,0xd61f0200,(uint32_t)(entry+16),(uint32_t)((entry+16)>>32)};
    memcpy((void *)trampoline,expected,16);memcpy((void *)(trampoline+16),back,16);
    sys_icache_invalidate((void *)trampoline,32);
    if(vm_protect(mach_task_self(),trampoline,vm_page_size,FALSE,VM_PROT_READ|VM_PROT_EXECUTE)!=KERN_SUCCESS){
        (void)vm_deallocate(mach_task_self(),trampoline,vm_page_size);return;
    }
    const uintptr_t replacement=(uintptr_t)phone_selected_class;
    uint32_t jump[]={0x58000050,0xd61f0200,(uint32_t)replacement,(uint32_t)(replacement>>32)};
    phone_selector_original=(Class(*)(id,id))trampoline;
    phone_selector_installed=install_compositor_code(uikit,uikit_slide,offset,
        expected,jump,sizeof(expected),"phone provider selector");
    if(!phone_selector_installed){phone_selector_original=NULL;(void)vm_deallocate(mach_task_self(),trampoline,vm_page_size);}
    else phone_prepare_policy(uikit,uikit_slide);
}

/* Darwin24's system status bar moved out of UIKitCore. It has distinct
 * STUI classes and a 368-byte DynamicSplit provider, not the 240-byte UIKit
 * one. Select and initialize that native implementation; never mix objects,
 * copy a 20G81 offset or replace a small fallback object's isa. */
static Class stui_provider,zizhan_stui_pad_provider;
static Ivar stui_provider_ivar;
static IMP stui_select_original,stui_prepare_original,stui_layout_original;
static Class stui_selected_class(id screen,id info)
{
    if(zizhan_stui_pad_provider&&zizhan_statusbar_screen(screen)) {
        static unsigned reported;if(__atomic_fetch_add(&reported,1,__ATOMIC_RELAXED)<3)
            fprintf(stderr,"Zizhan statusbar18: SystemStatusUI native Pad selected scale=2 default_screen=%u\n",screen?0u:1u);
        return zizhan_stui_pad_provider;
    }
    if(stui_provider) {
        bool pong=!screen;
        if(screen) {
            CGRect bounds=((CGRect(*)(id,SEL))objc_msgSend)(screen,sel_registerName("nativeBounds"));
            double scale=((double(*)(id,SEL))objc_msgSend)(screen,sel_registerName("nativeScale"));
            pong=((bounds.size.width==1080 && bounds.size.height==2412) ||
                  (bounds.size.width==2412 && bounds.size.height==1080)) && scale==3.0;
        }
        if(pong) {
            static unsigned reported;
            if(__atomic_fetch_add(&reported,1,__ATOMIC_RELAXED)<3)
                fprintf(stderr,"Pong UI18: SystemStatusUI selected native Pong clock/battery layout\n");
            return stui_provider;
        }
    }
    return ((Class(*)(id,id))stui_select_original)(screen,info);
}
static void stui_prepare_provider(id self,SEL selector)
{
    Class fallback=objc_lookUpClass("STUIStatusBarVisualProvider_Fallback");
    id current=object_getIvar(self,stui_provider_ivar);
    Class assigned=((Class(*)(id,SEL))objc_msgSend)(self,sel_registerName("_visualProviderClass"));
    if(current && (object_getClass(current)==fallback || (object_getClass(current)==stui_provider && zizhan_stui_pad_provider && zizhan_statusbar_screen(nil)))) {
        ((void(*)(id,SEL))objc_msgSend)(self,sel_registerName("resetVisualProvider"));
        object_setIvarWithStrongDefault(self,stui_provider_ivar,nil);
        ((void(*)(id,SEL,Class))objc_msgSend)(self,sel_registerName("_setVisualProviderClass:"),Nil);
    } else if(!current && assigned==fallback) {
        ((void(*)(id,SEL,Class))objc_msgSend)(self,sel_registerName("_setVisualProviderClass:"),Nil);
    }
    ((void(*)(id,SEL))stui_prepare_original)(self,selector);
}
static void stui_repair_bar(id bar)
{
    if(!stui_prepare_original || !stui_provider_ivar)return;
    Class cls=objc_lookUpClass("STUIStatusBar");
    if(!cls || !((BOOL(*)(id,SEL,Class))objc_msgSend)(bar,sel_registerName("isKindOfClass:"),cls))return;
    id current=object_getIvar(bar,stui_provider_ivar);
    if(current && (object_getClass(current)==objc_lookUpClass("STUIStatusBarVisualProvider_Fallback") || (object_getClass(current)==stui_provider && zizhan_stui_pad_provider && zizhan_statusbar_screen(nil)))) {
        stui_prepare_provider(bar,sel_registerName("_prepareVisualProviderIfNeeded"));
        ((void(*)(id,SEL))objc_msgSend)(bar,sel_registerName("setNeedsLayout"));
    }
}
static void stui_layout_provider(id self,SEL selector)
{
    stui_repair_bar(self);
    ((void(*)(id,SEL))stui_layout_original)(self,selector);
    if(boot_option("zizhan_statusbar_diag=1")) {
        static unsigned captured;
        if(__atomic_fetch_add(&captured,1,__ATOMIC_RELAXED)<2) {
            unsigned budget=128;zizhan_statusbar_geometry(self,12,&budget);
        }
        static char late_key;static unsigned scheduled;
        if(!objc_getAssociatedObject(self,&late_key)&&__atomic_load_n(&scheduled,__ATOMIC_RELAXED)<2) {
            objc_setAssociatedObject(self,&late_key,(id)kCFBooleanTrue,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            __atomic_fetch_add(&scheduled,1,__ATOMIC_RELAXED);
            id retained=((id(*)(id,SEL))objc_msgSend)(self,sel_registerName("retain"));
            dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,20*NSEC_PER_SEC),dispatch_get_main_queue(),retained,zizhan_statusbar_late_geometry);
        }
    }
}
static bool stui_install_entry(const struct mach_header *h,intptr_t slide,uintptr_t offset,
    const uint32_t expected[4],IMP replacement,IMP *saved,const char *name)
{
    const uintptr_t entry=(uintptr_t)h+offset;
    if(!executable_image_range((const void *)h,slide,entry,16) || memcmp((const void *)entry,expected,16))return false;
    vm_address_t code=0;
    if(vm_allocate(mach_task_self(),&code,vm_page_size,VM_FLAGS_ANYWHERE)!=KERN_SUCCESS)return false;
    uint32_t back[]={0x58000050,0xd61f0200,(uint32_t)(entry+16),(uint32_t)((entry+16)>>32)};
    memcpy((void *)code,expected,16);memcpy((void *)(code+16),back,16);
    sys_icache_invalidate((void *)code,32);
    if(vm_protect(mach_task_self(),code,vm_page_size,FALSE,VM_PROT_READ|VM_PROT_EXECUTE)!=KERN_SUCCESS) {
        (void)vm_deallocate(mach_task_self(),code,vm_page_size);return false;
    }
    const uintptr_t target=(uintptr_t)replacement;
    uint32_t jump[]={0x58000050,0xd61f0200,(uint32_t)target,(uint32_t)(target>>32)};
    *saved=(IMP)code;
    if(install_compositor_code(h,slide,offset,expected,jump,16,name))return true;
    *saved=NULL;(void)vm_deallocate(mach_task_self(),code,vm_page_size);return false;
}
static void stui_phone_image(const struct mach_header *header,intptr_t slide)
{
    if(!phone_ui_process || stui_select_original || header->magic!=MH_MAGIC_64)return;
    static const uint8_t uuid[]={0x0e,0x5c,0x36,0xcc,0x88,0x46,0x36,0x1c,
                                0x85,0x8b,0xba,0x51,0x0f,0xf8,0xdc,0x0d};
    const struct mach_header_64 *h=(const void *)header;
    const struct load_command *lc=(const void *)(h+1);
    const char *end=(const char *)lc+h->sizeofcmds;bool matched=false;
    for(uint32_t i=0;i<h->ncmds;i++) {
        if((const char *)lc+sizeof(*lc)>end || lc->cmdsize<sizeof(*lc) || lc->cmdsize>(size_t)(end-(const char *)lc))return;
        if(lc->cmd==LC_UUID && lc->cmdsize==sizeof(struct uuid_command))
            matched=!memcmp(((const struct uuid_command *)lc)->uuid,uuid,16);
        lc=(const void *)((const char *)lc+lc->cmdsize);
    }
    if(!matched)return;
    zizhan_stui_pad_provider=zizhan_statusbar_provider("STUIStatusBarVisualProvider_Pad","ZizhanSystemStatus18InnerPad");
    Class cls=objc_lookUpClass("STUIStatusBar"),base=objc_lookUpClass("STUIStatusBarVisualProvider_DynamicSplitMedium");
    Method prepare=cls?class_getInstanceMethod(cls,sel_registerName("_prepareVisualProviderIfNeeded")):NULL;
    Method reset=cls?class_getInstanceMethod(cls,sel_registerName("resetVisualProvider")):NULL;
    Method set=cls?class_getInstanceMethod(cls,sel_registerName("_setVisualProviderClass:")):NULL;
    Ivar ivar=cls?class_getInstanceVariable(cls,"_visualProvider"):NULL;
    if(!base || !method_abi(prepare,"v",0,"") || !method_abi(reset,"v",0,"") ||
       !method_abi(set,"v",1,"#") || !ivar || ivar_getTypeEncoding(ivar)[0]!='@')return;
    Class provider=objc_allocateClassPair(base,"PongSystemStatus18Phone1080",0);
    if(!provider)return;
    if(!class_addMethod(object_getClass((id)provider),sel_registerName("referenceScreenScale"),(IMP)phone_reference_scale,"d16@0:8")) {
        objc_disposeClassPair(provider);return;
    }
    objc_registerClassPair(provider);stui_provider=provider;stui_provider_ivar=ivar;
    // The matched four-instruction prologues only save SP/registers.
    static const uint32_t select_code[]={0xd102c3ff,0x6d0523e9,0xa90667fa,0xa9075ff8};
    if(!stui_install_entry(header,slide,0x1d1d5ef68ULL-0x1d1d5d000ULL,select_code,
        (IMP)stui_selected_class,&stui_select_original,"SystemStatusUI provider selector"))return;
    static const uint32_t prepare_code[]={0xd102c3ff,0x6d0623e9,0xa9075ff8,0xa90857f6};
    if(stui_install_entry(header,slide,0x1d1d61eb8ULL-0x1d1d5d000ULL,prepare_code,
        (IMP)stui_prepare_provider,&stui_prepare_original,"SystemStatusUI provider prepare")) {
        method_setImplementation(prepare,(IMP)stui_prepare_provider);
        SEL layout=sel_registerName("layoutSubviews");Method lm=class_getInstanceMethod(cls,layout);
        if(method_abi(lm,"v",0,"")) {
            stui_layout_original=method_getImplementation(lm);
            if(!class_addMethod(cls,layout,(IMP)stui_layout_provider,method_getTypeEncoding(lm)))
                method_setImplementation(lm,(IMP)stui_layout_provider);
        }
    }
    Class fallback=objc_lookUpClass("STUIStatusBarFallbackItem");
    Method diagnostic=fallback?class_getInstanceMethod(fallback,sel_registerName("canEnableDisplayItem:fromData:")):NULL;
    if(method_abi(diagnostic,"Bc",2,"@@"))method_setImplementation(diagnostic,(IMP)phone_diagnostic_item_enabled);
}

/* UX-06 (ux-p1, 2026-11-23): the boot-args are fixed for the life of the process, but boot_option() asked the kernel for them
 * (sysctlbyname, a name lookup plus a copy) on every call, and hooks on hot paths call it: zizhan_statusbar_screen() on every
 * status bar size or provider lookup. SpringBoard's main thread spent ~19 ms of each app-open tap in __sysctl. Read them once. */
static char cached_boot_args[2048];
static bool cached_boot_args_ok;
static void read_boot_args(void *unused)
{
    (void)unused;
    /* kern.zizhan_bootargs (XNU patch 0035) adds the /chosen zizhan-boot-args a round could not fit on the command line */
    size_t size=sizeof(cached_boot_args)-1;
    if(sysctlbyname("kern.zizhan_bootargs",cached_boot_args,&size,NULL,0) &&
       (size=sizeof(cached_boot_args)-1,sysctlbyname("kern.bootargs",cached_boot_args,&size,NULL,0))) {
        cached_boot_args[0]='\0';
        return;
    }
    cached_boot_args[size<sizeof(cached_boot_args)?size:sizeof(cached_boot_args)-1]='\0';
    cached_boot_args_ok=true;
}
static const char *boot_args(void)
{
    static dispatch_once_t once;
    dispatch_once_f(&once,NULL,read_boot_args);
    return cached_boot_args_ok?cached_boot_args:NULL;
}
static bool boot_option(const char *key)
{
    const char *args=boot_args();
    if(!args)return false;
    const size_t length=strlen(key);
    for(const char *p=args;(p=strstr(p,key));p+=length) {
        if((p==args || p[-1]==' ' || p[-1]=='\t') &&
           (p[length]=='\0' || p[length]==' ' || p[length]=='\t'))return true;
    }
    return false;
}

typedef CGContextRef (*PhotosContextFn)(CGSize,BOOL,uintptr_t,BOOL);
typedef CGContextRef (*PhotosAutomaticFormatFn)(CGSize,BOOL,uintptr_t);
static void *photos_context_original;
static void *photos_automatic_format_original;
static bool photos_context_installed,photos_automatic_format_installed;
static unsigned photos_zero_size_reports;

static CGSize photos_context_size(CGSize size,const char *name,const void *return_address)
{
    const double width=round(size.width),height=round(size.height);
    if(width>=1 && height>=1)return size;
    const CGSize original=size;
    if(!(width>=1))size.width=1;
    if(!(height>=1))size.height=1;
    if(__atomic_fetch_add(&photos_zero_size_reports,1,__ATOMIC_RELAXED)<8) {
        Dl_info caller={};
        const char *image=dladdr(return_address,&caller) && caller.dli_fname?caller.dli_fname:"unknown";
        fprintf(stderr,"Pong UI18: Photos %s size=%.3fx%.3f clamped to %.3fx%.3f caller=%p %s\n",
            name,original.width,original.height,size.width,size.height,return_address,image);
    }
    return size;
}

static CGContextRef photos_create_context(CGSize size,BOOL opaque,uintptr_t color_space,BOOL extended)
{
    return ((PhotosContextFn)photos_context_original)(
        photos_context_size(size,"PXCreateContext",__builtin_return_address(0)),opaque,color_space,extended);
}

static CGContextRef photos_create_context_automatic(CGSize size,BOOL opaque,uintptr_t format_range)
{
    return ((PhotosAutomaticFormatFn)photos_automatic_format_original)(
        photos_context_size(size,"PXCreateContextWithAutomaticFormat",__builtin_return_address(0)),opaque,format_range);
}

static bool photos_context_entry(const struct mach_header *header,intptr_t slide,const char *symbol,
    IMP replacement,void **original,const uint32_t expected[4],const char *label)
{
    void *entry=dlsym(RTLD_DEFAULT,symbol);
    Dl_info caller={};
    if(!entry || !dladdr(entry,&caller) || caller.dli_fbase!=header)return false;
    if(original && *original)return true;
    if(!executable_image_range((const void *)header,slide,(uintptr_t)entry,16) ||
       memcmp(entry,expected,16)) {
        fprintf(stderr,"Pong UI18: Photos %s entry differs; keep native code\n",label);
        return false;
    }
    vm_address_t trampoline=0;
    if(vm_allocate(mach_task_self(),&trampoline,vm_page_size,VM_FLAGS_ANYWHERE)!=KERN_SUCCESS)return false;
    uint32_t back[]={0x58000050,0xd61f0200,(uint32_t)((uintptr_t)entry+16),(uint32_t)(((uintptr_t)entry+16)>>32)};
    memcpy((void *)trampoline,expected,16);
    memcpy((void *)(trampoline+16),back,sizeof(back));
    sys_icache_invalidate((void *)trampoline,32);
    if(vm_protect(mach_task_self(),trampoline,vm_page_size,FALSE,VM_PROT_READ|VM_PROT_EXECUTE)!=KERN_SUCCESS) {
        (void)vm_deallocate(mach_task_self(),trampoline,vm_page_size);
        return false;
    }
    const uintptr_t patch=(uintptr_t)replacement;
    uint32_t jump[]={0x58000050,0xd61f0200,(uint32_t)patch,(uint32_t)(patch>>32)};
    if(!install_compositor_code(header,slide,(uintptr_t)entry-(uintptr_t)header,
        expected,jump,sizeof(jump),label)) {
        (void)vm_deallocate(mach_task_self(),trampoline,vm_page_size);
        return false;
    }
    if(original)*original=(void *)trampoline;
    return true;
}

static void photos_context_image(const struct mach_header *header,intptr_t slide)
{
    static const uint32_t automatic_expected[]={0xd10283ff,0x6d0523e9,0xa9065ff8,0xa90757f6};
    static const uint32_t context_expected[]={0xd10183ff,0x6d0123e9,0xa9025ff8,0xa90357f6};
    if(!photos_automatic_format_installed)
        photos_automatic_format_installed=photos_context_entry(header,slide,
            "PXCreateContextWithAutomaticFormat",(IMP)photos_create_context_automatic,
            &photos_automatic_format_original,automatic_expected,"Photos automatic context size");
    if(!photos_context_installed)
        photos_context_installed=photos_context_entry(header,slide,
            "PXCreateContext",(IMP)photos_create_context,
            &photos_context_original,context_expected,"Photos context size");
}

/* 22H374 QuartzCore, UUID FAF7D7EA-18F0-3C2D-816C-AC56D16C40A3.
 * Its AccelServer::render_update calls renderer(), and on nullptr branches
 * to Server::render_update -> sw_renderer() -> the original SWContext.
 * Replace the method body before the constructor's direct call, as on iOS16.
 * This selects a real software backend; no Metal device is fabricated.
 * Match this image's UUID and entry instructions, never a 20G81 address.
 */
static void springboard_scene_orientation_policy(void);
static void zizhan_cllocation_policy(void);
static void compositor_image(const struct mach_header *header,intptr_t slide)
{
    brightness18_image(header,slide);
    phone_gestalt_image(header,slide);
    phone_presentation();
    phone_selector_image(header,slide);
    stui_phone_image(header,slide);
    wallpaper_resource_policy();
    container_persona_policy();
    springboard_platform_policy();
    springboard_setup_policy();
    springboard_scene_orientation_policy();
    zizhan_cllocation_policy();
    launchservices_directories();
    installd_directory();
    installd_codesign_policy();
    pluginkit_directory();
    if((!software_composition && !scanout_format) || ((!software_composition || renderer_installed) && format_installed) || header->magic!=MH_MAGIC_64)return;
    const struct mach_header_64 *h=(const struct mach_header_64 *)header;
    static const unsigned char expected_uuid[16]={
        0xfa,0xf7,0xd7,0xea,0x18,0xf0,0x3c,0x2d,0x81,0x6c,0xac,0x56,0xd1,0x6c,0x40,0xa3};
    const struct load_command *lc=(const void *)(h+1);
    const char *end=(const char *)lc+h->sizeofcmds;
    bool matched=false;
    for(uint32_t i=0;i<h->ncmds;i++) {
        if((const char *)lc+sizeof(*lc)>end || lc->cmdsize<sizeof(*lc) ||
           lc->cmdsize>(size_t)(end-(const char *)lc))return;
        if(lc->cmd==LC_UUID && lc->cmdsize==sizeof(struct uuid_command))
            matched=memcmp(((const struct uuid_command *)lc)->uuid,expected_uuid,16)==0;
        lc=(const void *)((const char *)lc+lc->cmdsize);
    }
    if(!matched)return;
    static const uint32_t expected[]={0xa9be4ff4,0xa9017bfd};
    static const uint32_t cpu_renderer[]={0xd2800000,0xd65f03c0}; /* return nullptr */
    if(software_composition && !renderer_installed)renderer_installed=install_compositor_code(header,slide,
        0x184393468ULL-0x184339000ULL,expected,cpu_renderer,sizeof(expected),"stock CPU compositor");
    /* The 18.x IOMFBDisplay format method branches to this implementation.
     * Match its own prologue, not the four-byte thunk or 20G81 vtable.
     * MiniIOMFB's CPU backend consumes uncompressed eight-bit BGRA only. */
    static const uint32_t format_expected[]={0xa9bd57f6,0xa9014ff4,0xa9027bfd};
    static const uint32_t bgra_format[]={0x528a4820,0x72a848e0,0xd65f03c0};
    if(!format_installed)format_installed=install_compositor_code(header,slide,
        0x184396060ULL-0x184339000ULL,format_expected,bgra_format,sizeof(format_expected),"software scanout BGRA");
}

static id singleton(const char *name)
{
    Class cls=objc_lookUpClass(name);SEL selector=sel_registerName("sharedInstance");
    return method_abi(cls?class_getClassMethod(cls,selector):NULL,"@",0,"")?
        ((id(*)(id,SEL))objc_msgSend)((id)cls,selector):nil;
}

/* Use the already-created controller. The 22H374 volumeControl getter is an
 * ivar load; preserve that ownership rather than creating a controller from a
 * boot timer. Resolve the object ivar afresh on the main queue, without
 * constructing it. */
static id volume18_control(void)
{
    Class cls=objc_lookUpClass("SpringBoard");
    SEL shared=sel_registerName("sharedApplication");
    if(!method_abi(cls?class_getClassMethod(cls,shared):NULL,"@",0,""))return nil;
    id app=((id(*)(id,SEL))objc_msgSend)((id)cls,shared);
    Class application=app?object_getClass(app):Nil;
    Ivar ivar=application?class_getInstanceVariable(application,"_volumeControl"):NULL;
    const char *type=ivar?ivar_getTypeEncoding(ivar):NULL;
    ptrdiff_t offset=ivar?ivar_getOffset(ivar):-1;
    if(!type || type[0]!='@' || offset<0 ||
       (size_t)offset+sizeof(id)>class_getInstanceSize(application))return nil;
    id control=object_getIvar(app,ivar);
    Class expected=objc_lookUpClass("SBVolumeControl");
    for(Class actual=control?object_getClass(control):Nil;actual;actual=class_getSuperclass(actual))
        if(actual==expected)return control;
    return nil;
}

static struct { uint64_t presses; bool held; } volume18_keys[2];
static bool volume18_ready;
static bool volume18_has_cancel;
static int volume18_ready_token=-1;
static const char *volume18_names[]={"dev.unixnu.pong.volume18.increase","dev.unixnu.pong.volume18.decrease"};
static void volume18_cancel(void)
{
    id control=volume18_control();
    SEL cancel=sel_registerName("cancelVolumeEvent");
    if(!control || !method_abi(class_getInstanceMethod(object_getClass(control),cancel),"v",0,""))return;
    ((void(*)(id,SEL))objc_msgSend)(control,cancel);
}
static bool volume18_step(unsigned index)
{
    id control=volume18_control();
    SEL action=sel_registerName(index?"decreaseVolume":"increaseVolume");
    if(!control || !method_abi(class_getInstanceMethod(object_getClass(control),action),"v",0,""))return false;
    ((void(*)(id,SEL))objc_msgSend)(control,action);
    if(volume18_has_cancel)volume18_cancel();
    return true;
}
static void volume18_repeat(void *context)
{
    uintptr_t value=(uintptr_t)context;unsigned index=value&1;
    if(!volume18_keys[index].held || volume18_keys[index].presses!=(value>>1))return;
    if(volume18_step(index))dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,100*NSEC_PER_MSEC),
        dispatch_get_main_queue(),context,volume18_repeat);
}
static void volume18_prepare(void *context)
{
    if(volume18_ready)return;
    unsigned attempt=(unsigned)(uintptr_t)context;
    id control=volume18_control();
    Class cls=control?object_getClass(control):Nil;
    if(!cls ||
       !method_abi(class_getInstanceMethod(cls,sel_registerName("increaseVolume")),"v",0,"") ||
       !method_abi(class_getInstanceMethod(cls,sel_registerName("decreaseVolume")),"v",0,"")) {
        if(attempt<30)dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC),
            dispatch_get_main_queue(),(void *)(uintptr_t)(attempt+1),volume18_prepare);
        else fprintf(stderr,"Pong volume18: matching SBVolumeControl ABI unavailable; keep consumer HID\n");
        return;
    }
    volume18_has_cancel=method_abi(class_getInstanceMethod(cls,sel_registerName("cancelVolumeEvent")),"v",0,"");
    for(unsigned i=0;i<2;i++) {
        int token=-1;
        if(notify_register_dispatch(volume18_names[i],&token,dispatch_get_main_queue(),^(int changed){
            uint64_t state=0;if(notify_get_state(changed,&state))return;
            uint64_t count=state>>1,old=volume18_keys[i].presses;
            bool was_held=volume18_keys[i].held;
            volume18_keys[i].held=(state&1)!=0;volume18_keys[i].presses=count;
            // A restarted backboardd begins a new counter. Consume the new
            // click, never replay an old queue or synthesize a device report.
            // 22H374 SBVolumeControl increaseVolume/decreaseVolume arm an
            // NSRunLoop performSelector repeat until cancelVolumeEvent runs.
            uint64_t steps=count>=old?count-old:count;
            if(steps>8)steps=8;
            if(steps && volume18_has_cancel && !volume18_keys[1u-i].held)volume18_cancel();
            for(uint64_t n=0;n<steps;n++)(void)volume18_step(i);
            if(!volume18_keys[i].held && (steps || was_held) && !volume18_keys[1u-i].held)
                volume18_cancel();
            if(steps && volume18_keys[i].held)dispatch_after_f(
                dispatch_time(DISPATCH_TIME_NOW,400*NSEC_PER_MSEC),dispatch_get_main_queue(),
                (void *)(uintptr_t)((count<<1)|i),volume18_repeat);
            fprintf(stderr,"Pong volume18: native %s presses=%llu held=%u\n",i?"decrease":"increase",
                (unsigned long long)steps,volume18_keys[i].held);
        })!=NOTIFY_STATUS_OK)return;
    }
    if(notify_register_check("dev.unixnu.pong.volume18.ready",&volume18_ready_token)!=NOTIFY_STATUS_OK)return;
    volume18_ready=true;(void)notify_set_state(volume18_ready_token,1);
    fprintf(stderr,"Pong volume18: physical volume keys routed to 22H374 SBVolumeControl; power uses HID\n");
}

struct audio18_aq_ctx {
    OSStatus (*enqueue)(AudioQueueRef, AudioQueueBufferRef, UInt32, const AudioStreamPacketDescription *);
    double phase;
    double step;
    unsigned callbacks;
    unsigned max_callbacks;
};

static void audio18_aq_fill(struct audio18_aq_ctx *ctx, AudioQueueBufferRef buf)
{
    const unsigned frames = 4800u;
    float *dst = (float *)buf->mAudioData;
    for (unsigned i = 0; i < frames; ++i) {
        float s = 0.5f * sinf((float)ctx->phase);
        dst[2 * i] = s;
        dst[2 * i + 1] = s;
        ctx->phase += ctx->step;
        if (ctx->phase > 6.283185307179586) ctx->phase -= 6.283185307179586;
    }
    buf->mAudioDataByteSize = frames * 2u * (UInt32)sizeof(float);
}

static void audio18_aq_cb(void *user, AudioQueueRef q, AudioQueueBufferRef buf)
{
    struct audio18_aq_ctx *ctx = (struct audio18_aq_ctx *)user;
    if (!ctx || ctx->callbacks >= ctx->max_callbacks) return;
    ctx->callbacks++;
    audio18_aq_fill(ctx, buf);
    (void)ctx->enqueue(q, buf, 0, NULL);
}

static void audio18_test_session(void *context)
{
    unsigned session = (unsigned)(uintptr_t)context;
    void *atb = dlopen("/System/Library/Frameworks/AudioToolbox.framework/AudioToolbox", RTLD_LAZY);
    typedef OSStatus (*aq_new_fn)(const AudioStreamBasicDescription *, AudioQueueOutputCallback, void *, CFRunLoopRef, CFStringRef, UInt32, AudioQueueRef *);
    typedef OSStatus (*aq_alloc_fn)(AudioQueueRef, UInt32, AudioQueueBufferRef *);
    typedef OSStatus (*aq_enq_fn)(AudioQueueRef, AudioQueueBufferRef, UInt32, const AudioStreamPacketDescription *);
    typedef OSStatus (*aq_start_fn)(AudioQueueRef, const AudioTimeStamp *);
    typedef OSStatus (*aq_stop_fn)(AudioQueueRef, Boolean);
    typedef OSStatus (*aq_disp_fn)(AudioQueueRef, Boolean);
    aq_new_fn fn_new = atb ? (aq_new_fn)dlsym(atb, "AudioQueueNewOutput") : NULL;
    aq_alloc_fn fn_alloc = atb ? (aq_alloc_fn)dlsym(atb, "AudioQueueAllocateBuffer") : NULL;
    aq_enq_fn fn_enq = atb ? (aq_enq_fn)dlsym(atb, "AudioQueueEnqueueBuffer") : NULL;
    aq_start_fn fn_start = atb ? (aq_start_fn)dlsym(atb, "AudioQueueStart") : NULL;
    aq_stop_fn fn_stop = atb ? (aq_stop_fn)dlsym(atb, "AudioQueueStop") : NULL;
    aq_disp_fn fn_disp = atb ? (aq_disp_fn)dlsym(atb, "AudioQueueDispose") : NULL;
    if (!fn_new || !fn_alloc || !fn_enq || !fn_start || !fn_stop || !fn_disp) {
        fprintf(stderr, "Pong audio18: session=%u AudioToolbox symbols unavailable\n", session);
        return;
    }
    const double hz = (session == 1u) ? 440.0 : 660.0;
    AudioStreamBasicDescription fmt = {0};
    fmt.mSampleRate = 48000.0;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    fmt.mBytesPerPacket = 8;
    fmt.mFramesPerPacket = 1;
    fmt.mBytesPerFrame = 8;
    fmt.mChannelsPerFrame = 2;
    fmt.mBitsPerChannel = 32;
    struct audio18_aq_ctx ctx = { .enqueue = fn_enq, .phase = 0.0, .step = 6.283185307179586 * hz / 48000.0, .callbacks = 0, .max_callbacks = 16 };
    AudioQueueRef q = NULL;
    OSStatus st_new = fn_new(&fmt, audio18_aq_cb, &ctx, NULL, NULL, 0, &q);
    OSStatus st_start = -1, st_stop = -1;
    if (st_new == 0 && q) {
        for (unsigned i = 0; i < 3; ++i) {
            AudioQueueBufferRef b = NULL;
            if (fn_alloc(q, 4800u * 8u, &b) == 0 && b) {
                audio18_aq_fill(&ctx, b);
                (void)fn_enq(q, b, 0, NULL);
            }
        }
        st_start = fn_start(q, NULL);
        usleep(1500000);
        ctx.max_callbacks = 0;
        st_stop = fn_stop(q, true);
        (void)fn_disp(q, true);
    }
    fprintf(stderr, "Pong audio18: test session=%u hz=%.0f new=%d start=%d callbacks=%u stop=%d\n",
            session, hz, (int)st_new, (int)st_start, ctx.callbacks, (int)st_stop);
}

struct audio18_cap_ctx {
    OSStatus (*enqueue)(AudioQueueRef, AudioQueueBufferRef, UInt32, const AudioStreamPacketDescription *);
    unsigned callbacks;
    unsigned max_callbacks;
    unsigned total_frames;
    float max_abs;
};

typedef void (*AudioQueueInputCbFn)(void *, AudioQueueRef, AudioQueueBufferRef, const AudioTimeStamp *, UInt32, const AudioStreamPacketDescription *);

static void audio18_cap_cb(void *user, AudioQueueRef q, AudioQueueBufferRef buf,
                           const AudioTimeStamp *start_time, UInt32 num_packets,
                           const AudioStreamPacketDescription *pkt_desc)
{
    (void)start_time; (void)num_packets; (void)pkt_desc;
    struct audio18_cap_ctx *ctx = (struct audio18_cap_ctx *)user;
    if (!ctx || !buf || ctx->callbacks >= ctx->max_callbacks) return;
    ctx->callbacks++;
    const unsigned frames = buf->mAudioDataByteSize / (UInt32)sizeof(float);
    const float *samples = (const float *)buf->mAudioData;
    ctx->total_frames += frames;
    for (unsigned i = 0; i < frames; ++i) {
        float a = fabsf(samples[i]);
        if (a > ctx->max_abs) ctx->max_abs = a;
    }
    (void)ctx->enqueue(q, buf, 0, NULL);
}

static void audio18_cap_test_session(void *context)
{
    unsigned session = (unsigned)(uintptr_t)context;
    void *atb = dlopen("/System/Library/Frameworks/AudioToolbox.framework/AudioToolbox", RTLD_LAZY);
    typedef OSStatus (*aq_new_in_fn)(const AudioStreamBasicDescription *, AudioQueueInputCbFn, void *, CFRunLoopRef, CFStringRef, UInt32, AudioQueueRef *);
    typedef OSStatus (*aq_alloc_fn)(AudioQueueRef, UInt32, AudioQueueBufferRef *);
    typedef OSStatus (*aq_enq_fn)(AudioQueueRef, AudioQueueBufferRef, UInt32, const AudioStreamPacketDescription *);
    typedef OSStatus (*aq_start_fn)(AudioQueueRef, const AudioTimeStamp *);
    typedef OSStatus (*aq_stop_fn)(AudioQueueRef, Boolean);
    typedef OSStatus (*aq_disp_fn)(AudioQueueRef, Boolean);
    aq_new_in_fn fn_new = atb ? (aq_new_in_fn)dlsym(atb, "AudioQueueNewInput") : NULL;
    aq_alloc_fn fn_alloc = atb ? (aq_alloc_fn)dlsym(atb, "AudioQueueAllocateBuffer") : NULL;
    aq_enq_fn fn_enq = atb ? (aq_enq_fn)dlsym(atb, "AudioQueueEnqueueBuffer") : NULL;
    aq_start_fn fn_start = atb ? (aq_start_fn)dlsym(atb, "AudioQueueStart") : NULL;
    aq_stop_fn fn_stop = atb ? (aq_stop_fn)dlsym(atb, "AudioQueueStop") : NULL;
    aq_disp_fn fn_disp = atb ? (aq_disp_fn)dlsym(atb, "AudioQueueDispose") : NULL;
    if (!fn_new || !fn_alloc || !fn_enq || !fn_start || !fn_stop || !fn_disp) {
        fprintf(stderr, "Pong audio18: capture session=%u AudioToolbox symbols unavailable\n", session);
        return;
    }
    AudioStreamBasicDescription fmt = {0};
    fmt.mSampleRate = 48000.0;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    fmt.mBytesPerPacket = 4;
    fmt.mFramesPerPacket = 1;
    fmt.mBytesPerFrame = 4;
    fmt.mChannelsPerFrame = 1;
    fmt.mBitsPerChannel = 32;
    struct audio18_cap_ctx ctx = { .enqueue = fn_enq, .callbacks = 0, .max_callbacks = 32, .total_frames = 0, .max_abs = 0.0f };
    AudioQueueRef q = NULL;
    OSStatus st_new = fn_new(&fmt, audio18_cap_cb, &ctx, NULL, NULL, 0, &q);
    OSStatus st_start = -1, st_stop = -1;
    if (st_new == 0 && q) {
        for (unsigned i = 0; i < 3; ++i) {
            AudioQueueBufferRef b = NULL;
            if (fn_alloc(q, 4800u * 4u, &b) == 0 && b) {
                (void)fn_enq(q, b, 0, NULL);
            }
        }
        st_start = fn_start(q, NULL);
        usleep(1600000);
        ctx.max_callbacks = 0;
        st_stop = fn_stop(q, true);
        (void)fn_disp(q, true);
    }
    fprintf(stderr, "Pong audio18: capture session=%u new=%d start=%d callbacks=%u frames=%u max_abs=%.5f stop=%d\n",
            session, (int)st_new, (int)st_start, ctx.callbacks, ctx.total_frames, (double)ctx.max_abs, (int)st_stop);
}

static bool zizhan_bringup_preferences(void)
{
    const bool bringup=boot_option("zizhan_bringup=1");
    const bool legacy=boot_option("zizhan_autodim_restore=1");
    CFTypeRef current=CFPreferencesCopyAppValue(ZIZHAN_AUTODIM_KEY,kCFPreferencesCurrentApplication);
    CFTypeRef marker=CFPreferencesCopyAppValue(ZIZHAN_AUTODIM_OWNER_KEY,kCFPreferencesCurrentApplication);
    ZizhanBringupPlan plan=zizhan_bringup_plan(bringup,legacy,current,marker);
    bool synchronized=true;
    if(plan.write_marker)CFPreferencesSetAppValue(
        ZIZHAN_AUTODIM_OWNER_KEY,plan.marker,kCFPreferencesCurrentApplication);
    if(plan.write_value)CFPreferencesSetAppValue(
        ZIZHAN_AUTODIM_KEY,plan.value,kCFPreferencesCurrentApplication);
    if((plan.write_marker || plan.write_value) &&
       !CFPreferencesAppSynchronize(kCFPreferencesCurrentApplication)) {
        /* Retain both original values on failed persistence; do not claim a
         * completed migration or schedule UI actions from a failed plan. */
        synchronized=false;
        CFPreferencesSetAppValue(ZIZHAN_AUTODIM_KEY,current,kCFPreferencesCurrentApplication);
        CFPreferencesSetAppValue(ZIZHAN_AUTODIM_OWNER_KEY,marker,kCFPreferencesCurrentApplication);
        (void)CFPreferencesAppSynchronize(kCFPreferencesCurrentApplication);
    }
    fprintf(stderr,"Zizhan bringup18: %s reason=%s bringup=%u legacy=%u wake=%u\n",
        synchronized?plan.status:"FAIL",synchronized?plan.reason:"preference-sync",
        bringup,legacy,(unsigned)(plan.request_wake && synchronized));
    const bool wake=plan.request_wake && synchronized;
    zizhan_bringup_plan_release(&plan);
    if(marker)CFRelease(marker);
    if(current)CFRelease(current);
    return wake;
}

static void wake_home(void *unused)
{
    (void)unused;
    id screen=((id(*)(id,SEL))objc_msgSend)((id)objc_lookUpClass("UIScreen"),sel_registerName("mainScreen"));
    if(screen) {
        CGRect native=((CGRect(*)(id,SEL))objc_msgSend)(screen,sel_registerName("nativeBounds"));
        CGRect logical=((CGRect(*)(id,SEL))objc_msgSend)(screen,sel_registerName("bounds"));
        double scale=((double(*)(id,SEL))objc_msgSend)(screen,sel_registerName("scale"));
        double nativeScale=((double(*)(id,SEL))objc_msgSend)(screen,sel_registerName("nativeScale"));
        fprintf(stderr,"Pong UI18: screen native=%.0fx%.0f logical=%.2fx%.2f scale=%.2f nativeScale=%.2f\n",
            native.size.width,native.size.height,logical.size.width,logical.size.height,scale,nativeScale);
    }
    id backlight=singleton("SBBacklightController"),lock=singleton("SBLockScreenManager");
    SEL hold=sel_registerName("preventIdleSleepForNumberOfSeconds:");
    if(backlight && method_abi(class_getInstanceMethod(object_getClass(backlight),hold),"v",1,"f"))
        ((void(*)(id,SEL,float))objc_msgSend)(backlight,hold,300.0f);
    SEL wake=sel_registerName("turnOnScreenFullyWithBacklightSource:");
    if(backlight && method_abi(class_getInstanceMethod(object_getClass(backlight),wake),"v",1,"q"))
        ((void(*)(id,SEL,long long))objc_msgSend)(backlight,wake,0);
    SEL locked=sel_registerName("isUILocked"),unlock=sel_registerName("unlockUIFromSource:withOptions:");
    if(lock && method_abi(class_getInstanceMethod(object_getClass(lock),locked),"Bc",0,"") &&
       ((BOOL(*)(id,SEL))objc_msgSend)(lock,locked) &&
       method_abi(class_getInstanceMethod(object_getClass(lock),unlock),"Bc",2,"i@")) {
        BOOL accepted=((BOOL(*)(id,SEL,int,id))objc_msgSend)(lock,unlock,24,nil);
        fprintf(stderr,"Pong UI18: native home request accepted=%d\n",accepted);
    }
    fprintf(stderr,"Pong UI18: main queue wake completed backlight=%d lockscreen=%d\n",backlight!=nil,lock!=nil);
}

/* Value of boot-arg KEY=<value> (KEY includes the '='), copied into out; false when absent. */
static bool boot_value(const char *key,char *out,size_t size)
{
    const char *args=boot_args();
    if(!args)return false;
    for(const char *p=args;(p=strstr(p,key));p++) {
        if(p!=args && p[-1]!=' ' && p[-1]!='\t')continue;
        p+=strlen(key);size_t n=strcspn(p," \t");
        if(n>=size)n=size-1;
        memcpy(out,p,n);out[n]='\0';return true;
    }
    return false;
}

/* Boot-arg pong_url=<s>: <s> seconds after SpringBoard starts, open Settings at pane pong_pane (default BATTERY_USAGE) without a
 * finger, so an unattended round can sample the pane (11-01 v6: Settings > Battery hangs its main thread). The open goes
 * through lsd/FrontBoard back to SpringBoard, so it is sent from a global queue, never SpringBoard's main queue. */
static void open_pane(void *unused)
{
    (void)unused;
    char pane[64]="BATTERY_USAGE",url[96];
    (void)boot_value("pong_pane=",pane,sizeof(pane));
    // One-letter aliases: the kernel command line has a few bytes left (11-01 v10: Battery Health kept spinning).
    if(!strcmp(pane,"H"))snprintf(pane,sizeof(pane),"BATTERY_USAGE&path=BATTERY_HEALTH");
    snprintf(url,sizeof(url),"prefs:root=%s",pane);
    Class workspace=objc_lookUpClass("LSApplicationWorkspace");
    SEL open=sel_registerName("openSensitiveURL:withOptions:");
    id ws=workspace?((id(*)(id,SEL))objc_msgSend)((id)workspace,sel_registerName("defaultWorkspace")):nil;
    CFURLRef target=CFURLCreateWithBytes(NULL,(const UInt8 *)url,strlen(url),kCFStringEncodingUTF8,NULL);
    BOOL accepted=NO;
    if(ws && target && ((BOOL(*)(id,SEL,SEL))objc_msgSend)(ws,sel_registerName("respondsToSelector:"),open))
        accepted=((BOOL(*)(id,SEL,id,id))objc_msgSend)(ws,open,(id)target,nil);
    fprintf(stderr,"Pong UI18: open %s accepted=%d (workspace %d)\n",url,accepted,ws!=nil);
    if(target)CFRelease(target);
}

/* Boot-arg pong_open=<url>: open that URL after SpringBoard starts. An https
 * URL opens Safari, so an unattended round can load a real page without
 * tapping a home-screen icon whose position it would otherwise have to guess.
 * The value cannot contain spaces (boot arguments are space separated). */
static char pending_open_url[192];
/* App launch must start on SpringBoard's main run loop: the same calls made
 * from a global queue returned 0 in the v14 device round, while pong_url's
 * prefs: URL (handled inside SpringBoard) still worked there. */
static void open_url_on_main(void *unused)
{
    (void)unused;
    if(!pending_open_url[0])return;
    Class workspace=objc_lookUpClass("LSApplicationWorkspace");
    id ws=workspace?((id(*)(id,SEL))objc_msgSend)((id)workspace,sel_registerName("defaultWorkspace")):nil;
    if(!strncmp(pending_open_url,"bundle:",7) && pending_open_url[7]) {
        SEL app=sel_registerName("openApplicationWithBundleID:");
        CFStringRef identifier=CFStringCreateWithCString(NULL,pending_open_url+7,kCFStringEncodingUTF8);
        BOOL ok=NO;
        if(ws && identifier && ((BOOL(*)(id,SEL,SEL))objc_msgSend)(ws,sel_registerName("respondsToSelector:"),app))
            ok=((BOOL(*)(id,SEL,id))objc_msgSend)(ws,app,(id)identifier);
        if(!ok && identifier) {
            Class uiAppCls=objc_lookUpClass("UIApplication");
            id sb=uiAppCls?((id(*)(Class,SEL))objc_msgSend)(uiAppCls,sel_registerName("sharedApplication")):nil;
            SEL sLaunch=sel_registerName("launchApplicationWithIdentifier:suspended:");
            if(sb && ((BOOL(*)(id,SEL,SEL))objc_msgSend)(sb,sel_registerName("respondsToSelector:"),sLaunch))
                ok=((BOOL(*)(id,SEL,id,BOOL))objc_msgSend)(sb,sLaunch,(id)identifier,NO);
        }
        static unsigned bundle_retries;
        if(!ok && bundle_retries<2) {
            bundle_retries++;
            dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,6*NSEC_PER_SEC),dispatch_get_main_queue(),NULL,open_url_on_main);
        }
        fprintf(stderr,"Pong UI18: open-bundle %s returned=%d retry=%u (workspace %d)\n",
            pending_open_url+7,(int)ok,bundle_retries,ws!=nil);
        if(identifier)CFRelease(identifier);
        return;
    }
    CFURLRef target=CFURLCreateWithBytes(NULL,(const UInt8 *)pending_open_url,strlen(pending_open_url),kCFStringEncodingUTF8,NULL);
    BOOL accepted=NO;
    SEL open=sel_registerName("openSensitiveURL:withOptions:");
    Method openMethod=ws?class_getInstanceMethod(object_getClass(ws),open):NULL;
    fprintf(stderr,"Pong UI18: URL workspace ABI %s\n",openMethod?method_getTypeEncoding(openMethod):"unavailable");
    if(ws && target && ((BOOL(*)(id,SEL,SEL))objc_msgSend)(ws,sel_registerName("respondsToSelector:"),open))
        accepted=((BOOL(*)(id,SEL,id,id))objc_msgSend)(ws,open,(id)target,nil);
    fprintf(stderr,"Pong UI18: open-sensitive %s ok=%d (workspace %d)\n",pending_open_url,accepted,ws!=nil);
    if(!accepted && ws && target) {
        SEL plain=sel_registerName("openURL:withOptions:");
        if(((BOOL(*)(id,SEL,SEL))objc_msgSend)(ws,sel_registerName("respondsToSelector:"),plain))
            accepted=((BOOL(*)(id,SEL,id,id))objc_msgSend)(ws,plain,(id)target,nil);
        fprintf(stderr,"Pong UI18: open-plain %s ok=%d\n",pending_open_url,accepted);
    }
    /* A false synchronous return did not mean the queued URL failed in the
     * actual Safari video round. Never redirect a custom scheme to Safari:
     * the old fallback covered Weather with the previous browser page. */
    const bool web=!strncmp(pending_open_url,"http://",7) || !strncmp(pending_open_url,"https://",8);
    const char *bundle=web?"com.apple.mobilesafari":
        !strncmp(pending_open_url,"weather:",8)?"com.apple.weather":NULL;
    if(!accepted && ws && bundle) {
        SEL app=sel_registerName("openApplicationWithBundleID:");
        CFStringRef identifier=CFStringCreateWithCString(NULL,bundle,kCFStringEncodingUTF8);
        if(identifier && ((BOOL(*)(id,SEL,SEL))objc_msgSend)(ws,sel_registerName("respondsToSelector:"),app))
            accepted=((BOOL(*)(id,SEL,id))objc_msgSend)(ws,app,(id)identifier);
        fprintf(stderr,"Pong UI18: open-app %s returned=%d; actual page requires observation\n",bundle,accepted);
        if(identifier)CFRelease(identifier);
    }
    if(target)CFRelease(target);
}
static void open_pending_url(void *unused)
{
    (void)unused;
    dispatch_async_f(dispatch_get_main_queue(),NULL,open_url_on_main);
}

static bool own_directory(const char *path)
{
    if(mkdir(path,0700) && errno!=EEXIST)return false;
    struct stat st;
    return lstat(path,&st)==0 && S_ISDIR(st.st_mode) && st.st_uid==geteuid();
}

static id installation_library(id self,SEL selector)
{ (void)self;(void)selector;return (id)installd_library; }

static id plugin_state_container(id self,SEL selector)
{ (void)self;(void)selector;return (id)pluginkit_container; }

/* The services20 exception stack ends at 22H374's
 * -[PKDAnnotationStore containerURL] + 0xd8: its class-10 com.apple.pkd
 * query fails with error 52. Give this daemon's annotation store a real
 * directory on Data. Keep the stock database filename, serialization,
 * extension discovery and app container/security providers unchanged.
 * A system or mobile instance uses its own UID-owned home, never another
 * user's container. An unavailable directory retains the native failure.
 */
static void pluginkit_directory(void)
{
    if(!pluginkit_process || pluginkit_directory_attempted)return;
    const uid_t uid=geteuid();
    if(uid!=0 && uid!=501)return;
    Class cls=objc_lookUpClass("PKDAnnotationStore");
    Method method=cls?class_getInstanceMethod(cls,sel_registerName("containerURL")):NULL;
    if(!method_abi(method,"@",0,""))return;
    pluginkit_directory_attempted=true;
    const char *home=uid==0?"/private/var/root/Library":"/private/var/mobile/Library";
    char parent[128],path[160];
    snprintf(parent,sizeof(parent),"%s/Pong",home);
    snprintf(path,sizeof(path),"%s/PlugInKit",parent);
    if(!own_directory(home) || !own_directory(parent) || !own_directory(path)) {
        fprintf(stderr,"Pong PK18: annotation directory unavailable uid=%u errno=%d\n",uid,errno);
        return;
    }
    pluginkit_container=CFURLCreateFromFileSystemRepresentation(NULL,
        (const UInt8 *)path,strlen(path),true);
    if(!pluginkit_container)return;
    method_setImplementation(method,(IMP)plugin_state_container);
    fprintf(stderr,"Pong PK18: native annotations use %s uid=%u\n",path,uid);
}

/* 22H374 InstalledContentLibrary's daemonUserDataLibraryDirectory queries
 * class-10 com.apple.installd with a persona, and fatally terminates when
 * the query fails. Use the actual _installd home from this build's passwd
 * on our single-user Data. Its original currentUserDataDirectory and
 * currentUserCachesDirectory append MobileInstallation and Caches to this
 * NSURL. Keep all installation, signature and app-container operations.
 * Resolve the Objective-C method at runtime; never write private ivars or
 * reuse the iOS16 framework's addresses.
 */
static void installd_directory(void)
{
    if(!installd_process || installd_directory_installed || geteuid()!=33)return;
    Class cls=objc_lookUpClass("MIDaemonConfiguration");
    Method method=cls?class_getInstanceMethod(cls,
        sel_registerName("daemonUserDataLibraryDirectory")):NULL;
    if(!method_abi(method,"@",0,""))return;
    const char *dirs[]={"/private/var/installd","/private/var/installd/Library",
        "/private/var/installd/Library/MobileInstallation",
        "/private/var/installd/Library/Caches"};
    for(unsigned i=0;i<sizeof(dirs)/sizeof(dirs[0]);i++) {
        if(!own_directory(dirs[i])) {
            fprintf(stderr,"Pong MI18: daemon directory unavailable %s errno=%d\n",dirs[i],errno);
            return;
        }
    }
    installd_library=CFURLCreateFromFileSystemRepresentation(NULL,
        (const UInt8 *)dirs[1],strlen(dirs[1]),true);
    if(!installd_library)return;
    method_setImplementation(method,(IMP)installation_library);
    installd_directory_installed=true;
    fprintf(stderr,"Pong MI18: native installd uses writable _installd Library\n");
}

/* Track 2 (APP-02): Allow decrypted (cryptid == 0) Ad-hoc / self-signed 4K & 16K third-party IPAs
 * through installd's MICodeSigningVerifier while preserving clean rejection of FairPlay-encrypted
 * (cryptid != 0) App Store binaries. Opt-out via boot-arg zizhan_ipa_install=0. */
static bool installd_codesign_installed;
static IMP mi_perform_val_original;
static IMP mi_validate_copy_original;

static int zizhan_mi_inspect_url(id url, ZizhanMachoReport *rep, id *ent_dict_out)
{
    if(!url)return ZIZHAN_IPA_ERR_ARGS;
    UInt8 pbuf[1024]={0};
    if(!CFURLGetFileSystemRepresentation((CFURLRef)url,true,pbuf,sizeof(pbuf)-1))return ZIZHAN_IPA_ERR_ARGS;
    struct stat st;
    if(lstat((const char *)pbuf,&st)!=0)return ZIZHAN_IPA_ERR_ARGS;
    char exec_path[1024]={0};
    if(S_ISDIR(st.st_mode)) {
        /* Bundle directory: read Info.plist CFBundleExecutable */
        char plist_path[1024];
        snprintf(plist_path,sizeof(plist_path),"%s/Info.plist",(const char *)pbuf);
        Class dict_cls=objc_lookUpClass("NSDictionary"),str_cls=objc_lookUpClass("NSString");
        id pstr=str_cls?((id(*)(Class,SEL,const char *))objc_msgSend)(str_cls,sel_registerName("stringWithUTF8String:"),plist_path):nil;
        id info=(dict_cls && pstr)?((id(*)(Class,SEL,id))objc_msgSend)(dict_cls,sel_registerName("dictionaryWithContentsOfFile:"),pstr):nil;
        id exe=info?((id(*)(id,SEL,id))objc_msgSend)(info,sel_registerName("objectForKey:"),(id)CFSTR("CFBundleExecutable")):nil;
        const char *ename=exe?((const char *(*)(id,SEL))objc_msgSend)(exe,sel_registerName("UTF8String")):NULL;
        if(!ename || !ename[0])return ZIZHAN_IPA_ERR_NOT_MACHO;
        snprintf(exec_path,sizeof(exec_path),"%s/%s",(const char *)pbuf,ename);
    } else {
        snprintf(exec_path,sizeof(exec_path),"%s",(const char *)pbuf);
    }
    int fd=open(exec_path,O_RDONLY);
    if(fd<0)return ZIZHAN_IPA_ERR_ARGS;
    struct stat est;
    if(fstat(fd,&est)!=0 || est.st_size<64 || est.st_size>(512LL<<20)){close(fd);return ZIZHAN_IPA_ERR_TRUNCATED;}
    size_t sz=(size_t)est.st_size;
    uint8_t *buf=(uint8_t *)malloc(sz);
    if(!buf){close(fd);return ZIZHAN_IPA_ERR_ARGS;}
    size_t off=0;
    while(off<sz) {
        ssize_t r=read(fd,buf+off,sz-off);
        if(r<=0)break;
        off+=(size_t)r;
    }
    close(fd);
    if(off!=sz){free(buf);return ZIZHAN_IPA_ERR_TRUNCATED;}
    int rc=zizhan_macho_inspect(buf,sz,rep);
    if(rc==ZIZHAN_IPA_OK && ent_dict_out && rep->entitlements_len>0 &&
       (uint64_t)rep->entitlements_off+rep->entitlements_len<=sz) {
        CFDataRef edata=CFDataCreate(NULL,buf+rep->entitlements_off,(CFIndex)rep->entitlements_len);
        if(edata) {
            CFPropertyListRef plist=CFPropertyListCreateWithData(NULL,edata,kCFPropertyListImmutable,NULL,NULL);
            if(plist) {
                if(CFGetTypeID(plist)==CFDictionaryGetTypeID())
                    *ent_dict_out=((id(*)(id,SEL))objc_msgSend)((id)plist,sel_registerName("autorelease"));
                else
                    CFRelease(plist);
            }
            CFRelease(edata);
        }
    }
    free(buf);
    return rc;
}

static BOOL zizhan_mi_perform_validation(id self,SEL selector,id *error)
{
    SEL s_bundle=sel_registerName("bundle"),s_exe=sel_registerName("executableURL");
    id bundle=((BOOL(*)(id,SEL,SEL))objc_msgSend)(self,sel_registerName("respondsToSelector:"),s_bundle)?
        ((id(*)(id,SEL))objc_msgSend)(self,s_bundle):nil;
    id exe_url=(bundle && ((BOOL(*)(id,SEL,SEL))objc_msgSend)(bundle,sel_registerName("respondsToSelector:"),s_exe))?
        ((id(*)(id,SEL))objc_msgSend)(bundle,s_exe):nil;
    ZizhanMachoReport rep;
    int rc=zizhan_mi_inspect_url(exe_url,&rep,NULL);
    if(rc==ZIZHAN_IPA_ERR_FAIRPLAY_ENCRYPTED) {
        fprintf(stderr,"Zizhan MI18: REJECT FairPlay encrypted Mach-O cryptid=%u\n",rep.cryptid);
        return ((BOOL(*)(id,SEL,id *))mi_perform_val_original)(self,selector,error);
    }
    if(rc==ZIZHAN_IPA_OK) {
        const char *yes_sels[]={"setAllowAdhocSigning:","setSkipProfileIDValidation:"};
        const char *no_sels[]={"setPerformOnlineAuthorization:","setVerifyTrustCachePresence:","setValidateResources:"};
        for(unsigned i=0;i<2;i++) {
            SEL s=sel_registerName(yes_sels[i]);
            if(((BOOL(*)(id,SEL,SEL))objc_msgSend)(self,sel_registerName("respondsToSelector:"),s))
                ((void(*)(id,SEL,BOOL))objc_msgSend)(self,s,YES);
        }
        for(unsigned i=0;i<3;i++) {
            SEL s=sel_registerName(no_sels[i]);
            if(((BOOL(*)(id,SEL,SEL))objc_msgSend)(self,sel_registerName("respondsToSelector:"),s))
                ((void(*)(id,SEL,BOOL))objc_msgSend)(self,s,NO);
        }
    }
    return ((BOOL(*)(id,SEL,id *))mi_perform_val_original)(self,selector,error);
}

static id zizhan_mi_validate_and_copy(id cls,SEL selector,id url,id options,id *error)
{
    ZizhanMachoReport rep;
    id ent_dict=nil;
    int rc=zizhan_mi_inspect_url(url,&rep,&ent_dict);
    if(rc==ZIZHAN_IPA_ERR_FAIRPLAY_ENCRYPTED) {
        return ((id(*)(id,SEL,id,id,id *))mi_validate_copy_original)(cls,selector,url,options,error);
    }
    id res=((id(*)(id,SEL,id,id,id *))mi_validate_copy_original)(cls,selector,url,options,error);
    if(rc!=ZIZHAN_IPA_OK)return res;

    const CFStringRef *k_cdhash=dlsym(RTLD_DEFAULT,"kMISValidationInfoCdHash");
    const CFStringRef *k_sigid=dlsym(RTLD_DEFAULT,"kMISValidationInfoSigningID");
    const CFStringRef *k_stype=dlsym(RTLD_DEFAULT,"kMISValidationInfoSignerType");
    const CFStringRef *k_ents=dlsym(RTLD_DEFAULT,"kMISValidationInfoEntitlements");
    const CFStringRef *k_native=dlsym(RTLD_DEFAULT,"kMISValidationInfoIsNativeForPlatform");

    CFMutableDictionaryRef info=res?
        CFDictionaryCreateMutableCopy(NULL,0,(CFDictionaryRef)res):
        CFDictionaryCreateMutable(NULL,8,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    if(!info)return res;

    const char *sid=rep.signing_id[0]?rep.signing_id:"com.zizhan.adhoc";
    if(!res) {
        CFDataRef cdh=CFDataCreate(NULL,rep.cdhash,20);
        if(cdh && k_cdhash && *k_cdhash)CFDictionarySetValue(info,*k_cdhash,cdh);
        if(cdh)CFRelease(cdh);

        CFStringRef sref=CFStringCreateWithCString(NULL,sid,kCFStringEncodingUTF8);
        if(sref && k_sigid && *k_sigid)CFDictionarySetValue(info,*k_sigid,sref);
        if(sref)CFRelease(sref);

        int stype=6; /* 22H374 libmis AdHoc=6 -> MICodeSignerType=5 */
        CFNumberRef ntype=CFNumberCreate(NULL,kCFNumberIntType,&stype);
        if(ntype && k_stype && *k_stype)CFDictionarySetValue(info,*k_stype,ntype);
        if(ntype)CFRelease(ntype);

        if(k_native && *k_native)CFDictionarySetValue(info,*k_native,kCFBooleanTrue);
    }

    /* Ensure every decrypted Ad-hoc bundle carries application-identifier so installd accepts bare codesign -s - IPAs */
    if(k_ents && *k_ents) {
        CFDictionaryRef cur_ents=(CFDictionaryRef)CFDictionaryGetValue(info,*k_ents);
        if(!cur_ents && ent_dict)cur_ents=(CFDictionaryRef)ent_dict;
        CFMutableDictionaryRef ments=cur_ents?
            CFDictionaryCreateMutableCopy(NULL,0,cur_ents):
            CFDictionaryCreateMutable(NULL,4,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
        if(ments) {
            if(!CFDictionaryGetValue(ments,CFSTR("application-identifier"))) {
                char appid[320];
                snprintf(appid,sizeof(appid),"ZIZHAN0000.%s",sid);
                CFStringRef aref=CFStringCreateWithCString(NULL,appid,kCFStringEncodingUTF8);
                if(aref){CFDictionarySetValue(ments,CFSTR("application-identifier"),aref);CFRelease(aref);}
            }
            if(!CFDictionaryGetValue(ments,CFSTR("keychain-access-groups"))) {
                const void *kag[2]={CFSTR("ZIZHAN0000.*"),CFSTR("*")};
                CFArrayRef arr=CFArrayCreate(NULL,kag,2,&kCFTypeArrayCallBacks);
                if(arr){CFDictionarySetValue(ments,CFSTR("keychain-access-groups"),arr);CFRelease(arr);}
            }
            if(!CFDictionaryGetValue(ments,CFSTR("com.apple.security.application-groups"))) {
                const void *ag[1]={CFSTR("group.com.zizhan.shared")};
                CFArrayRef arr=CFArrayCreate(NULL,ag,1,&kCFTypeArrayCallBacks);
                if(arr){CFDictionarySetValue(ments,CFSTR("com.apple.security.application-groups"),arr);CFRelease(arr);}
            }
            CFDictionarySetValue(info,*k_ents,ments);
            CFRelease(ments);
        }
    }

    if(!res && error)*error=nil;
    fprintf(stderr,"Zizhan MI18: validated decrypted Mach-O id=%s page_bytes=%u orig_ok=%d\n",sid,rep.cd_page_bytes,res!=nil);
    return ((id(*)(id,SEL))objc_msgSend)((id)info,sel_registerName("autorelease"));
}

static void installd_codesign_policy(void)
{
    if(!installd_process || installd_codesign_installed || boot_option("zizhan_ipa_install=0"))return;
    Class cls=objc_lookUpClass("MICodeSigningVerifier");
    if(!cls)return;
    Method m_perf=class_getInstanceMethod(cls,sel_registerName("performValidationWithError:"));
    Method m_copy=class_getClassMethod(cls,sel_registerName("_validateSignatureAndCopyInfoForURL:withOptions:error:"));
    if(!m_perf || !m_copy)return;
    mi_perform_val_original=method_setImplementation(m_perf,(IMP)zizhan_mi_perform_validation);
    mi_validate_copy_original=method_setImplementation(m_copy,(IMP)zizhan_mi_validate_and_copy);
    installd_codesign_installed=mi_perform_val_original!=NULL && mi_validate_copy_original!=NULL;
    fprintf(stderr,"Zizhan MI18: decrypted 4K/16K IPA MICodeSigningVerifier hook=%d\n",(int)installd_codesign_installed);
}

static id ls_directory(id self,SEL selector)
{
    (void)self;
    if(selector==sel_registerName("systemContainerBaseURL"))return (id)ls_base;
    if(selector==sel_registerName("systemContainerURL"))return (id)ls_system;
    if(selector==sel_registerName("systemGroupContainerURL"))return (id)ls_shared;
    return (id)ls_user;
}

/* 22H374 databaseContainerURL selects userContainerURL for an ordinary lsd.
 * LS_DATABASE_DIR covers only __LSDefaultsBaseSystemContainerURL, so it
 * cannot avoid userContainerURL's class-10 persona query -> abort path.
 * Apply the port's single-user storage policy to this server's own state,
 * preserving the original database/schema, app containers and XPC methods.
 * These are real directories, not successful replies from a fake MCM daemon.
 */
static void zizhan_purge_csstore_tree(const char *root, unsigned depth)
{
    if(depth>4)return;
    DIR *d=opendir(root);
    if(!d)return;
    struct dirent *de;
    while((de=readdir(d))!=NULL) {
        if(!strcmp(de->d_name,".") || !strcmp(de->d_name,".."))continue;
        char sub[512];
        snprintf(sub,sizeof(sub),"%s/%s",root,de->d_name);
        struct stat st;
        if(lstat(sub,&st)!=0)continue;
        if(S_ISDIR(st.st_mode)) {
            zizhan_purge_csstore_tree(sub,depth+1);
        } else if(strstr(de->d_name,".csstore") || strstr(de->d_name,"com.apple.LaunchServices")) {
            (void)unlink(sub);
        }
    }
    closedir(d);
}

static void launchservices_directories(void)
{
    if(!launchservices_process || launchservices_installed || geteuid()!=501)return;
    Class cls=objc_lookUpClass("_LSDefaults");
    const char *names[]={"systemContainerBaseURL","systemContainerURL",
        "userContainerURL","systemGroupContainerURL"};
    Method methods[4];
    for(unsigned i=0;i<4;i++) {
        methods[i]=cls?class_getInstanceMethod(cls,sel_registerName(names[i])):NULL;
        if(!method_abi(methods[i],"@",0,""))return;
    }
    const char *dirs[]={
        "/private/var/mobile/Library/Pong",
        "/private/var/mobile/Library/Pong/LaunchServices",
        "/private/var/mobile/Library/Pong/LaunchServices/Library",
        "/private/var/mobile/Library/Pong/LaunchServices/Library/Caches",
        "/private/var/mobile/Library/Pong/LaunchServices/User",
        "/private/var/mobile/Library/Pong/LaunchServices/User/Library",
        "/private/var/mobile/Library/Pong/LaunchServices/User/Library/Caches",
        "/private/var/mobile/Library/Pong/LaunchServices/Shared",
        "/private/var/mobile/Library/Pong/LaunchServices/Shared/Library",
        "/private/var/mobile/Library/Pong/LaunchServices/Shared/Library/Caches"
    };
    for(unsigned i=0;i<sizeof(dirs)/sizeof(dirs[0]);i++) {
        if(!own_directory(dirs[i])) {
            fprintf(stderr,"Pong LS18: writable directory unavailable %s errno=%d\n",dirs[i],errno);
            return;
        }
    }
    /* One-shot LaunchServices cache refresh when ZizhanInstaller.app v3 is updated in /Applications */
    struct stat ist,mst;
    const char *stamp="/private/var/mobile/Library/Pong/LaunchServices/.zizhan_installer_v3";
    if(stat("/Applications/ZizhanInstaller.app/Info.plist",&ist)==0 && stat(stamp,&mst)!=0) {
        zizhan_purge_csstore_tree("/private/var/mobile/Library/Pong/LaunchServices",0);
        int sfd=open(stamp,O_CREAT|O_WRONLY|O_TRUNC,0600);
        if(sfd>=0){(void)write(sfd,"1\n",2);close(sfd);}
        fprintf(stderr,"Pong LS18: purged stale csstore to index ZizhanInstaller.app\n");
    }
    const char *paths[]={dirs[1],dirs[3],dirs[6],dirs[9]};
    CFURLRef urls[4]={0};
    for(unsigned i=0;i<4;i++) {
        urls[i]=CFURLCreateFromFileSystemRepresentation(NULL,(const UInt8 *)paths[i],strlen(paths[i]),true);
        if(!urls[i]) {
            for(unsigned j=0;j<i;j++)CFRelease(urls[j]);
            return;
        }
    }
    ls_base=urls[0];ls_system=urls[1];ls_user=urls[2];ls_shared=urls[3];
    for(unsigned i=0;i<4;i++)method_setImplementation(methods[i],(IMP)ls_directory);
    launchservices_installed=true;
    fprintf(stderr,"Pong LS18: native lsd database uses writable single-user paths\n");
}

/* Keep the real fatal signal. Retain bounded frames because ReportCrash's
 * osanalytics backend is absent in this port; never resume after SIGABRT. */
static void lsd_abort(int number,siginfo_t *info,void *saved)
{
    (void)info;
    ucontext_t *context=saved;
    char line[160];uint64_t fp=0;
    if(context && context->uc_mcontext) {
        int n=snprintf(line,sizeof(line),"Pong LS18: fatal signal=%d pc=%llx lr=%llx\n",
            number,(unsigned long long)context->uc_mcontext->__ss.__pc,
            (unsigned long long)context->uc_mcontext->__ss.__lr);
        if(n>0)(void)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
        fp=context->uc_mcontext->__ss.__fp;
    }
    for(unsigned i=0;i<12 && fp>=0x100000000ULL && !(fp&15);i++) {
        uint64_t frame[2];vm_size_t size=0;
        if(vm_read_overwrite(mach_task_self(),(vm_address_t)fp,sizeof(frame),
            (vm_address_t)frame,&size)!=KERN_SUCCESS || size!=sizeof(frame))break;
        int n=snprintf(line,sizeof(line),"Pong LS18: fatal frame[%u]=%llx\n",i,(unsigned long long)frame[1]);
        if(n>0)(void)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
        if(frame[0]<=fp || frame[0]-fp>0x100000)break;
        fp=frame[0];
    }
    signal(number,SIG_DFL);raise(number);
}
static void backboard_fatal(int number,siginfo_t *info,void *saved)
{
    (void)info;
    ucontext_t *context=saved;
    char line[180];uint64_t fp=0;
    if(context && context->uc_mcontext) {
        int n=snprintf(line,sizeof line,"Pong BB18: fatal signal=%d pc=%llx lr=%llx\n",number,
            (unsigned long long)context->uc_mcontext->__ss.__pc,(unsigned long long)context->uc_mcontext->__ss.__lr);
        if(n>0)(void)write(2,line,(size_t)n<sizeof line?(size_t)n:sizeof line-1);
        fp=context->uc_mcontext->__ss.__fp;
    }
    for(unsigned i=0;i<12 && fp>=0x100000000ULL && !(fp&15);i++) {
        uint64_t frame[2];vm_size_t size=0;
        if(vm_read_overwrite(mach_task_self(),(vm_address_t)fp,sizeof frame,(vm_address_t)frame,&size)!=KERN_SUCCESS || size!=sizeof frame)break;
        int n=snprintf(line,sizeof line,"Pong BB18: fatal frame[%u]=%llx\n",i,(unsigned long long)frame[1]);
        if(n>0)(void)write(2,line,(size_t)n<sizeof line?(size_t)n:sizeof line-1);
        if(frame[0]<=fp || frame[0]-fp>0x100000)break;
        fp=frame[0];
    }
    signal(number,SIG_DFL);raise(number);
}


/* Preserve the actual PosterBoard fatal signal and a small persistent stack.
 * The restored catalog and eligibility policy are the functional changes;
 * this retains a usable cause if the native 22H374 renderer still rejects it.
 */
static int poster_log=-1;
static bool poster_directory_installed;
static CFURLRef poster_data_root,poster_legacy_root;
static IMP poster_base_original,poster_store_original;
static IMP poster_database_original;
static IMP poster_resource_original,poster_resource_tree_original,poster_resource_update_original;

/* The Pong Data volume is ordinary unencrypted APFS. Its real inode class
 * is 0 and it does not advertise MNT_CPROTECT. It cannot persist a class-D
 * encryption policy. Adapt PosterBoard's expected attributes to that volume
 * capability instead of claiming a successful protection class transition.
 * Reachability, POSIX access, directory checks and SQLite validation still
 * run in the original 22H374 methods. Encrypted volumes are untouched. */
static bool poster_unprotected_data(id url)
{
    UInt8 path[1024];
    if(!url || !CFURLGetFileSystemRepresentation((CFURLRef)url,true,path,sizeof(path)))return false;
    const char *p=(const char *)path;
    const char *roots[]={"/private/var/mobile/Library/Application Support/PRBPosterExtensionDataStore",
        "/var/mobile/Library/Application Support/PRBPosterExtensionDataStore",
        "/private/var/mobile/Library/Pong/PosterBoard", "/var/mobile/Library/Pong/PosterBoard"};
    bool scoped=false;
    for(unsigned i=0;i<sizeof(roots)/sizeof(roots[0]);i++) {
        size_t n=strlen(roots[i]);
        if(!strncmp(p,roots[i],n) && (!p[n] || p[n]=='/'))scoped=true;
    }
    struct stat st;struct statfs fs;
    if(!scoped || lstat(p,&st) || S_ISLNK(st.st_mode) || st.st_uid!=geteuid() ||
       statfs(p,&fs) || strcmp(fs.f_fstypename,"apfs") ||
       (strcmp(fs.f_mntonname,"/private/var") && strcmp(fs.f_mntonname,"/var")))return false;
    // Darwin24 bsd/sys/mount.h: MNT_CPROTECT = 0x00000080.
    return !(fs.f_flags&0x00000080u);
}
static CFMutableDictionaryRef poster_volume_resources(id url,id resources)
{
    const CFStringRef *key=dlsym(RTLD_DEFAULT,"NSURLFileProtectionKey");
    if(!resources || CFGetTypeID((CFTypeRef)resources)!=CFDictionaryGetTypeID() ||
       !key || !*key || !CFDictionaryContainsKey((CFDictionaryRef)resources,*key) ||
       !poster_unprotected_data(url))return NULL;
    CFMutableDictionaryRef actual=CFDictionaryCreateMutableCopy(NULL,0,(CFDictionaryRef)resources);
    if(actual)CFDictionaryRemoveValue(actual,*key);
    static unsigned reported;
    if(actual && __atomic_fetch_add(&reported,1,__ATOMIC_RELAXED)<3)
        fprintf(stderr,"Pong Poster18: native resource validation uses unencrypted Data capabilities\n");
    return actual;
}
static BOOL poster_resource_values(id self,SEL selector,id values,id *error)
{
    CFMutableDictionaryRef actual=poster_volume_resources(self,values);
    BOOL result=((BOOL(*)(id,SEL,id,id *))poster_resource_original)(self,selector,actual?(id)actual:values,error);
    if(actual)CFRelease(actual);
    return result;
}
static BOOL poster_resource_tree(id self,SEL selector,id values,id *nonconforming,id *error)
{
    CFMutableDictionaryRef actual=poster_volume_resources(self,values);
    BOOL result=((BOOL(*)(id,SEL,id,id *,id *))poster_resource_tree_original)(self,selector,actual?(id)actual:values,nonconforming,error);
    if(actual)CFRelease(actual);
    return result;
}
static BOOL poster_resource_update(id self,SEL selector,id values,id *error)
{
    CFMutableDictionaryRef actual=poster_volume_resources(self,values);
    BOOL result=((BOOL(*)(id,SEL,id,id *))poster_resource_update_original)(self,selector,actual?(id)actual:values,error);
    if(actual)CFRelease(actual);
    return result;
}
static void poster_volume_policy(void)
{
    if(poster_resource_original)return;
    Class cls=objc_lookUpClass("NSURL");
    Method single=cls?class_getInstanceMethod(cls,sel_registerName("pbf_URLIsReachableAndConformToResourceValues:error:")):NULL;
    Method tree=cls?class_getInstanceMethod(cls,sel_registerName("pbf_recursivelyValidateContentsAreReachableAndConformToResourceValues:URLsNotConformingToAttributes:error:")):NULL;
    Method update=cls?class_getInstanceMethod(cls,sel_registerName("pbf_recursivelyUpdateResourceValues:error:")):NULL;
    if(!method_abi(single,"Bc",2,"@^") || !method_abi(tree,"Bc",3,"@^^") || !method_abi(update,"Bc",2,"@^"))return;
    poster_resource_original=method_setImplementation(single,(IMP)poster_resource_values);
    poster_resource_tree_original=method_setImplementation(tree,(IMP)poster_resource_tree);
    poster_resource_update_original=method_setImplementation(update,(IMP)poster_resource_update);
}

/* The restore can leave a current-version directory without its database.
 * 22H374's option 1 validates/opens an existing database and does not create
 * it. Its native option 8 path opens SQLite read/write/create and runs the
 * matching schema setup. Use that path only for an absent Poster database
 * below a real mobile-owned Data directory; preserve every existing store.
 */
static id poster_database(id self,SEL selector,id url,unsigned long long options,id *error)
{
    UInt8 path[1024];
    if(options==1 && url && CFURLGetFileSystemRepresentation((CFURLRef)url,true,path,sizeof(path))) {
        const char *p=(const char *)path;
        const char *native="/private/var/mobile/Library/Application Support/PRBPosterExtensionDataStore/";
        const char *port="/private/var/mobile/Library/Pong/PosterBoard/";
        const char *native_alias="/var/mobile/Library/Application Support/PRBPosterExtensionDataStore/";
        const char *port_alias="/var/mobile/Library/Pong/PosterBoard/";
        bool own_path=!strncmp(p,native,strlen(native)) || !strncmp(p,port,strlen(port)) ||
            !strncmp(p,native_alias,strlen(native_alias)) || !strncmp(p,port_alias,strlen(port_alias));
        struct stat st;
        if(own_path && lstat(p,&st)!=0 && errno==ENOENT) {
            char parent[sizeof(path)];strlcpy(parent,p,sizeof(parent));
            char *slash=strrchr(parent,'/');if(slash)*slash=0;
            if(slash && lstat(parent,&st)==0 && S_ISDIR(st.st_mode) && st.st_uid==geteuid()) {
                options|=8;
                fprintf(stderr,"Pong Poster18: initialize absent database with native schema %s\n",p);
            }
        }
    }
    id result=((id(*)(id,SEL,id,unsigned long long,id *))poster_database_original)(self,selector,url,options,error);
    if(!result) {
        id text=error && *error?((id(*)(id,SEL))objc_msgSend)(*error,sel_registerName("description")):nil;
        char detail[768]={0};if(text)CFStringGetCString((CFStringRef)text,detail,sizeof(detail),kCFStringEncodingUTF8);
        fprintf(stderr,"Pong Poster18: native database initialization failed: %s errno=%d\n",detail,errno);
    }
    return result;
}

/* The 22H374 scene constructor requires the real PBF data store. Its
 * _baseDataStoreURL: can resolve an unavailable shared/container home on
 * this single-user port. Use a UID-owned directory on the mounted Data,
 * keeping PBF's database, migration, role configuration and renderer intact. */
static void poster_directory_protection(id url)
{
    if(poster_unprotected_data(url))return;
    UInt8 path[1024];
    if(!url || !CFURLGetFileSystemRepresentation((CFURLRef)url,true,path,sizeof(path)))return;
    const char *p=(const char *)path;
    const char *roots[]={"/private/var/mobile/Library/Application Support/PRBPosterExtensionDataStore",
        "/var/mobile/Library/Application Support/PRBPosterExtensionDataStore",
        "/private/var/mobile/Library/Pong/PosterBoard/", "/var/mobile/Library/Pong/PosterBoard/"};
    bool owned=false;for(unsigned i=0;i<sizeof(roots)/sizeof(roots[0]);i++) {
        size_t n=strlen(roots[i]);
        if(!strncmp(p,roots[i],n) && (roots[i][n-1]=='/' || !p[n] || p[n]=='/'))owned=true;
    }
    if(!owned)return;
    const CFStringRef *key=dlsym(RTLD_DEFAULT,"NSURLFileProtectionKey");
    const CFStringRef *value=dlsym(RTLD_DEFAULT,"NSURLFileProtectionNone");
    if(!key || !value || !*key || !*value)return;
    // PFPosterPathURLResourceValues requires IsReadable=YES and protection
    // None. A writable directory with inherited class C still fails this
    // native check. Set the actual filesystem attribute; do not bypass it.
    // 22H374's minimum version is 59 but its current version is 61.
    // Resolve both from the loaded framework. Never create or rename a
    // version directory here: the real migrator owns that transition.
    Class store=objc_lookUpClass("PBFPosterExtensionDataStore");
    SEL minimum=sel_registerName("minimumDataStoreVersion"),current=sel_registerName("dataStoreVersion");
    if(!method_abi(store?class_getClassMethod(store,minimum):NULL,"Qq",0,"") ||
       !method_abi(class_getClassMethod(store,current),"Qq",0,""))return;
    unsigned long long first=((unsigned long long(*)(id,SEL))objc_msgSend)((id)store,minimum);
    unsigned long long last=((unsigned long long(*)(id,SEL))objc_msgSend)((id)store,current);
    if(first>last || last-first>16)return;
    const char *suffixes[]={"","/Extensions","/GalleryCache"};
    unsigned count=1+(unsigned)(last-first+1)*3;
    for(unsigned i=0;i<count;i++) {
        char target[1200];
        if(!i)strlcpy(target,p,sizeof(target));
        else snprintf(target,sizeof(target),"%s/%llu%s",p,first+(i-1)/3,suffixes[(i-1)%3]);
        struct stat st;if(lstat(target,&st) || !S_ISDIR(st.st_mode) || st.st_uid!=geteuid())continue;
        CFURLRef directory=CFURLCreateFromFileSystemRepresentation(NULL,(const UInt8 *)target,strlen(target),true);
        if(!directory)continue;
        SEL get=sel_registerName("getResourceValue:forKey:error:"),set=sel_registerName("setResourceValue:forKey:error:");
        Class cls=object_getClass((id)directory);
        Method gm=class_getInstanceMethod(cls,get),sm=class_getInstanceMethod(cls,set);
        if(method_abi(gm,"Bc",3,"^@^") && method_abi(sm,"Bc",3,"@@^")) {
            id current=nil,error=nil;
            BOOL readable=((BOOL(*)(id,SEL,id *,id,id *))objc_msgSend)((id)directory,get,&current,(id)*key,&error);
            if(!readable || !current || !CFEqual((CFTypeRef)current,*value)) {
                error=nil;
                BOOL applied=((BOOL(*)(id,SEL,id,id,id *))objc_msgSend)((id)directory,set,(id)*value,(id)*key,&error);
                char detail[512]={0};id text=error?((id(*)(id,SEL))objc_msgSend)(error,sel_registerName("description")):nil;
                if(text)CFStringGetCString((CFStringRef)text,detail,sizeof(detail),kCFStringEncodingUTF8);
                static unsigned reported;
                if(!applied || __atomic_fetch_add(&reported,1,__ATOMIC_RELAXED)<8)
                    fprintf(stderr,"Pong Poster18: native directory protection %s %s %s\n",applied?"set":"failed",target,detail);
            }
        }
        CFRelease(directory);
    }
}
static void poster_base_directory_protection(id base)
{
    // PBF's base is Application Support (or the distinct shared/legacy
    // fallback), not PRBPosterExtensionDataStore itself. Derive the same
    // store root as its native NSURL category before touching attributes.
    UInt8 path[1024];
    if(!base || !CFURLGetFileSystemRepresentation((CFURLRef)base,true,path,sizeof(path)))return;
    const char *p=(const char *)path;
    const char *allowed[]={"/private/var/mobile/Library/Application Support",
        "/var/mobile/Library/Application Support",
        "/private/var/mobile/Library/Pong/PosterBoard/Shared",
        "/private/var/mobile/Library/Pong/PosterBoard/Legacy",
        "/var/mobile/Library/Pong/PosterBoard/Shared",
        "/var/mobile/Library/Pong/PosterBoard/Legacy"};
    bool owned=false;
    for(unsigned i=0;i<sizeof(allowed)/sizeof(allowed[0]);i++)
        if(!strcmp(p,allowed[i]))owned=true;
    if(!owned)return;
    struct stat st;
    if(lstat(p,&st) || !S_ISDIR(st.st_mode) || st.st_uid!=geteuid() || access(p,W_OK|X_OK))return;
    CFURLRef root=CFURLCreateCopyAppendingPathComponent(NULL,(CFURLRef)base,
        CFSTR("PRBPosterExtensionDataStore"),true);
    if(root){poster_directory_protection((id)root);CFRelease(root);}
}
static id poster_base_url(id self,SEL selector,BOOL use_shared)
{
    id original=((id(*)(id,SEL,BOOL))poster_base_original)(self,selector,use_shared);
    UInt8 path[1024];
    if(original && CFURLGetFileSystemRepresentation((CFURLRef)original,true,path,sizeof(path)) &&
       access((const char *)path,W_OK|X_OK)==0) {
        poster_base_directory_protection(original);return original;
    }
    id selected=(id)(use_shared?poster_data_root:poster_legacy_root);
    poster_base_directory_protection(selected);return selected;
}
static id poster_store(id self,SEL selector)
{
    (void)selector;id error=nil;
    /* The stock dataStore accessor is a tail call to this exact method with
     * a null error pointer. Preserve the operation and retain its real error. */
    id store=((id(*)(id,SEL,id *))objc_msgSend)(self,sel_registerName("_dataStoreWithError:"),&error);
    if(!store) {
        id description=error?((id(*)(id,SEL))objc_msgSend)(error,sel_registerName("description")):nil;
        char detail[768]={0};
        if(description)CFStringGetCString((CFStringRef)description,detail,sizeof(detail),kCFStringEncodingUTF8);
        fprintf(stderr,"Pong Poster18: native DataStore unavailable: %s\n",detail);
        if(poster_log>=0){(void)write(poster_log,detail,strlen(detail));(void)write(poster_log,"\n",1);}
    }
    return store;
}
static void poster_image(const struct mach_header *header,intptr_t slide)
{
    (void)header;(void)slide;
    wallpaper_resource_policy();
    // MCM may arrive after PosterFoundation. Keep applying its real
    // single-user policy on later image loads, even once PBF is adapted.
    container_persona_policy();
    poster_volume_policy();
    if(poster_directory_installed)return;
    if(geteuid()!=501)return;
    Class cls=objc_lookUpClass("PBFPosterExtensionDataStoreXPCServiceGlue");
    Method base=cls?class_getInstanceMethod(cls,sel_registerName("_baseDataStoreURL:")):NULL;
    Method store=cls?class_getInstanceMethod(cls,sel_registerName("dataStore")):NULL;
    Method error=cls?class_getInstanceMethod(cls,sel_registerName("_dataStoreWithError:")):NULL;
    if(!method_abi(base,"@",1,"B") || !method_abi(store,"@",0,"") ||
       !method_abi(error,"@",1,"^"))return;
    const char *parent="/private/var/mobile/Library/Pong";
    const char *path="/private/var/mobile/Library/Pong/PosterBoard";
    const char *shared="/private/var/mobile/Library/Pong/PosterBoard/Shared";
    const char *legacy="/private/var/mobile/Library/Pong/PosterBoard/Legacy";
    if(!own_directory(parent) || !own_directory(path) ||
       !own_directory(shared) || !own_directory(legacy))return;
    // Native migration compares the old per-user and new shared locations;
    // do not alias those two distinct roots to the same directory.
    poster_data_root=CFURLCreateFromFileSystemRepresentation(NULL,(const UInt8 *)shared,strlen(shared),true);
    poster_legacy_root=CFURLCreateFromFileSystemRepresentation(NULL,(const UInt8 *)legacy,strlen(legacy),true);
    if(!poster_data_root || !poster_legacy_root)return;
    poster_base_original=method_setImplementation(base,(IMP)poster_base_url);
    poster_store_original=method_setImplementation(store,(IMP)poster_store);
    Class database=objc_lookUpClass("PBFPosterExtensionDataStoreSQLiteDatabase");
    Method initializer=database?class_getInstanceMethod(database,sel_registerName("initWithURL:options:error:")):NULL;
    if(method_abi(initializer,"@",3,"@Q^")) {
        poster_database_original=method_setImplementation(initializer,(IMP)poster_database);
    }
    poster_directory_installed=true;
    container_persona_policy();
    fprintf(stderr,"Pong Poster18: native DataStore fallback uses writable %s\n",path);
}
static void poster_fatal(int number,siginfo_t *info,void *saved)
{
    (void)info;ucontext_t *context=saved;
    char line[176];uint64_t fp=0;
    if(context && context->uc_mcontext) {
        int n=snprintf(line,sizeof(line),"Pong Poster18: fatal=%d pc=%llx lr=%llx\n",number,
            (unsigned long long)context->uc_mcontext->__ss.__pc,
            (unsigned long long)context->uc_mcontext->__ss.__lr);
        if(n>0 && n<(int)sizeof(line)){(void)write(2,line,(size_t)n);if(poster_log>=0)(void)write(poster_log,line,(size_t)n);}
        fp=context->uc_mcontext->__ss.__fp;
    }
    for(unsigned i=0;i<12 && fp>=0x100000000ULL && !(fp&15);i++) {
        uint64_t frame[2];vm_size_t bytes=0;
        if(vm_read_overwrite(mach_task_self(),(vm_address_t)fp,sizeof(frame),(vm_address_t)frame,&bytes)!=KERN_SUCCESS || bytes!=sizeof(frame))break;
        int n=snprintf(line,sizeof(line),"Pong Poster18: frame[%u]=%llx\n",i,(unsigned long long)frame[1]);
        if(n>0 && n<(int)sizeof(line)){(void)write(2,line,(size_t)n);if(poster_log>=0)(void)write(poster_log,line,(size_t)n);}
        if(frame[0]<=fp || frame[0]-fp>0x100000)break;
        fp=frame[0];
    }
    if(poster_log>=0)(void)fsync(poster_log);
    signal(number,SIG_DFL);raise(number);
}
static void poster_fatal_capture(void)
{
    poster_log=open("/private/var/mobile/Library/Logs/Pong-Poster18.log",O_WRONLY|O_CREAT|O_APPEND,0644);
    Dl_info image={0};
    if(dladdr((const void *)objc_msgSend,&image)) {
        char line[192];int n=snprintf(line,sizeof(line),"Pong Poster18: pid=%d libobjc base=%p\n",getpid(),image.dli_fbase);
        if(n>0 && n<(int)sizeof(line) && poster_log>=0)(void)write(poster_log,line,(size_t)n);
    }
    struct sigaction action={0};action.sa_sigaction=poster_fatal;
    action.sa_flags=SA_SIGINFO|SA_RESETHAND;sigemptyset(&action.sa_mask);
    (void)sigaction(SIGTRAP,&action,NULL);(void)sigaction(SIGABRT,&action,NULL);
}

static id process_root(id self,SEL selector)
{ (void)self;(void)selector;return (id)container_root; }
static id process_library(id self,SEL selector)
{ (void)self;(void)selector;return (id)container_library; }

/* The 22H374 backboardd trace enters this provider with a nil system
 * container Library string. Supply the port's actual, process-owned Data
 * directories. Its original cachesPath appends Caches to libraryPath.
 * This is confined to backboardd, with no global NSURL/exception overrides
 * and no changes to launchd's native system-proxy personas.
 */
static void backboard_container(void)
{
    Class cls=objc_lookUpClass("BSSystemContainerForCurrentProcessPathProvider");
    SEL root_sel=sel_registerName("defaultPath"),lib_sel=sel_registerName("libraryPath");
    Method root=cls?class_getInstanceMethod(cls,root_sel):NULL;
    Method lib=cls?class_getInstanceMethod(cls,lib_sel):NULL;
    if(!root || !lib || method_getNumberOfArguments(root)!=2 || method_getNumberOfArguments(lib)!=2)return;
    char *root_type=method_copyReturnType(root),*lib_type=method_copyReturnType(lib);
    bool valid=root_type && lib_type && root_type[0]=='@' && lib_type[0]=='@';
    free(root_type);free(lib_type);
    if(!valid)return;
    const char *base="/private/var/mobile/Library/Pong";
    const char *path="/private/var/mobile/Library/Pong/backboardd";
    const char *library="/private/var/mobile/Library/Pong/backboardd/Library";
    if(!own_directory(base) || !own_directory(path) || !own_directory(library) ||
       !own_directory("/private/var/mobile/Library/Pong/backboardd/Library/Caches")) {
        fprintf(stderr,"Pong UI18: backboardd Data directories unavailable errno=%d\n",errno);return;
    }
    container_root=CFURLCreateFromFileSystemRepresentation(NULL,(const UInt8 *)path,strlen(path),true);
    container_library=CFURLCreateFromFileSystemRepresentation(NULL,(const UInt8 *)library,strlen(library),true);
    if(!container_root || !container_library)return;
    class_replaceMethod(cls,root_sel,(IMP)process_root,method_getTypeEncoding(root));
    class_replaceMethod(cls,lib_sel,(IMP)process_library,method_getTypeEncoding(lib));
    fprintf(stderr,"Pong UI18: backboardd container uses writable %s\n",path);
}

static uint64_t input_trace_us(uint64_t ticks)
{
    static mach_timebase_info_data_t clock;
    if(!clock.denom && mach_timebase_info(&clock)!=KERN_SUCCESS)return 0;
    return clock.denom?(uint64_t)((__uint128_t)ticks*clock.numer/clock.denom/1000):0;
}
static id input_gesture_observer;
static char input_observed_key, input_moved_key;
static void input_gesture(id self,SEL selector,id gesture)
{
    (void)self;(void)selector;
    static unsigned count;
    const long state=((long(*)(id,SEL))objc_msgSend)(gesture,sel_registerName("state"));
    id view=((id(*)(id,SEL))objc_msgSend)(gesture,sel_registerName("view"));
    bool report=state==1 || state==3 || state==4;
    if(state==1)objc_setAssociatedObject(gesture,&input_moved_key,nil,OBJC_ASSOCIATION_ASSIGN);
    if(state==2 && !objc_getAssociatedObject(gesture,&input_moved_key)) {
        report=true;objc_setAssociatedObject(gesture,&input_moved_key,(id)kCFBooleanTrue,OBJC_ASSOCIATION_ASSIGN);
    }
    if(report && count++<160) {
        fprintf(stderr,"tl: g class %s state %ld at %llu\n",view?object_getClassName(view):"nil",state,input_trace_us(mach_absolute_time()));
    }
}
static unsigned input_observe_view(id view,unsigned depth,unsigned *visited)
{
    if(!view || depth>32 || ++*visited>2048)return 0;
    unsigned found=0;
    if(((bool(*)(id,SEL,Class))objc_msgSend)(view,sel_registerName("isKindOfClass:"),objc_getClass("UIScrollView")) &&
        (!springboard_process || strstr(object_getClassName(view),"IconScrollView"))) {
        id pan=((id(*)(id,SEL))objc_msgSend)(view,sel_registerName("panGestureRecognizer"));
        if(pan && !objc_getAssociatedObject(pan,&input_observed_key)) {
            ((void(*)(id,SEL,id,SEL))objc_msgSend)(pan,sel_registerName("addTarget:action:"),input_gesture_observer,sel_registerName("observeGesture:"));
            objc_setAssociatedObject(pan,&input_observed_key,(id)kCFBooleanTrue,OBJC_ASSOCIATION_ASSIGN);
            fprintf(stderr,"Pong input trace: native pan observer view=%s\n",object_getClassName(view));found++;
            const CGPoint offset=((CGPoint(*)(id,SEL))objc_msgSend)(view,sel_registerName("contentOffset"));
            const CGSize content=((CGSize(*)(id,SEL))objc_msgSend)(view,sel_registerName("contentSize"));
            const CGRect bounds=((CGRect(*)(id,SEL))objc_msgSend)(view,sel_registerName("bounds"));
            fprintf(stderr,"Pong input trace: scroll extent offset=%.2f,%.2f content=%.2f,%.2f viewport=%.2f,%.2f\n",
                offset.x,offset.y,content.width,content.height,bounds.size.width,bounds.size.height);
        }
    }
    id children=((id(*)(id,SEL))objc_msgSend)(view,sel_registerName("subviews"));
    unsigned long n=((unsigned long(*)(id,SEL))objc_msgSend)(children,sel_registerName("count"));
    for(unsigned long i=0;i<n && *visited<2048;i++)
        found+=input_observe_view(((id(*)(id,SEL,unsigned long))objc_msgSend)(children,sel_registerName("objectAtIndex:"),i),depth+1,visited);
    return found;
}
static void input_event_trace(void *unused)
{
    (void)unused;
    if(!input_gesture_observer) {
        Class cls=objc_allocateClassPair(objc_getClass("NSObject"),"PongInputGestureObserver",0);
        if(!cls){fprintf(stderr,"Pong input trace: SKIP observer class unavailable\n");return;}
        class_addMethod(cls,sel_registerName("observeGesture:"),(IMP)input_gesture,"v@:@");
        objc_registerClassPair(cls);input_gesture_observer=((id(*)(id,SEL))objc_msgSend)((id)cls,sel_registerName("new"));
    }
    id app=((id(*)(id,SEL))objc_msgSend)((id)objc_getClass("UIApplication"),sel_registerName("sharedApplication"));
    id windows=((id(*)(id,SEL))objc_msgSend)(app,sel_registerName("windows"));
    unsigned long n=((unsigned long(*)(id,SEL))objc_msgSend)(windows,sel_registerName("count"));
    unsigned found=0,visited=0;
    for(unsigned long i=0;i<n && visited<2048;i++)
        found+=input_observe_view(((id(*)(id,SEL,unsigned long))objc_msgSend)(windows,sel_registerName("objectAtIndex:"),i),0,&visited);
    fprintf(stderr,"Pong input trace: added=%u visited=%u native pan observers\n",found,visited);
}

static id brightness_test_slider;
static float brightness_test_original;
static unsigned brightness_test_step, brightness_test_tries;
static bool brightness_test_restore_only;
static bool brightness_visible(id view,CGRect *rectangle)
{
    id window=((id(*)(id,SEL))objc_msgSend)(view,sel_registerName("window"));
    if(!window)return false;
    for(id current=view;current;current=((id(*)(id,SEL))objc_msgSend)(current,sel_registerName("superview"))) {
        if(((bool(*)(id,SEL))objc_msgSend)(current,sel_registerName("isHidden")) ||
           ((CGFloat(*)(id,SEL))objc_msgSend)(current,sel_registerName("alpha"))<.01)return false;
    }
    CGRect bounds=((CGRect(*)(id,SEL))objc_msgSend)(view,sel_registerName("bounds"));
    CGRect frame=((CGRect(*)(id,SEL,CGRect,id))objc_msgSend)(view,sel_registerName("convertRect:toView:"),bounds,window);
    CGRect screen=((CGRect(*)(id,SEL))objc_msgSend)(window,sel_registerName("bounds"));
    if(rectangle)*rectangle=frame;
    return !CGRectIsEmpty(CGRectIntersection(frame,screen));
}
static void brightness_find_slider(id view,unsigned depth,unsigned *visited,id *result,unsigned *matches)
{
    if(!view || depth>32 || ++*visited>2048)return;
    if(((bool(*)(id,SEL,Class))objc_msgSend)(view,sel_registerName("isKindOfClass:"),objc_getClass("UISlider"))) {
        float lo=((float(*)(id,SEL))objc_msgSend)(view,sel_registerName("minimumValue"));
        float hi=((float(*)(id,SEL))objc_msgSend)(view,sel_registerName("maximumValue"));
        bool enabled=((bool(*)(id,SEL))objc_msgSend)(view,sel_registerName("isEnabled"));
        if(enabled && fabsf(lo)<.0001f && fabsf(hi-1.f)<.0001f) {
            CGRect frame=CGRectZero;bool visible=brightness_visible(view,&frame);
            if(brightness_test_tries<2)
                fprintf(stderr,"Pong brightness UI: candidate class=%s visible=%d frame=%.1f,%.1f,%.1f,%.1f\n",
                    object_getClassName(view),visible,frame.origin.x,frame.origin.y,frame.size.width,frame.size.height);
            if(visible){*result=view;(*matches)++;}
        }
    }
    id children=((id(*)(id,SEL))objc_msgSend)(view,sel_registerName("subviews"));
    unsigned long n=((unsigned long(*)(id,SEL))objc_msgSend)(children,sel_registerName("count"));
    for(unsigned long i=0;i<n && *visited<2048;i++)
        brightness_find_slider(((id(*)(id,SEL,unsigned long))objc_msgSend)(children,sel_registerName("objectAtIndex:"),i),depth+1,visited,result,matches);
}
static void brightness_ui_test(void *unused)
{
    (void)unused;
    if(!brightness_test_slider) {
        id app=((id(*)(id,SEL))objc_msgSend)((id)objc_getClass("UIApplication"),sel_registerName("sharedApplication"));
        id windows=((id(*)(id,SEL))objc_msgSend)(app,sel_registerName("windows"));
        unsigned long n=((unsigned long(*)(id,SEL))objc_msgSend)(windows,sel_registerName("count"));
        id slider=nil;unsigned visited=0,matches=0;
        for(unsigned long i=0;i<n && visited<2048;i++)
            brightness_find_slider(((id(*)(id,SEL,unsigned long))objc_msgSend)(windows,sel_registerName("objectAtIndex:"),i),0,&visited,&slider,&matches);
        if(matches!=1) {
            if(++brightness_test_tries<10)dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC),dispatch_get_main_queue(),NULL,brightness_ui_test);
            else fprintf(stderr,"Pong brightness UI: SKIP expected one enabled 0..1 slider, found=%u\n",matches);
            return;
        }
        brightness_test_original=((float(*)(id,SEL))objc_msgSend)(slider,sel_registerName("value"));
        const float initial=brightness_test_original;
        char restoration[32];
        if(boot_value("pong_brightness_restore=",restoration,sizeof restoration)) {
            char *end=NULL;double value=strtod(restoration,&end);
            if(end==restoration || *end || !isfinite(value) || value<.05 || value>.95) {
                fprintf(stderr,"Pong brightness UI: SKIP invalid restoration value\n");return;
            }
            brightness_test_original=(float)value;
        }
        if(!isfinite(brightness_test_original) || brightness_test_original<.05f || brightness_test_original>.95f) {
            fprintf(stderr,"Pong brightness UI: SKIP original value outside safe test interval\n");return;
        }
        brightness_test_slider=((id(*)(id,SEL))objc_msgSend)(slider,sel_registerName("retain"));
        fprintf(stderr,"Pong brightness UI: native initial=%.6f restoration=%.6f\n",initial,brightness_test_original);
        if(brightness_test_restore_only)brightness_test_step=2;
    }
    float value=brightness_test_step==0?(fabsf(brightness_test_original-.25f)<.01f?.35f:.25f):
        brightness_test_step==1?(fabsf(brightness_test_original-.65f)<.01f?.55f:.65f):brightness_test_original;
    ((void(*)(id,SEL,float,bool))objc_msgSend)(brightness_test_slider,sel_registerName("setValue:animated:"),value,false);
    ((void(*)(id,SEL,unsigned long))objc_msgSend)(brightness_test_slider,sel_registerName("sendActionsForControlEvents:"),(1ul<<12)|(1ul<<6));
    fprintf(stderr,"Pong brightness UI: RUNNING native slider step=%u value=%.4f original=%.4f; verify hardware independently\n",
        ++brightness_test_step,value,brightness_test_original);
    if(brightness_test_step<3)
        dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,15*NSEC_PER_SEC),dispatch_get_main_queue(),NULL,brightness_ui_test);
    else {
        ((void(*)(id,SEL))objc_msgSend)(brightness_test_slider,sel_registerName("release"));
        brightness_test_slider=nil;
    }
}

/* Explicit regression probe: create/release real panel-sized IOSurfaces in
 * backboardd after normal startup. API allocation alone is not a panel-pool
 * PASS; correlate with the kernel's slot/mapping evidence. No pixels drawn. */
static void late_surface_probe(void *unused)
{
    (void)unused;
    static void *image;
    static unsigned index;
    if(!image)image=dlopen("/System/Library/Frameworks/IOSurface.framework/IOSurface",RTLD_NOW|RTLD_LOCAL);
    CFTypeRef (*create)(CFDictionaryRef)=image?(CFTypeRef(*)(CFDictionaryRef))dlsym(image,"IOSurfaceCreate"):NULL;
    void *(*address)(CFTypeRef)=image?(void *(*)(CFTypeRef))dlsym(image,"IOSurfaceGetBaseAddress"):NULL;
    size_t (*size)(CFTypeRef)=image?(size_t(*)(CFTypeRef))dlsym(image,"IOSurfaceGetAllocSize"):NULL;
    if(!create || !address || !size){fprintf(stderr,"Pong surface probe: SKIP native API unavailable\n");return;}
    const void *keys[]={CFSTR("IOSurfaceWidth"),CFSTR("IOSurfaceHeight"),CFSTR("IOSurfaceBytesPerRow"),
        CFSTR("IOSurfaceAllocSize"),CFSTR("IOSurfaceBytesPerElement"),CFSTR("IOSurfacePixelFormat")};
    int32_t values[]={2160,1916,8704,0xfe8000,4,0x42475241};
    CFNumberRef numbers[6]={};bool ready=true;
    for(unsigned i=0;i<6;i++){numbers[i]=CFNumberCreate(NULL,kCFNumberSInt32Type,&values[i]);ready&=numbers[i]!=NULL;}
    CFDictionaryRef properties=ready?CFDictionaryCreate(NULL,keys,(const void **)numbers,6,
        &kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks):NULL;
    CFTypeRef surface=properties?create(properties):NULL;
    const bool mapped=surface && address(surface)!=NULL;
    fprintf(stderr,"Pong surface probe: %s native allocation %u at %llu created=%d mapped=%d bytes=%lu; verify panel-slot reuse independently\n",
        mapped?"PASS":"FAIL",++index,input_trace_us(mach_absolute_time()),surface!=NULL,mapped,
        (unsigned long)(surface?size(surface):0));
    if(surface)CFRelease(surface);
    if(properties)CFRelease(properties);
    for(unsigned i=0;i<6;i++)if(numbers[i])CFRelease(numbers[i]);
}

/* zizhan: pong_dl_full=0 preserves requested rates. g296 observed the
 * current runtime target UIKit.InProcessAnimationManager; retain the older
 * Objective-C class spelling for restores that still use it.
 * SpringBoard's own animations at the panel's rate. UIKit's UIViewInProcessAnimationManager (the animations an app runs in its own
 * process, a home-screen page settling among them) asks its CADisplayLink for 20..30 a second: the frames after a swipe came 33.3 ms
 * apart (g136-g143 gap p90 33.2 ms). On a ProMotion iPhone the low rate saves power; this panel refreshes at 120 whatever is drawn. The
 * rates asked of that display link are dropped, other display links keep theirs (the clock's, the motion effects'): run at 120 they woke
 * SpringBoard for nothing. Ported from hakios 7caa457 (right swipe 78 -> 115 fps there). pong_dl_full=0 keeps every rate; touch_lat=1
 * prints each display link made and each rate asked. */
typedef struct {float minimum,maximum,preferred;} frame_rate_range;
static id (*original_link_with)(id,SEL,id,SEL);
static void (*original_link_fps)(id,SEL,long);
static void (*original_link_range)(id,SEL,frame_rate_range);
static bool link_say,link_full;
static unsigned link_said;
static char link_full_key;
static bool link_at_full_rate(id link) {return link_full && objc_getAssociatedObject(link,&link_full_key)!=nil;}
static const char *link_caller(const void *address,char *out,size_t size)
{
    Dl_info info;
    if(!dladdr(address,&info) || !info.dli_fname) {snprintf(out,size,"%p",address);return out;}
    const char *image=strrchr(info.dli_fname,'/')?strrchr(info.dli_fname,'/')+1:info.dli_fname;
    snprintf(out,size,"%s+%#lx",image,(unsigned long)((uintptr_t)address-(uintptr_t)info.dli_fbase));
    return out;
}
static id link_with(id self,SEL selector,id target,SEL action)
{
    id link=original_link_with(self,selector,target,action);
    char who[160];
    if(link && target) {
        const char *name=object_getClassName(target);
        if(!strcmp(name,"UIViewInProcessAnimationManager") || !strcmp(name,"UIKit.InProcessAnimationManager"))
            objc_setAssociatedObject(link,&link_full_key,(id)kCFBooleanTrue,OBJC_ASSOCIATION_ASSIGN);
    }
    if(link_say && link_said++<300)
        fprintf(stderr,"Pong UI18: display link %p made for -[%s %s] by %s\n",(void *)link,target?object_getClassName(target):"nil",
            action?sel_getName(action):"",link_caller(__builtin_return_address(0),who,sizeof(who)));
    return link;
}
static void link_fps(id self,SEL selector,long fps)
{
    char who[160];
    if(link_say && link_said++<300)
        fprintf(stderr,"Pong UI18: display link %p asked for %ld a second%s by %s\n",(void *)self,fps,link_at_full_rate(self)?" (dropped)":"",
            link_caller(__builtin_return_address(0),who,sizeof(who)));
    original_link_fps(self,selector,link_at_full_rate(self)?0:fps);
}
static void link_range(id self,SEL selector,frame_rate_range range)
{
    char who[160];
    if(link_say && link_said++<300)
        fprintf(stderr,"Pong UI18: display link %p asked for %.0f..%.0f, %.0f a second%s by %s\n",(void *)self,range.minimum,range.maximum,
            range.preferred,link_at_full_rate(self)?" (dropped)":"",link_caller(__builtin_return_address(0),who,sizeof(who)));
    if(link_at_full_rate(self))range=(frame_rate_range){0,0,0};
    original_link_range(self,selector,range);
}
static void display_link_rates(void)
{
    link_say=boot_option("touch_lat=1");link_full=!boot_option("pong_dl_full=0");
    Class link=objc_getClass("CADisplayLink");
    if(!link || (!link_say && !link_full))return;
    Method made=class_getClassMethod(link,sel_registerName("displayLinkWithTarget:selector:"));
    Method fps=class_getInstanceMethod(link,sel_registerName("setPreferredFramesPerSecond:"));
    Method range=class_getInstanceMethod(link,sel_registerName("setPreferredFrameRateRange:"));
    if(made)original_link_with=(id(*)(id,SEL,id,SEL))method_setImplementation(made,(IMP)link_with);
    if(fps)original_link_fps=(void(*)(id,SEL,long))method_setImplementation(fps,(IMP)link_fps);
    if(range)original_link_range=(void(*)(id,SEL,frame_rate_range))method_setImplementation(range,(IMP)link_range);
    fprintf(stderr,"Pong UI18: display links: made %d fps %d range %d, in-process animations at the panel's rate %d\n",
        made!=NULL,fps!=NULL,range!=NULL,link_full);
}

/* Lock screen date/time overlap (2026-11-21 user check; sbar-2: the date line touches the clock).
 * The zizhan panel is 1080x958 points, wider than tall, while SpringBoard runs portrait. Log the
 * SBFLockScreenDateView geometry (first layouts only) and the landscape-time-font requests; boot-arg
 * zizhan_lock_time=1 forces the portrait time font, =2 the landscape one. */
static void (*lockdate_layout_original)(id,SEL);
static void (*lockdate_landscape_original)(id,SEL,BOOL);
static unsigned lockdate_layout_logs,lockdate_landscape_logs;
static int lockdate_mode;
static void lockdate_rect(const char *what,id view,unsigned level)
{
    Method frame=class_getInstanceMethod(object_getClass(view),sel_registerName("frame"));
    if(!method_abi(frame,"{",0,"")) {
        return;
    }
    CGRect f=((CGRect(*)(id,SEL))objc_msgSend)(view,sel_registerName("frame"));
    fprintf(stderr,"Zizhan lockdate: %s L%u %s frame=%.1f/%.1f/%.1f/%.1f\n",what,level,object_getClassName(view),
            f.origin.x,f.origin.y,f.size.width,f.size.height);
}
static void lockdate_tree(id view,unsigned level,unsigned *budget)
{
    if(!view || !*budget || level>4) {
        return;
    }
    --*budget;
    lockdate_rect("view",view,level);
    id children=((id(*)(id,SEL))objc_msgSend)(view,sel_registerName("subviews"));
    if(!children) {
        return;
    }
    CFIndex count=CFArrayGetCount((CFArrayRef)children);
    for(CFIndex i=0;i<count && *budget;++i) {
        lockdate_tree((id)CFArrayGetValueAtIndex((CFArrayRef)children,i),level+1,budget);
    }
}
static id lockdate_last;
static void lockdate_report(id self,const char *when)
{
    int landscape=-1;
    Method getter=class_getInstanceMethod(object_getClass(self),sel_registerName("useLandscapeTimeFontSize"));
    if(method_abi(getter,"Bc",0,"")) {
        landscape=((BOOL(*)(id,SEL))objc_msgSend)(self,sel_registerName("useLandscapeTimeFontSize"));
    }
    fprintf(stderr,"Zizhan lockdate: %s landscapeFont=%d mode=%d layouts=%u\n",when,landscape,lockdate_mode,lockdate_layout_logs);
    unsigned budget=24;
    lockdate_tree(self,0,&budget);
}
/* The console ring wraps during boot: report again at about 60/120/180 s after SpringBoard started. */
static void lockdate_late(void *context)
{
    (void)context;
    fprintf(stderr,"Zizhan lockdate: late hooks layout=%d landscape=%d view=%d\n",lockdate_layout_original!=NULL,
            lockdate_landscape_original!=NULL,lockdate_last!=nil);
    if(lockdate_last) {
        lockdate_report(lockdate_last,"late");
    }
}
static void lockdate_layout(id self,SEL selector)
{
    if(lockdate_layout_original) {
        lockdate_layout_original(self,selector);
    }
    if(lockdate_last!=self) {
        ((id(*)(id,SEL))objc_msgSend)(self,sel_registerName("retain"));
        if(lockdate_last) {
            ((void(*)(id,SEL))objc_msgSend)(lockdate_last,sel_registerName("release"));
        }
        lockdate_last=self;
    }
    if(lockdate_layout_logs>=6) {
        ++lockdate_layout_logs;
        return;
    }
    ++lockdate_layout_logs;
    int landscape=-1;
    Method getter=class_getInstanceMethod(object_getClass(self),sel_registerName("useLandscapeTimeFontSize"));
    if(method_abi(getter,"Bc",0,"")) {
        landscape=((BOOL(*)(id,SEL))objc_msgSend)(self,sel_registerName("useLandscapeTimeFontSize"));
    }
    fprintf(stderr,"Zizhan lockdate: layout #%u landscapeFont=%d mode=%d\n",lockdate_layout_logs,landscape,lockdate_mode);
    unsigned budget=24;
    lockdate_tree(self,0,&budget);
}
static void lockdate_set_landscape(id self,SEL selector,BOOL value)
{
    BOOL forced=lockdate_mode==1?NO:(lockdate_mode==2?YES:value);
    if(lockdate_landscape_logs<16) {
        ++lockdate_landscape_logs;
        fprintf(stderr,"Zizhan lockdate: setUseLandscapeTimeFontSize %d -> %d\n",(int)value,(int)forced);
    }
    lockdate_landscape_original(self,selector,forced);
}

/* lock-2 (2026-11-23): CSProminentDisplayView placed the date container at y=79 (subtitle 36 pt tall,
 * bottom 115) and the time container at y=84 (594x100) on the 1080x958-point panel: the clock overlaps
 * the date line. After its layout, move a time container that overlaps the date down so the digits
 * (their top is about 21% into the time frame) start 8 pt below the date. zizhan_lock_fix=0 disables. */
static void (*lockfix_layout_original)(id,SEL);
static bool lockfix_enabled;
static unsigned lockfix_logs;
static bool lockfix_contains(id view,const char *class_name,unsigned depth,CGRect *found,CGRect offset)
{
    if(!view || !depth) {
        return false;
    }
    id children=((id(*)(id,SEL))objc_msgSend)(view,sel_registerName("subviews"));
    if(!children) {
        return false;
    }
    CFIndex count=CFArrayGetCount((CFArrayRef)children);
    for(CFIndex i=0;i<count;++i) {
        id child=(id)CFArrayGetValueAtIndex((CFArrayRef)children,i);
        CGRect f=((CGRect(*)(id,SEL))objc_msgSend)(child,sel_registerName("frame"));
        f.origin.x+=offset.origin.x;
        f.origin.y+=offset.origin.y;
        const char *name=object_getClassName(child);
        if(name && !strcmp(name,class_name)) {
            *found=f;
            return true;
        }
        if(lockfix_contains(child,class_name,depth-1,found,f)) {
            return true;
        }
    }
    return false;
}
static void lockfix_layout(id self,SEL selector)
{
    if(lockfix_layout_original) {
        lockfix_layout_original(self,selector);
    }
    if(!lockfix_enabled) {
        return;
    }
    id children=((id(*)(id,SEL))objc_msgSend)(self,sel_registerName("subviews"));
    if(!children) {
        return;
    }
    id time_container=nil;
    CGRect time_frame=CGRectZero,date=CGRectZero,inner=CGRectZero;
    bool have_date=false;
    CFIndex count=CFArrayGetCount((CFArrayRef)children);
    for(CFIndex i=0;i<count;++i) {
        id child=(id)CFArrayGetValueAtIndex((CFArrayRef)children,i);
        CGRect f=((CGRect(*)(id,SEL))objc_msgSend)(child,sel_registerName("frame"));
        if(!time_container && lockfix_contains(child,"CSProminentTimeView",3,&inner,f)) {
            time_container=child;
            time_frame=f;
        } else if(!have_date && lockfix_contains(child,"CSProminentSubtitleDateView",3,&date,f)) {
            have_date=true;
        }
    }
    if(!time_container || !have_date || time_frame.size.height<=0 || date.size.height<=0) {
        return;
    }
    double date_bottom=date.origin.y+date.size.height;
    double digits_top=time_frame.origin.y+0.21*time_frame.size.height;
    if(digits_top>=date_bottom+4) {
        return;
    }
    double target=date_bottom+8-0.21*time_frame.size.height;
    if(lockfix_logs<8) {
        ++lockfix_logs;
        fprintf(stderr,"Zizhan lockfix: date %.1f..%.1f time y %.1f h %.1f -> y %.1f\n",date.origin.y,date_bottom,
                time_frame.origin.y,time_frame.size.height,target);
    }
    time_frame.origin.y=target;
    ((void(*)(id,SEL,CGRect))objc_msgSend)(time_container,sel_registerName("setFrame:"),time_frame);
}
static void lockfix_install(void)
{
    Class cls=objc_lookUpClass("CSProminentDisplayView");
    if(!cls) {
        fprintf(stderr,"Zizhan lockfix: CSProminentDisplayView absent\n");
        return;
    }
    SEL layout=sel_registerName("layoutSubviews");
    Method own=class_getInstanceMethod(cls,layout);
    Method inherited=class_getInstanceMethod(class_getSuperclass(cls),layout);
    if(own && own!=inherited && method_abi(own,"v",0,"")) {
        lockfix_layout_original=(void(*)(id,SEL))method_setImplementation(own,(IMP)lockfix_layout);
    } else if(inherited && method_abi(inherited,"v",0,"")) {
        lockfix_layout_original=(void(*)(id,SEL))method_getImplementation(inherited);
        if(!class_addMethod(cls,layout,(IMP)lockfix_layout,"v16@0:8")) {
            lockfix_layout_original=NULL;
        }
    }
    fprintf(stderr,"Zizhan lockfix: hook=%d enabled=%d\n",lockfix_layout_original!=NULL,lockfix_enabled);
}

static void lockdate_install(void *context)
{
    (void)context;
    lockfix_install();
    Class cls=objc_lookUpClass("SBFLockScreenDateView");
    if(!cls) {
        fprintf(stderr,"Zizhan lockdate: SBFLockScreenDateView absent\n");
        return;
    }
    SEL layout=sel_registerName("layoutSubviews");
    Method own=class_getInstanceMethod(cls,layout);
    Method inherited=class_getInstanceMethod(class_getSuperclass(cls),layout);
    if(own && own!=inherited && method_abi(own,"v",0,"")) {
        lockdate_layout_original=(void(*)(id,SEL))method_setImplementation(own,(IMP)lockdate_layout);
    } else if(inherited && method_abi(inherited,"v",0,"")) {
        /* Not overridden: add an override on this class only, calling the superclass implementation. */
        lockdate_layout_original=(void(*)(id,SEL))method_getImplementation(inherited);
        if(!class_addMethod(cls,layout,(IMP)lockdate_layout,"v16@0:8")) {
            lockdate_layout_original=NULL;
        }
    }
    SEL set=sel_registerName("setUseLandscapeTimeFontSize:");
    Method landscape=class_getInstanceMethod(cls,set);
    if(landscape && landscape!=class_getInstanceMethod(class_getSuperclass(cls),set) && (method_abi(landscape,"v",1,"B") || method_abi(landscape,"v",1,"c"))) {
        lockdate_landscape_original=(void(*)(id,SEL,BOOL))method_setImplementation(landscape,(IMP)lockdate_set_landscape);
    }
    fprintf(stderr,"Zizhan lockdate: hooks layout=%d landscape=%d mode=%d\n",lockdate_layout_original!=NULL,
            lockdate_landscape_original!=NULL,lockdate_mode);
    for(int64_t seconds=50;seconds<=170;seconds+=60) {
        dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,seconds*(int64_t)NSEC_PER_SEC),dispatch_get_main_queue(),NULL,lockdate_late);
    }
}
/* zizhan SENS-03 diagnostic (boot-arg zizhan_accel_test): every 2 s on the main queue, for five minutes. */
static void
orientation_log_window(const char *proc_name, id win)
{
    if(!win)return;
    Class wcls=object_getClass(win);
    SEL s_hidden=sel_registerName("isHidden"),s_bounds=sel_registerName("bounds");
    SEL s_wori=sel_registerName("_windowInterfaceOrientation"),s_layer=sel_registerName("layer");
    SEL s_ctx=sel_registerName("_contextId");
    int hidden=method_abi(class_getInstanceMethod(wcls,s_hidden),"Bc",0,"")?
        (int)((BOOL(*)(id,SEL))objc_msgSend)(win,s_hidden):-1;
    long wori=method_abi(class_getInstanceMethod(wcls,s_wori),"qQ",0,"")?
        ((long(*)(id,SEL))objc_msgSend)(win,s_wori):-1;
    unsigned ctx=method_abi(class_getInstanceMethod(wcls,s_ctx),"I",0,"")?
        ((unsigned(*)(id,SEL))objc_msgSend)(win,s_ctx):0;
    CGRect b=method_abi(class_getInstanceMethod(wcls,s_bounds),"{",0,"")?
        ((CGRect(*)(id,SEL))objc_msgSend)(win,s_bounds):CGRectZero;
    CGAffineTransform af=CGAffineTransformIdentity,saf=CGAffineTransformIdentity,ssaf=CGAffineTransformIdentity;
    SEL s_af=sel_registerName("affineTransform"),s_sup=sel_registerName("superlayer");
    id layer=method_abi(class_getInstanceMethod(wcls,s_layer),"@",0,"")?
        ((id(*)(id,SEL))objc_msgSend)(win,s_layer):nil;
    if(layer && method_abi(class_getInstanceMethod(object_getClass(layer),s_af),"{",0,"")) {
        af=((CGAffineTransform(*)(id,SEL))objc_msgSend)(layer,s_af);
    }
    id sup=(layer && method_abi(class_getInstanceMethod(object_getClass(layer),s_sup),"@",0,""))?
        ((id(*)(id,SEL))objc_msgSend)(layer,s_sup):nil;
    if(sup && method_abi(class_getInstanceMethod(object_getClass(sup),s_af),"{",0,"")) {
        saf=((CGAffineTransform(*)(id,SEL))objc_msgSend)(sup,s_af);
    }
    id ssup=(sup && method_abi(class_getInstanceMethod(object_getClass(sup),s_sup),"@",0,""))?
        ((id(*)(id,SEL))objc_msgSend)(sup,s_sup):nil;
    if(ssup && method_abi(class_getInstanceMethod(object_getClass(ssup),s_af),"{",0,"")) {
        ssaf=((CGAffineTransform(*)(id,SEL))objc_msgSend)(ssup,s_af);
    }
    fprintf(stderr,"Zizhan orientation win[%s]: %s ctx=0x%x hidden=%d wori=%ld bounds=%.0fx%.0f af=[%.2f %.2f %.2f %.2f] saf=[%.2f %.2f %.2f %.2f] ssaf=[%.2f %.2f %.2f %.2f]\n",
        proc_name,object_getClassName(win),ctx,hidden,wori,b.size.width,b.size.height,
        af.a,af.b,af.c,af.d,saf.a,saf.b,saf.c,saf.d,ssaf.a,ssaf.b,ssaf.c,ssaf.d);
}
static void
orientation_watch(void *unused)
{
    static int last_device=-2,last_interface=-2;static unsigned rounds;
    (void)unused;
    const char *proc_name=getprogname();if(!proc_name)proc_name="?";
    Class device_class=objc_getClass("UIDevice"),app_class=objc_getClass("UIApplication"),win_class=objc_getClass("UIWindow"),scr_class=objc_getClass("UIScreen");
    id device=device_class?((id(*)(Class,SEL))objc_msgSend)(device_class,sel_registerName("currentDevice")):NULL;
    id app=app_class?((id(*)(Class,SEL))objc_msgSend)(app_class,sel_registerName("sharedApplication")):NULL;
    if(!rounds && device)((void(*)(id,SEL))objc_msgSend)(device,sel_registerName("beginGeneratingDeviceOrientationNotifications"));
    int now_device=device?(int)((long(*)(id,SEL))objc_msgSend)(device,sel_registerName("orientation")):-1;
    SEL active=sel_registerName("activeInterfaceOrientation");
    int now_interface=app && class_respondsToSelector(object_getClass(app),active)?(int)((long(*)(id,SEL))objc_msgSend)(app,active):-1;
    if(now_device!=last_device || now_interface!=last_interface) {
        int tl_rot=-1;double scr_rot=-99.0;
        SEL s_tlr=sel_registerName("_transformLayerRotationsAreEnabled"),s_main=sel_registerName("mainScreen"),s_srot=sel_registerName("_rotation");
        if(win_class && method_abi(class_getClassMethod(win_class,s_tlr),"Bc",0,"")) {
            tl_rot=(int)((BOOL(*)(Class,SEL))objc_msgSend)(win_class,s_tlr);
        }
        id scr=(scr_class && method_abi(class_getClassMethod(scr_class,s_main),"@",0,""))?
            ((id(*)(Class,SEL))objc_msgSend)(scr_class,s_main):nil;
        if(scr && method_abi(class_getInstanceMethod(object_getClass(scr),s_srot),"d",0,"")) {
            scr_rot=((double(*)(id,SEL))objc_msgSend)(scr,s_srot);
        }
        fprintf(stderr,"Zizhan orientation: %s device %d interface %d tl_rot=%d scr_rot=%.2f\n",proc_name,now_device,now_interface,tl_rot,scr_rot);
        SEL s_allw=sel_registerName("allWindowsIncludingInternalWindows:onlyVisibleWindows:");
        id wins=(win_class && method_abi(class_getClassMethod(win_class,s_allw),"@",2,"BB") )?
            ((id(*)(Class,SEL,BOOL,BOOL))objc_msgSend)(win_class,s_allw,YES,NO):nil;
        if(!wins && win_class && method_abi(class_getClassMethod(win_class,s_allw),"@",2,"cc")) {
            wins=((id(*)(Class,SEL,BOOL,BOOL))objc_msgSend)(win_class,s_allw,YES,NO);
        }
        CFIndex nw=wins?CFArrayGetCount((CFArrayRef)wins):0;
        for(CFIndex wi=0;wi<nw && wi<24;++wi) {
            orientation_log_window(proc_name,(id)CFArrayGetValueAtIndex((CFArrayRef)wins,wi));
        }
        last_device=now_device;last_interface=now_interface;
    }
    if(++rounds<150)dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC),dispatch_get_main_queue(),NULL,orientation_watch);
}

/* Stage 4: Seed cold-launched SBDeviceApplicationSceneHandle initial settings with the locked or active
 * SpringBoard interface orientation when [self sceneIfExists] == nil (before SBTraitsSceneParticipantManager
 * registers the new scene's participant), preventing a 1-frame portrait (1080x958 sideways) flash.
 * Disable with boot-arg zizhan_scene_orient=0. */
static void (*scene_orient_original)(id,SEL,id,id,id);
static unsigned long (*scene_supp_mask_original)(id,SEL);
static long (*scene_default_orient_original)(id,SEL);
static long (*scene_launching_orient_original)(id,SEL,long);
static bool scene_orient_installed;
static unsigned scene_orient_logs;
static char scene_pad_mask_key;
static unsigned long parse_orient_array_mask(CFTypeRef arr)
{
    if(!arr || CFGetTypeID(arr)!=CFArrayGetTypeID())return 0;
    unsigned long mask=0;
    CFIndex n=CFArrayGetCount((CFArrayRef)arr);
    for(CFIndex i=0;i<n && i<8;i++) {
        CFTypeRef item=CFArrayGetValueAtIndex((CFArrayRef)arr,i);
        if(!item || CFGetTypeID(item)!=CFStringGetTypeID())continue;
        char buf[64]={0};
        if(CFStringGetCString((CFStringRef)item,buf,sizeof(buf),kCFStringEncodingUTF8))
            mask|=zizhan_orient_mask_from_name(buf);
    }
    return mask;
}
static unsigned long zizhan_main_scene_supported_orientations(id self,SEL selector)
{
    unsigned long orig=scene_supp_mask_original?scene_supp_mask_original(self,selector):0;
    if((orig&0x1eUL)==0x1eUL)return orig;
    Class dev_cls=objc_lookUpClass("UIDevice");
    SEL s_cur=sel_registerName("currentDevice"),s_idiom=sel_registerName("userInterfaceIdiom");
    id dev=(dev_cls && method_abi(class_getClassMethod(dev_cls,s_cur),"@",0,""))?
        ((id(*)(Class,SEL))objc_msgSend)(dev_cls,s_cur):nil;
    long idiom=(dev && method_abi(class_getInstanceMethod(object_getClass(dev),s_idiom),"qQ",0,""))?
        ((long(*)(id,SEL))objc_msgSend)(dev,s_idiom):0;
    if(idiom!=1)return orig;
    Class hcls=object_getClass(self);
    SEL s_exists=sel_registerName("sceneIfExists"),s_uics=sel_registerName("uiClientSettings");
    SEL s_supp=sel_registerName("supportedInterfaceOrientations");
    id scene=(hcls && method_abi(class_getInstanceMethod(hcls,s_exists),"@",0,""))?
        ((id(*)(id,SEL))objc_msgSend)(self,s_exists):nil;
    int has_client=0;
    if(scene && ((BOOL(*)(id,SEL,SEL))objc_msgSend)(scene,sel_registerName("respondsToSelector:"),s_uics)) {
        id uics=((id(*)(id,SEL))objc_msgSend)(scene,s_uics);
        if(uics && ((BOOL(*)(id,SEL,SEL))objc_msgSend)(uics,sel_registerName("respondsToSelector:"),s_supp)) {
            unsigned long cm=((unsigned long(*)(id,SEL))objc_msgSend)(uics,s_supp);
            if(cm!=0)has_client=1;
        }
    }
    if(has_client)return orig;
    SEL s_app=sel_registerName("application"),s_classic=sel_registerName("isClassic");
    id app=(hcls && method_abi(class_getInstanceMethod(hcls,s_app),"@",0,""))?
        ((id(*)(id,SEL))objc_msgSend)(self,s_app):nil;
    if(!app)return orig;
    int is_classic=method_abi(class_getInstanceMethod(object_getClass(app),s_classic),"Bc",0,"")?
        (int)((BOOL(*)(id,SEL))objc_msgSend)(app,s_classic):0;
    if(is_classic)return orig;
    unsigned long ipad_mask=0;int has_any=1;
    id cached=objc_getAssociatedObject(app,&scene_pad_mask_key);
    if(cached && CFGetTypeID((CFTypeRef)cached)==CFNumberGetTypeID()) {
        long long packed=0;
        if(CFNumberGetValue((CFNumberRef)cached,kCFNumberLongLongType,&packed)) {
            ipad_mask=(unsigned long)(packed&0xff);
            has_any=(int)((packed>>8)&1);
        }
    } else {
        SEL s_info=sel_registerName("info"),s_burl=sel_registerName("bundleURL");
        id info=method_abi(class_getInstanceMethod(object_getClass(app),s_info),"@",0,"")?
            ((id(*)(id,SEL))objc_msgSend)(app,s_info):nil;
        id burl=(info && method_abi(class_getInstanceMethod(object_getClass(info),s_burl),"@",0,""))?
            ((id(*)(id,SEL))objc_msgSend)(info,s_burl):nil;
        Class bcls=objc_lookUpClass("NSBundle");
        SEL s_bwith=sel_registerName("bundleWithURL:"),s_idict=sel_registerName("infoDictionary");
        id bundle=(burl && bcls && method_abi(class_getClassMethod(bcls,s_bwith),"@",1,"@"))?
            ((id(*)(Class,SEL,id))objc_msgSend)(bcls,s_bwith,burl):nil;
        CFDictionaryRef idict=(bundle && method_abi(class_getInstanceMethod(object_getClass(bundle),s_idict),"@",0,""))?
            (CFDictionaryRef)((id(*)(id,SEL))objc_msgSend)(bundle,s_idict):NULL;
        if(idict && CFGetTypeID((CFTypeRef)idict)==CFDictionaryGetTypeID()) {
            CFTypeRef pad_arr=CFDictionaryGetValue(idict,CFSTR("UISupportedInterfaceOrientations~ipad"));
            CFTypeRef base_arr=CFDictionaryGetValue(idict,CFSTR("UISupportedInterfaceOrientations"));
            CFTypeRef ph_arr=CFDictionaryGetValue(idict,CFSTR("UISupportedInterfaceOrientations~iphone"));
            ipad_mask=parse_orient_array_mask(pad_arr);
            has_any=(pad_arr!=NULL || base_arr!=NULL || ph_arr!=NULL)?1:0;
            long long packed=(long long)(ipad_mask&0xff)|((long long)has_any<<8);
            CFNumberRef num=CFNumberCreate(NULL,kCFNumberLongLongType,&packed);
            if(num){objc_setAssociatedObject(app,&scene_pad_mask_key,(id)num,OBJC_ASSOCIATION_RETAIN_NONATOMIC);CFRelease(num);}
        }
    }
    return zizhan_effective_pad_orient_mask(1,0,0,orig,ipad_mask,has_any);
}
static void query_sb_orient_state(id handle,long *live_out,int *locked_out,long *lock_ori_out,long *active_ori_out)
{
    long live=0,lock_ori=0,active_ori=0;
    int locked=0;
    if(handle) {
        Class hcls=object_getClass(handle);
        SEL s_cur=sel_registerName("currentInterfaceOrientation");
        if(hcls && method_abi(class_getInstanceMethod(hcls,s_cur),"qQ",0,""))
            live=((long(*)(id,SEL))objc_msgSend)(handle,s_cur);
    }
    id lm=singleton("SBOrientationLockManager");
    if(lm) {
        Class lcls=object_getClass(lm);
        SEL s_locked=sel_registerName("isUserLocked"),s_lori=sel_registerName("userLockOrientation");
        if(method_abi(class_getInstanceMethod(lcls,s_locked),"Bc",0,""))
            locked=(int)((BOOL(*)(id,SEL))objc_msgSend)(lm,s_locked);
        if(method_abi(class_getInstanceMethod(lcls,s_lori),"qQ",0,""))
            lock_ori=((long(*)(id,SEL))objc_msgSend)(lm,s_lori);
    }
    Class acls=objc_lookUpClass("UIApplication");
    id app=(acls && method_abi(class_getClassMethod(acls,sel_registerName("sharedApplication")),"@",0,""))?
        ((id(*)(Class,SEL))objc_msgSend)(acls,sel_registerName("sharedApplication")):nil;
    if(app) {
        SEL s_active=sel_registerName("activeInterfaceOrientation");
        if(method_abi(class_getInstanceMethod(object_getClass(app),s_active),"qQ",0,""))
            active_ori=((long(*)(id,SEL))objc_msgSend)(app,s_active);
    }
    if(live_out)*live_out=live;
    if(locked_out)*locked_out=locked;
    if(lock_ori_out)*lock_ori_out=lock_ori;
    if(active_ori_out)*active_ori_out=active_ori;
}
static long zizhan_default_interface_orientation(id self,SEL selector)
{
    long orig=scene_default_orient_original?scene_default_orient_original(self,selector):0;
    long live=0,lock_ori=0,active_ori=0;int locked=0,sup=0;
    query_sb_orient_state(self,&live,&locked,&lock_ori,&active_ori);
    long cand=zizhan_scene_orient_candidate(1,live!=0,locked,lock_ori,active_ori);
    Class hcls=object_getClass(self);
    SEL s_supports=sel_registerName("_mainSceneSupportsInterfaceOrientation:");
    if(cand>=1 && cand<=4 && hcls && method_abi(class_getInstanceMethod(hcls,s_supports),"Bc",1,"q"))
        sup=(int)((BOOL(*)(id,SEL,long))objc_msgSend)(self,s_supports,cand);
    return zizhan_scene_launch_orient_override(1,live,orig,locked,lock_ori,active_ori,sup);
}
static long zizhan_launching_interface_orientation(id self,SEL selector,long req_ori)
{
    long orig=scene_launching_orient_original?scene_launching_orient_original(self,selector,req_ori):req_ori;
    long live=0,lock_ori=0,active_ori=0;int locked=0,sup=0;
    query_sb_orient_state(self,&live,&locked,&lock_ori,&active_ori);
    long cand=zizhan_scene_orient_candidate(1,live!=0,locked,lock_ori,active_ori);
    Class hcls=object_getClass(self);
    SEL s_supports=sel_registerName("_mainSceneSupportsInterfaceOrientation:");
    if(cand>=1 && cand<=4 && hcls && method_abi(class_getInstanceMethod(hcls,s_supports),"Bc",1,"q"))
        sup=(int)((BOOL(*)(id,SEL,long))objc_msgSend)(self,s_supports,cand);
    long out=zizhan_scene_launch_orient_override(1,live,orig,locked,lock_ori,active_ori,sup);
    if(out!=orig && __atomic_fetch_add(&scene_orient_logs,1,__ATOMIC_RELAXED)<24) {
        fprintf(stderr,"Zizhan scene18: launch container orientation %ld -> %ld (req=%ld locked=%d lock_ori=%ld active=%ld)\n",
            orig,out,req_ori,locked,lock_ori,active_ori);
    }
    return out;
}
static void zizhan_modify_scene_settings(id self,SEL selector,id settings,id ctx,id entity)
{
    SEL s_exists=sel_registerName("sceneIfExists");
    SEL s_resp=sel_registerName("respondsToSelector:");
    SEL s_get_ori=sel_registerName("interfaceOrientation");
    SEL s_set_ori=sel_registerName("setInterfaceOrientation:");
    SEL s_supports=sel_registerName("_mainSceneSupportsInterfaceOrientation:");
    Class hcls=object_getClass(self);
    int exists=1;
    if(hcls && method_abi(class_getInstanceMethod(hcls,s_exists),"@",0,"")) {
        exists=((id(*)(id,SEL))objc_msgSend)(self,s_exists)!=nil;
    }
    long chosen=0,before=-1,lock_ori=0,active_ori=0;
    int locked=0,sup=-1;
    if(!exists && settings &&
       ((BOOL(*)(id,SEL,SEL))objc_msgSend)(settings,s_resp,s_get_ori) &&
       ((BOOL(*)(id,SEL,SEL))objc_msgSend)(settings,s_resp,s_set_ori)) {
        before=((long(*)(id,SEL))objc_msgSend)(settings,s_get_ori);
        id lm=singleton("SBOrientationLockManager");
        if(lm) {
            Class lcls=object_getClass(lm);
            SEL s_locked=sel_registerName("isUserLocked"),s_lori=sel_registerName("userLockOrientation");
            if(method_abi(class_getInstanceMethod(lcls,s_locked),"Bc",0,""))
                locked=(int)((BOOL(*)(id,SEL))objc_msgSend)(lm,s_locked);
            if(method_abi(class_getInstanceMethod(lcls,s_lori),"qQ",0,""))
                lock_ori=((long(*)(id,SEL))objc_msgSend)(lm,s_lori);
        }
        Class acls=objc_lookUpClass("UIApplication");
        id app=(acls && method_abi(class_getClassMethod(acls,sel_registerName("sharedApplication")),"@",0,""))?
            ((id(*)(Class,SEL))objc_msgSend)(acls,sel_registerName("sharedApplication")):nil;
        if(app) {
            SEL s_active=sel_registerName("activeInterfaceOrientation");
            if(method_abi(class_getInstanceMethod(object_getClass(app),s_active),"qQ",0,""))
                active_ori=((long(*)(id,SEL))objc_msgSend)(app,s_active);
        }
        long cand=zizhan_scene_orient_candidate(1,exists,locked,lock_ori,active_ori);
        sup=0;
        if(cand>=1 && cand<=4 && method_abi(class_getInstanceMethod(hcls,s_supports),"Bc",1,"q")) {
            sup=(int)((BOOL(*)(id,SEL,long))objc_msgSend)(self,s_supports,cand);
        }
        chosen=zizhan_scene_orient_apply(cand,before,sup);
        if(chosen) {
            ((void(*)(id,SEL,long))objc_msgSend)(settings,s_set_ori,chosen);
        }
    }
    scene_orient_original(self,selector,settings,ctx,entity);
    if(chosen && settings) {
        long after=((long(*)(id,SEL))objc_msgSend)(settings,s_get_ori);
        if(after!=chosen) {
            ((void(*)(id,SEL,long))objc_msgSend)(settings,s_set_ori,chosen);
            after=((long(*)(id,SEL))objc_msgSend)(settings,s_get_ori);
        }
        if(__atomic_fetch_add(&scene_orient_logs,1,__ATOMIC_RELAXED)<16) {
            fprintf(stderr,"Zizhan scene18: cold launch initial orientation %ld -> %ld (after=%ld locked=%d lock_ori=%ld active=%ld)\n",
                before,chosen,after,locked,lock_ori,active_ori);
        }
    } else if(!exists && __atomic_fetch_add(&scene_orient_logs,1,__ATOMIC_RELAXED)<8) {
        fprintf(stderr,"Zizhan scene18: cold launch kept orientation before=%ld locked=%d lock_ori=%ld active=%ld sup=%d\n",
            before,locked,lock_ori,active_ori,sup);
    }
}
static void springboard_scene_orientation_policy(void)
{
    if(!springboard_process || scene_orient_installed || boot_option("zizhan_scene_orient=0"))return;
    Class cls=objc_lookUpClass("SBDeviceApplicationSceneHandle");
    Class core=objc_lookUpClass("FBSSceneSettingsCore");
    if(!cls || !core)return;
    SEL sel=sel_registerName("_modifyApplicationSceneSettings:fromRequestContext:entity:");
    SEL sel_mask=sel_registerName("_mainSceneSupportedInterfaceOrientations");
    SEL sel_def=sel_registerName("defaultInterfaceOrientation");
    SEL sel_launch=sel_registerName("_launchingInterfaceOrientationForOrientation:");
    Method own=class_getInstanceMethod(cls,sel);
    Method sup=class_getInstanceMethod(class_getSuperclass(cls),sel);
    Method mask_own=class_getInstanceMethod(cls,sel_mask);
    Method mask_sup=class_getInstanceMethod(class_getSuperclass(cls),sel_mask);
    Method def_own=class_getInstanceMethod(cls,sel_def);
    Method launch_own=class_getInstanceMethod(cls,sel_launch);
    Method core_get=class_getInstanceMethod(core,sel_registerName("interfaceOrientation"));
    Method core_set=class_getInstanceMethod(core,sel_registerName("setInterfaceOrientation:"));
    if(!own || own==sup || !method_abi(own,"v",3,"@@@") ||
       !mask_own || mask_own==mask_sup || !method_abi(mask_own,"qQ",0,"") ||
       !def_own || !method_abi(def_own,"qQ",0,"") ||
       !launch_own || !(method_abi(launch_own,"qQ",1,"q") || method_abi(launch_own,"qQ",1,"Q")) ||
       !method_abi(core_get,"qQ",0,"") ||
       !(method_abi(core_set,"v",1,"q") || method_abi(core_set,"v",1,"Q")) ||
       !method_abi(class_getInstanceMethod(cls,sel_registerName("sceneIfExists")),"@",0,"") ||
       !method_abi(class_getInstanceMethod(cls,sel_registerName("_mainSceneSupportsInterfaceOrientation:")),"Bc",1,"q"))return;
    scene_supp_mask_original=(unsigned long(*)(id,SEL))method_setImplementation(mask_own,(IMP)zizhan_main_scene_supported_orientations);
    scene_default_orient_original=(long(*)(id,SEL))method_setImplementation(def_own,(IMP)zizhan_default_interface_orientation);
    scene_launching_orient_original=(long(*)(id,SEL,long))method_setImplementation(launch_own,(IMP)zizhan_launching_interface_orientation);
    scene_orient_original=(void(*)(id,SEL,id,id,id))method_setImplementation(own,(IMP)zizhan_modify_scene_settings);
    scene_orient_installed=scene_orient_original!=NULL && scene_supp_mask_original!=NULL &&
        scene_default_orient_original!=NULL && scene_launching_orient_original!=NULL;
    fprintf(stderr,"Zizhan scene18: SBDeviceApplicationSceneHandle cold-launch orientation hook=%d\n",(int)scene_orient_installed);
}
static void apply_boot_orientation_lock(void *context)
{
    long target=(long)(intptr_t)context;
    id lm=singleton("SBOrientationLockManager");
    SEL s_lock=sel_registerName("lock:");
    if(!lm || target<1 || target>4 || !method_abi(class_getInstanceMethod(object_getClass(lm),s_lock),"v",1,"q")) {
        fprintf(stderr,"Zizhan scene18: boot orientation lock=%ld SKIP manager=%d\n",target,lm!=nil);
        return;
    }
    ((void(*)(id,SEL,long))objc_msgSend)(lm,s_lock,target);
    fprintf(stderr,"Zizhan scene18: applied boot orientation lock=%ld\n",target);
}

/* Stage 5: Read-only notifyd GNSS -> CoreLocation bridge. Avoids synchronous XPC hangs on
 * com.apple.locationd.synchronous in adapted LaunchDaemons mode; never logs raw coordinates. */
struct zizhan_cl_coord2d { double latitude; double longitude; };
static id (*cl_location_original)(id,SEL);
static void (*cl_start_original)(id,SEL);
static BOOL (*cl_enabled_original)(Class,SEL);
static int (*cl_auth_inst_original)(id,SEL);
static int (*cl_auth_cls_original)(Class,SEL);
static bool cl_policy_installed;
static id cl_active_manager;
static int cl_tok_sv=-1,cl_tok_ll=-1,cl_tok_aa=-1,cl_tok_vl=-1,cl_tok_upd=-1;
static dispatch_once_t cl_tok_once;

static int zizhan_read_gnss_snapshot(ZizhanGnssSnapshot *out)
{
    dispatch_once(&cl_tok_once,^{
        (void)notify_register_check(ZIZHAN_GNSS_NOTIFY_SV,&cl_tok_sv);
        (void)notify_register_check(ZIZHAN_GNSS_NOTIFY_LATLON,&cl_tok_ll);
        (void)notify_register_check(ZIZHAN_GNSS_NOTIFY_ACCALT,&cl_tok_aa);
        (void)notify_register_check(ZIZHAN_GNSS_NOTIFY_VEL,&cl_tok_vl);
    });
    uint64_t sv=0,ll=0,aa=0,vl=0;
    if(cl_tok_sv!=-1)(void)notify_get_state(cl_tok_sv,&sv);
    if(cl_tok_ll!=-1)(void)notify_get_state(cl_tok_ll,&ll);
    if(cl_tok_aa!=-1)(void)notify_get_state(cl_tok_aa,&aa);
    if(cl_tok_vl!=-1)(void)notify_get_state(cl_tok_vl,&vl);
    return zizhan_gnss_unpack(sv,ll,aa,vl,out);
}

static id zizhan_make_cllocation(const ZizhanGnssSnapshot *snap)
{
    Class loc_cls=objc_lookUpClass("CLLocation");
    Class date_cls=objc_lookUpClass("NSDate");
    SEL s_init=sel_registerName("initWithCoordinate:altitude:horizontalAccuracy:verticalAccuracy:course:speed:timestamp:");
    if(!loc_cls || !date_cls || !method_abi(class_getInstanceMethod(loc_cls,s_init),"@",7,"{ddddd@"))return nil;
    id now=((id(*)(Class,SEL))objc_msgSend)(date_cls,sel_registerName("date"));
    id raw=((id(*)(Class,SEL))objc_msgSend)(loc_cls,sel_registerName("alloc"));
    if(!raw || !now)return nil;
    struct zizhan_cl_coord2d coord={snap->lat_e7/1e7,snap->lon_e7/1e7};
    double alt=snap->alt_dm/10.0;
    double hacc=snap->hor_unc_dm>0?(snap->hor_unc_dm/10.0):50.0;
    double course=snap->heading_ddeg>=0?(snap->heading_ddeg/10.0):-1.0;
    double speed=snap->speed_cms>=0?(snap->speed_cms/100.0):-1.0;
    id obj=((id(*)(id,SEL,struct zizhan_cl_coord2d,double,double,double,double,double,id))objc_msgSend)(
        raw,s_init,coord,alt,hacc,25.0,course,speed,now);
    return obj?((id(*)(id,SEL))objc_msgSend)(obj,sel_registerName("autorelease")):nil;
}

static void zizhan_deliver_gnss_to_manager(id mgr)
{
    ZizhanGnssSnapshot snap;
    if(!mgr || !zizhan_read_gnss_snapshot(&snap))return;
    SEL s_del=sel_registerName("delegate");
    SEL s_upd=sel_registerName("locationManager:didUpdateLocations:");
    if(!method_abi(class_getInstanceMethod(object_getClass(mgr),s_del),"@",0,""))return;
    id del=((id(*)(id,SEL))objc_msgSend)(mgr,s_del);
    if(!del || !((BOOL(*)(id,SEL,SEL))objc_msgSend)(del,sel_registerName("respondsToSelector:"),s_upd))return;
    id loc=zizhan_make_cllocation(&snap);
    Class arr_cls=objc_lookUpClass("NSArray");
    if(!loc || !arr_cls)return;
    id arr=((id(*)(Class,SEL,id))objc_msgSend)(arr_cls,sel_registerName("arrayWithObject:"),loc);
    if(arr)((void(*)(id,SEL,id,id))objc_msgSend)(del,s_upd,mgr,arr);
}

static BOOL zizhan_cl_services_enabled(Class cls,SEL selector)
{
    (void)cls;(void)selector;
    return YES;
}

static int zizhan_cl_auth_inst(id self,SEL selector)
{
    (void)self;(void)selector;
    return 3; /* kCLAuthorizationStatusAuthorizedAlways */
}

static int zizhan_cl_auth_cls(Class cls,SEL selector)
{
    (void)cls;(void)selector;
    return 3; /* kCLAuthorizationStatusAuthorizedAlways */
}

static id zizhan_cl_location(id self,SEL selector)
{
    (void)self;(void)selector;
    ZizhanGnssSnapshot snap;
    if(zizhan_read_gnss_snapshot(&snap)) {
        id loc=zizhan_make_cllocation(&snap);
        if(loc)return loc;
    }
    if(boot_option("zizhan_cl_forward_daemon=1") && cl_location_original)
        return cl_location_original(self,selector);
    return nil;
}

static void zizhan_cl_start_updating(id self,SEL selector)
{
    cl_active_manager=self;
    if(cl_tok_upd==-1) {
        (void)notify_register_dispatch(ZIZHAN_GNSS_NOTIFY_UPDATED,&cl_tok_upd,dispatch_get_main_queue(),^(int t){
            (void)t;
            if(cl_active_manager)zizhan_deliver_gnss_to_manager(cl_active_manager);
        });
    }
    if(boot_option("zizhan_cl_forward_daemon=1") && cl_start_original)
        cl_start_original(self,selector);
    zizhan_deliver_gnss_to_manager(self);
}

static void zizhan_cllocation_policy(void)
{
    if(cl_policy_installed || boot_option("zizhan_cllocation=0"))return;
    Class cls=objc_lookUpClass("CLLocationManager");
    Class loc_cls=objc_lookUpClass("CLLocation");
    if(!cls || !loc_cls)return;
    Method m_loc=class_getInstanceMethod(cls,sel_registerName("location"));
    Method m_start=class_getInstanceMethod(cls,sel_registerName("startUpdatingLocation"));
    SEL s_init=sel_registerName("initWithCoordinate:altitude:horizontalAccuracy:verticalAccuracy:course:speed:timestamp:");
    if(!method_abi(m_loc,"@",0,"") || !method_abi(m_start,"v",0,"") ||
       !method_abi(class_getInstanceMethod(loc_cls,s_init),"@",7,"{ddddd@"))return;
    cl_location_original=(id(*)(id,SEL))method_setImplementation(m_loc,(IMP)zizhan_cl_location);
    cl_start_original=(void(*)(id,SEL))method_setImplementation(m_start,(IMP)zizhan_cl_start_updating);
    Method m_en=class_getClassMethod(cls,sel_registerName("locationServicesEnabled"));
    if(method_abi(m_en,"Bc",0,""))
        cl_enabled_original=(BOOL(*)(Class,SEL))method_setImplementation(m_en,(IMP)zizhan_cl_services_enabled);
    Method m_ai=class_getInstanceMethod(cls,sel_registerName("authorizationStatus"));
    if(method_abi(m_ai,"iI",0,""))
        cl_auth_inst_original=(int(*)(id,SEL))method_setImplementation(m_ai,(IMP)zizhan_cl_auth_inst);
    Method m_ac=class_getClassMethod(cls,sel_registerName("authorizationStatus"));
    if(method_abi(m_ac,"iI",0,""))
        cl_auth_cls_original=(int(*)(Class,SEL))method_setImplementation(m_ac,(IMP)zizhan_cl_auth_cls);
    cl_policy_installed=cl_location_original!=NULL && cl_start_original!=NULL;
    fprintf(stderr,"Zizhan loc18: CLLocationManager GNSS bridge installed=%d en_hook=%d auth_hook=%d (%s)\n",
        (int)cl_policy_installed,cl_enabled_original!=NULL,cl_auth_inst_original!=NULL,getprogname()?getprogname():"?");
}

static void zizhan_cllocation_image(const struct mach_header *header,intptr_t slide)
{
    (void)header;(void)slide;
    zizhan_cllocation_policy();
}

static unsigned loc_probe_tick_count;
static void zizhan_loc_probe_tick(void *unused)
{
    (void)unused;
    (void)dlopen("/System/Library/Frameworks/CoreLocation.framework/CoreLocation",RTLD_LAZY);
    zizhan_cllocation_policy();
    ZizhanGnssSnapshot snap;
    int gnss_valid=zizhan_read_gnss_snapshot(&snap);
    /* Verify CLLocation value-object construction & ABI readback without touching locationd XPC. */
    ZizhanGnssSnapshot synth=snap;
    if(!gnss_valid) {
        synth.fix_status=2;synth.lat_e7=373349000;synth.lon_e7=-1220090200;
        synth.hor_unc_dm=45;synth.alt_dm=523;synth.speed_cms=0;synth.heading_ddeg=-1;
    }
    id sample_loc=zizhan_make_cllocation(&synth);
    int cl_abi_ok=0;
    double hacc=-1.0;
    if(sample_loc && method_abi(class_getInstanceMethod(object_getClass(sample_loc),sel_registerName("horizontalAccuracy")),"d",0,"")) {
        hacc=((double(*)(id,SEL))objc_msgSend)(sample_loc,sel_registerName("horizontalAccuracy"));
        cl_abi_ok=(hacc>0.0);
    }
    Class cls=objc_lookUpClass("CLLocationManager");
    int enabled=-1;
    if(cls && method_abi(class_getClassMethod(cls,sel_registerName("locationServicesEnabled")),"Bc",0,""))
        enabled=(int)((BOOL(*)(Class,SEL))objc_msgSend)(cls,sel_registerName("locationServicesEnabled"));
    /* Privacy rule (AGENTS.md): log only sanitized status/accuracy/SV counts, NEVER raw coordinates. */
    fprintf(stderr,"Zizhan loc18 probe[%u]: services_enabled=%d cl_abi_ok=%d cl_hacc=%.1fm gnss_valid=%d seq=%u eng=%u fix=%u vis=%u trk=%u used=%u cn0=%.1f\n",
        ++loc_probe_tick_count,enabled,cl_abi_ok,hacc,gnss_valid,
        snap.seq,snap.engine_state,snap.fix_status,snap.sv_visible,snap.sv_tracking,snap.sv_used,snap.max_cn0_tenths/10.0);
    fflush(stderr);
    if(loc_probe_tick_count<5)
        dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,15*NSEC_PER_SEC),
            dispatch_get_global_queue(QOS_CLASS_UTILITY,0),NULL,zizhan_loc_probe_tick);
}

#include "zizhan-avcapture18.h"

/* --- Tier 2.1 & 2.4 (APP-03 / PERIPH-01): Third-party container sandbox, Keychain -34018 fallback,
 * Shared AppGroup container, Haptics/Earpiece bridge, and Folded 9:16 window fit --- */
static bool gAppCompatObjcInstalled;
static IMP gOrigAppGroupUrl;
static IMP gOrigImpactOccurred;
static IMP gOrigImpactIntensity;
static IMP gOrigSelectionChanged;
static IMP gOrigNotifOccurred;
static IMP gOrigAudioSetMode;
static IMP gOrigAudioOverridePort;
static IMP gOrigWinLayout;
static bool gVoiceChatModeActive;

static OSStatus (*gOrigSecItemAdd)(CFDictionaryRef, CFTypeRef *);
static OSStatus (*gOrigSecItemCopyMatching)(CFDictionaryRef, CFTypeRef *);
static OSStatus (*gOrigSecItemUpdate)(CFDictionaryRef, CFDictionaryRef);
static OSStatus (*gOrigSecItemDelete)(CFDictionaryRef);
static void (*gOrigAudioPlaySys)(uint32_t);
static void (*gOrigAudioPlayAlert)(uint32_t);

static pthread_mutex_t gKeychainFallbackLock = PTHREAD_MUTEX_INITIALIZER;

static void zizhan_kc_plist_path(char *out, size_t cap) {
    const char *home = getenv("HOME");
    if (!home || !home[0]) home = "/private/var/mobile";
    snprintf(out, cap, "%s/Library/PongKeychain.plist", home);
}

static CFMutableDictionaryRef zizhan_kc_load_locked(void) {
    char path[512];
    zizhan_kc_plist_path(path, sizeof(path));
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        struct stat st;
        if (fstat(fd, &st) == 0 && st.st_size > 0 && st.st_size < (4 << 20)) {
            uint8_t *buf = (uint8_t *)malloc((size_t)st.st_size);
            if (buf) {
                ssize_t r = read(fd, buf, (size_t)st.st_size);
                if (r == st.st_size) {
                    CFDataRef d = CFDataCreate(NULL, buf, (CFIndex)r);
                    if (d) {
                        CFPropertyListRef pl = CFPropertyListCreateWithData(NULL, d, kCFPropertyListMutableContainersAndLeaves, NULL, NULL);
                        CFRelease(d);
                        free(buf);
                        close(fd);
                        if (pl && CFGetTypeID(pl) == CFDictionaryGetTypeID()) return (CFMutableDictionaryRef)pl;
                        if (pl) CFRelease(pl);
                        return CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
                    }
                }
                free(buf);
            }
        }
        close(fd);
    }
    return CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
}

static void zizhan_kc_save_locked(CFDictionaryRef store) {
    if (!store) return;
    CFDataRef data = CFPropertyListCreateData(NULL, store, kCFPropertyListBinaryFormat_v1_0, 0, NULL);
    if (!data) return;
    char path[512], tmp[528];
    zizhan_kc_plist_path(path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) {
        const UInt8 *p = CFDataGetBytePtr(data);
        CFIndex len = CFDataGetLength(data);
        ssize_t off = 0;
        while (off < len) {
            ssize_t w = write(fd, p + off, (size_t)(len - off));
            if (w <= 0) break;
            off += w;
        }
        close(fd);
        if (off == len) (void)rename(tmp, path);
        else (void)unlink(tmp);
    }
    CFRelease(data);
}

static CFStringRef zizhan_kc_item_key(CFDictionaryRef dict) {
    if (!dict) return CFSTR("genp||");
    const CFStringRef keys[4] = { CFSTR("class"), CFSTR("svce"), CFSTR("acct"), CFSTR("sdmn") };
    char parts[4][128];
    for (unsigned i = 0; i < 4; ++i) {
        parts[i][0] = '\0';
        CFTypeRef v = CFDictionaryGetValue(dict, keys[i]);
        if (v && CFGetTypeID(v) == CFStringGetTypeID()) {
            CFStringGetCString((CFStringRef)v, parts[i], sizeof(parts[i]), kCFStringEncodingUTF8);
        }
    }
    char full[560];
    snprintf(full, sizeof(full), "%s|%s|%s|%s",
             parts[0][0] ? parts[0] : "genp", parts[1], parts[2], parts[3]);
    return CFStringCreateWithCString(NULL, full, kCFStringEncodingUTF8);
}

static OSStatus zizhan_hook_SecItemAdd(CFDictionaryRef attributes, CFTypeRef *result) {
    OSStatus st = gOrigSecItemAdd ? gOrigSecItemAdd(attributes, result) : -34018;
    if (st != -34018 && st != -25291) return st;
    if (!attributes) return -50;
    pthread_mutex_lock(&gKeychainFallbackLock);
    CFMutableDictionaryRef store = zizhan_kc_load_locked();
    CFStringRef k = zizhan_kc_item_key(attributes);
    OSStatus outSt = 0;
    if (CFDictionaryContainsKey(store, k)) {
        outSt = -25299; /* errSecDuplicateItem */
    } else {
        CFMutableDictionaryRef entry = CFDictionaryCreateMutableCopy(NULL, 0, attributes);
        CFDictionaryRemoveValue(entry, CFSTR("r_Data"));
        CFDictionaryRemoveValue(entry, CFSTR("r_Attributes"));
        CFDictionarySetValue(store, k, entry);
        zizhan_kc_save_locked(store);
        if (result) {
            CFTypeRef rData = CFDictionaryGetValue(attributes, CFSTR("r_Data"));
            CFTypeRef vData = CFDictionaryGetValue(entry, CFSTR("v_Data"));
            if (rData == kCFBooleanTrue && vData) *result = CFRetain(vData);
            else *result = CFRetain(entry);
        }
        CFRelease(entry);
    }
    CFRelease(k);
    CFRelease(store);
    pthread_mutex_unlock(&gKeychainFallbackLock);
    return outSt;
}

static OSStatus zizhan_hook_SecItemCopyMatching(CFDictionaryRef query, CFTypeRef *result) {
    OSStatus st = gOrigSecItemCopyMatching ? gOrigSecItemCopyMatching(query, result) : -34018;
    if (st != -34018 && st != -25291 && st != -25300) return st;
    if (!query) return -50;
    pthread_mutex_lock(&gKeychainFallbackLock);
    CFMutableDictionaryRef store = zizhan_kc_load_locked();
    CFStringRef k = zizhan_kc_item_key(query);
    CFDictionaryRef entry = (CFDictionaryRef)CFDictionaryGetValue(store, k);
    OSStatus outSt = -25300; /* errSecItemNotFound */
    if (entry && CFGetTypeID(entry) == CFDictionaryGetTypeID()) {
        outSt = 0;
        if (result) {
            CFTypeRef rData = CFDictionaryGetValue(query, CFSTR("r_Data"));
            CFTypeRef rAttr = CFDictionaryGetValue(query, CFSTR("r_Attributes"));
            CFTypeRef vData = CFDictionaryGetValue(entry, CFSTR("v_Data"));
            if (rData == kCFBooleanTrue && rAttr != kCFBooleanTrue && vData) {
                *result = CFRetain(vData);
            } else {
                *result = CFDictionaryCreateCopy(NULL, entry);
            }
        }
    }
    CFRelease(k);
    CFRelease(store);
    pthread_mutex_unlock(&gKeychainFallbackLock);
    return outSt;
}

static OSStatus zizhan_hook_SecItemUpdate(CFDictionaryRef query, CFDictionaryRef attrsToUpdate) {
    OSStatus st = gOrigSecItemUpdate ? gOrigSecItemUpdate(query, attrsToUpdate) : -34018;
    if (st != -34018 && st != -25291 && st != -25300) return st;
    if (!query || !attrsToUpdate) return -50;
    pthread_mutex_lock(&gKeychainFallbackLock);
    CFMutableDictionaryRef store = zizhan_kc_load_locked();
    CFStringRef k = zizhan_kc_item_key(query);
    CFDictionaryRef cur = (CFDictionaryRef)CFDictionaryGetValue(store, k);
    OSStatus outSt = -25300;
    if (cur && CFGetTypeID(cur) == CFDictionaryGetTypeID()) {
        CFMutableDictionaryRef upd = CFDictionaryCreateMutableCopy(NULL, 0, cur);
        const CFIndex n = CFDictionaryGetCount(attrsToUpdate);
        if (n > 0 && n <= 64) {
            const void *ukeys[64], *uvals[64];
            CFDictionaryGetKeysAndValues(attrsToUpdate, ukeys, uvals);
            for (CFIndex i = 0; i < n; ++i) CFDictionarySetValue(upd, ukeys[i], uvals[i]);
        }
        CFDictionarySetValue(store, k, upd);
        CFRelease(upd);
        zizhan_kc_save_locked(store);
        outSt = 0;
    }
    CFRelease(k);
    CFRelease(store);
    pthread_mutex_unlock(&gKeychainFallbackLock);
    return outSt;
}

static OSStatus zizhan_hook_SecItemDelete(CFDictionaryRef query) {
    OSStatus st = gOrigSecItemDelete ? gOrigSecItemDelete(query) : -34018;
    if (st != -34018 && st != -25291 && st != -25300) return st;
    if (!query) return -50;
    pthread_mutex_lock(&gKeychainFallbackLock);
    CFMutableDictionaryRef store = zizhan_kc_load_locked();
    CFStringRef k = zizhan_kc_item_key(query);
    OSStatus outSt = CFDictionaryContainsKey(store, k) ? 0 : -25300;
    if (outSt == 0) {
        CFDictionaryRemoveValue(store, k);
        zizhan_kc_save_locked(store);
    }
    CFRelease(k);
    CFRelease(store);
    pthread_mutex_unlock(&gKeychainFallbackLock);
    return outSt;
}

static void zizhan_hook_AudioPlaySys(uint32_t soundID) {
    if (soundID == 1519u || soundID == 1520u || soundID == 1521u || soundID == 4095u ||
        (soundID >= 1100u && soundID <= 1400u)) {
        (void)zizhan_cam_ctl_write(0x102);
    }
    if (gOrigAudioPlaySys) gOrigAudioPlaySys(soundID);
}

static void zizhan_hook_AudioPlayAlert(uint32_t soundID) {
    (void)zizhan_cam_ctl_write(0x103);
    if (gOrigAudioPlayAlert) gOrigAudioPlayAlert(soundID);
}

static id zizhan_app_group_url(id self, SEL _cmd, id groupId) {
    id orig = gOrigAppGroupUrl ? ((id(*)(id, SEL, id))gOrigAppGroupUrl)(self, _cmd, groupId) : nil;
    if (orig) return orig;
    const char *gid = groupId ? ((const char *(*)(id, SEL))objc_msgSend)(groupId, sel_registerName("UTF8String")) : NULL;
    if (!gid || !gid[0]) return nil;
    char safe[192];
    size_t j = 0;
    for (size_t i = 0; gid[i] && j + 1 < sizeof(safe); ++i) {
        const char c = gid[i];
        safe[j++] = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                     (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_') ? c : '_';
    }
    safe[j] = '\0';
    (void)mkdir("/private/var/mobile/Containers/Shared", 0755);
    (void)mkdir("/private/var/mobile/Containers/Shared/AppGroup", 0777);
    char full[320];
    snprintf(full, sizeof(full), "/private/var/mobile/Containers/Shared/AppGroup/%s", safe);
    (void)mkdir(full, 0777);
    (void)chmod(full, 0777);
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)full, (CFIndex)strlen(full), true);
    return url ? ((id(*)(id, SEL))objc_msgSend)((id)url, sel_registerName("autorelease")) : nil;
}

static void zizhan_hap_impact(id self, SEL _cmd) {
    (void)zizhan_cam_ctl_write(0x102);
    if (gOrigImpactOccurred) ((void(*)(id, SEL))gOrigImpactOccurred)(self, _cmd);
}
static void zizhan_hap_impact_int(id self, SEL _cmd, double intensity) {
    (void)zizhan_cam_ctl_write(intensity >= 0.7 ? 0x103 : 0x101);
    if (gOrigImpactIntensity) ((void(*)(id, SEL, double))gOrigImpactIntensity)(self, _cmd, intensity);
}
static void zizhan_hap_selection(id self, SEL _cmd) {
    (void)zizhan_cam_ctl_write(0x104);
    if (gOrigSelectionChanged) ((void(*)(id, SEL))gOrigSelectionChanged)(self, _cmd);
}
static void zizhan_hap_notif(id self, SEL _cmd, long ntype) {
    int cmd = (ntype == 1) ? 0x106 : (ntype == 2 ? 0x107 : 0x105);
    (void)zizhan_cam_ctl_write(cmd);
    if (gOrigNotifOccurred) ((void(*)(id, SEL, long))gOrigNotifOccurred)(self, _cmd, ntype);
}

static BOOL zizhan_avas_set_mode(id self, SEL _cmd, id mode, id *err) {
    BOOL ok = gOrigAudioSetMode ? ((BOOL(*)(id, SEL, id, id *))gOrigAudioSetMode)(self, _cmd, mode, err) : YES;
    const char *m = mode ? ((const char *(*)(id, SEL))objc_msgSend)(mode, sel_registerName("UTF8String")) : NULL;
    const bool isVoice = (m && !strcmp(m, "AVAudioSessionModeVoiceChat"));
    gVoiceChatModeActive = isVoice;
    (void)zizhan_cam_ctl_write(isVoice ? ZIZHAN_CAM_CMD_AUDIO_RECEIVER : ZIZHAN_CAM_CMD_AUDIO_SPEAKER);
    return ok;
}

static BOOL zizhan_avas_override_port(id self, SEL _cmd, unsigned long port, id *err) {
    BOOL ok = gOrigAudioOverridePort ? ((BOOL(*)(id, SEL, unsigned long, id *))gOrigAudioOverridePort)(self, _cmd, port, err) : YES;
    const bool forceSpk = (port == 0x73706b72ul || port == 1936747378ul); /* 'spkr' */
    const bool wantRcv = (!forceSpk && gVoiceChatModeActive);
    (void)zizhan_cam_ctl_write(wantRcv ? ZIZHAN_CAM_CMD_AUDIO_RECEIVER : ZIZHAN_CAM_CMD_AUDIO_SPEAKER);
    return ok;
}

static void zizhan_app_win_layout(id self, SEL _cmd) {
    if (gOrigWinLayout) ((void(*)(id, SEL))gOrigWinLayout)(self, _cmd);
    if (boot_option("zizhan_outer_app_fit=0")) return;
    static uint64_t lastQueryNs = 0;
    static int cachedOuterView = 0;
    const uint64_t nowNs = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    if (nowNs - lastQueryNs >= 500000000ull) {
        lastQueryNs = nowNs;
        cachedOuterView = (zizhan_cam_ctl_query() >> 4) & 3;
    }
    static char kDidFitKey;
    const bool fit916 = (cachedOuterView == 3);
    const bool wasFit = objc_getAssociatedObject(self, &kDidFitKey) != nil;
    if (!fit916 && !wasFit) return;
    CGRect f = ((CGRect(*)(id, SEL))objc_msgSend)(self, sel_registerName("frame"));
    if (fit916 && f.size.width >= 1000.0 && f.size.height >= 900.0) {
        objc_setAssociatedObject(self, &kDidFitKey, (id)kCFBooleanTrue, OBJC_ASSOCIATION_ASSIGN);
        ((void(*)(id, SEL, CGRect))objc_msgSend)(self, sel_registerName("setFrame:"), CGRectMake(270.0, 0.0, 540.0, 958.0));
    } else if (!fit916 && wasFit) {
        objc_setAssociatedObject(self, &kDidFitKey, nil, OBJC_ASSOCIATION_ASSIGN);
        ((void(*)(id, SEL, CGRect))objc_msgSend)(self, sel_registerName("setFrame:"), CGRectMake(0.0, 0.0, 1080.0, 958.0));
    }
}

static void zizhan_rebind_image_got(const struct mach_header *header, intptr_t slide) {
    if (!header || header->magic != MH_MAGIC_64) return;
    Dl_info dli = {0};
    if (!dladdr(header, &dli) || !dli.dli_fname) return;
    if (strncmp(dli.dli_fname, "/private/var/containers/Bundle/Application/", 43) != 0 &&
        strncmp(dli.dli_fname, "/var/containers/Bundle/Application/", 35) != 0 &&
        strncmp(dli.dli_fname, "/Applications/Zizhan", 20) != 0) {
        return;
    }
    const struct mach_header_64 *h = (const struct mach_header_64 *)header;
    const struct segment_command_64 *linkedit = NULL;
    const struct symtab_command *symtab_cmd = NULL;
    const struct dysymtab_command *dysym_cmd = NULL;
    uintptr_t cur = (uintptr_t)(h + 1);
    for (uint32_t i = 0; i < h->ncmds; ++i) {
        const struct load_command *lc = (const struct load_command *)cur;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *)lc;
            if (!strcmp(seg->segname, "__LINKEDIT")) linkedit = seg;
        } else if (lc->cmd == LC_SYMTAB) {
            symtab_cmd = (const struct symtab_command *)lc;
        } else if (lc->cmd == LC_DYSYMTAB) {
            dysym_cmd = (const struct dysymtab_command *)lc;
        }
        cur += lc->cmdsize;
    }
    if (!linkedit || !symtab_cmd || !dysym_cmd || !dysym_cmd->nindirectsyms) return;
    const uintptr_t linkedit_base = (uintptr_t)slide + (uintptr_t)linkedit->vmaddr - (uintptr_t)linkedit->fileoff;
    const struct nlist_64 *symtab = (const struct nlist_64 *)(linkedit_base + symtab_cmd->symoff);
    const char *strtab = (const char *)(linkedit_base + symtab_cmd->stroff);
    const uint32_t *indirect = (const uint32_t *)(linkedit_base + dysym_cmd->indirectsymoff);

    cur = (uintptr_t)(h + 1);
    for (uint32_t i = 0; i < h->ncmds; ++i) {
        const struct load_command *lc = (const struct load_command *)cur;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *)lc;
            if (!strncmp(seg->segname, "__DATA", 6) || !strncmp(seg->segname, "__AUTH", 6)) {
                const struct section_64 *sects = (const struct section_64 *)(seg + 1);
                for (uint32_t s = 0; s < seg->nsects; ++s) {
                    const uint32_t stype = sects[s].flags & SECTION_TYPE;
                    if (stype != S_NON_LAZY_SYMBOL_POINTERS && stype != S_LAZY_SYMBOL_POINTERS) continue;
                    const uint32_t idx0 = sects[s].reserved1;
                    const size_t count = (size_t)(sects[s].size / sizeof(void *));
                    void **ptrs = (void **)((uintptr_t)slide + (uintptr_t)sects[s].addr);
                    for (size_t k = 0; k < count; ++k) {
                        if (idx0 + k >= dysym_cmd->nindirectsyms) break;
                        const uint32_t sidx = indirect[idx0 + k];
                        if ((sidx & (INDIRECT_SYMBOL_LOCAL | INDIRECT_SYMBOL_ABS)) || sidx >= symtab_cmd->nsyms) continue;
                        const uint32_t strx = symtab[sidx].n_un.n_strx;
                        if (strx >= symtab_cmd->strsize) continue;
                        const char *sname = strtab + strx;
                        void *repl = NULL;
                        if (!strcmp(sname, "_SecItemAdd")) repl = (void *)zizhan_hook_SecItemAdd;
                        else if (!strcmp(sname, "_SecItemCopyMatching")) repl = (void *)zizhan_hook_SecItemCopyMatching;
                        else if (!strcmp(sname, "_SecItemUpdate")) repl = (void *)zizhan_hook_SecItemUpdate;
                        else if (!strcmp(sname, "_SecItemDelete")) repl = (void *)zizhan_hook_SecItemDelete;
                        else if (!strcmp(sname, "_AudioServicesPlaySystemSound")) repl = (void *)zizhan_hook_AudioPlaySys;
                        else if (!strcmp(sname, "_AudioServicesPlayAlertSound")) repl = (void *)zizhan_hook_AudioPlayAlert;
                        if (repl) {
                            const vm_address_t pg = (vm_address_t)(&ptrs[k]) & ~((vm_address_t)vm_page_size - 1u);
                            if (vm_protect(mach_task_self(), pg, vm_page_size, FALSE,
                                           VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) == KERN_SUCCESS) {
                                ptrs[k] = repl;
                            }
                        }
                    }
                }
            }
        }
        cur += lc->cmdsize;
    }
}

static void zizhan_app_sandbox_compat_policy(void) {
    if (boot_option("zizhan_app_compat=0")) return;
    static dispatch_once_t symOnce;
    dispatch_once(&symOnce, ^{
        void *sec = dlopen("/System/Library/Frameworks/Security.framework/Security", RTLD_LAZY);
        if (sec) {
            gOrigSecItemAdd          = dlsym(sec, "SecItemAdd");
            gOrigSecItemCopyMatching = dlsym(sec, "SecItemCopyMatching");
            gOrigSecItemUpdate       = dlsym(sec, "SecItemUpdate");
            gOrigSecItemDelete       = dlsym(sec, "SecItemDelete");
        }
        void *atb = dlopen("/System/Library/Frameworks/AudioToolbox.framework/AudioToolbox", RTLD_LAZY);
        if (atb) {
            gOrigAudioPlaySys   = dlsym(atb, "AudioServicesPlaySystemSound");
            gOrigAudioPlayAlert = dlsym(atb, "AudioServicesPlayAlertSound");
        }
    });

    if (!gAppCompatObjcInstalled) {
        Class fmCls = objc_lookUpClass("NSFileManager");
        Class impCls = objc_lookUpClass("UIImpactFeedbackGenerator");
        Class selCls = objc_lookUpClass("UISelectionFeedbackGenerator");
        Class notCls = objc_lookUpClass("UINotificationFeedbackGenerator");
        Class winCls = objc_lookUpClass("UIWindow");
        Method m;
        if (fmCls && !gOrigAppGroupUrl && (m = class_getInstanceMethod(fmCls, sel_registerName("containerURLForSecurityApplicationGroupIdentifier:"))))
            gOrigAppGroupUrl = method_setImplementation(m, (IMP)zizhan_app_group_url);
        if (impCls && !gOrigImpactOccurred && (m = class_getInstanceMethod(impCls, sel_registerName("impactOccurred"))))
            gOrigImpactOccurred = method_setImplementation(m, (IMP)zizhan_hap_impact);
        if (impCls && !gOrigImpactIntensity && (m = class_getInstanceMethod(impCls, sel_registerName("impactOccurredWithIntensity:"))))
            gOrigImpactIntensity = method_setImplementation(m, (IMP)zizhan_hap_impact_int);
        if (selCls && !gOrigSelectionChanged && (m = class_getInstanceMethod(selCls, sel_registerName("selectionChanged"))))
            gOrigSelectionChanged = method_setImplementation(m, (IMP)zizhan_hap_selection);
        if (notCls && !gOrigNotifOccurred && (m = class_getInstanceMethod(notCls, sel_registerName("notificationOccurred:"))))
            gOrigNotifOccurred = method_setImplementation(m, (IMP)zizhan_hap_notif);
        if (winCls && !gOrigWinLayout && (m = class_getInstanceMethod(winCls, sel_registerName("layoutSubviews"))))
            gOrigWinLayout = method_setImplementation(m, (IMP)zizhan_app_win_layout);
        if (gOrigAppGroupUrl && gOrigImpactOccurred && gOrigWinLayout) gAppCompatObjcInstalled = true;
    }
    Class avasCls = objc_lookUpClass("AVAudioSession");
    if (avasCls && !gOrigAudioSetMode) {
        Method m1 = class_getInstanceMethod(avasCls, sel_registerName("setMode:error:"));
        Method m2 = class_getInstanceMethod(avasCls, sel_registerName("overrideOutputAudioPort:error:"));
        if (m1) gOrigAudioSetMode = method_setImplementation(m1, (IMP)zizhan_avas_set_mode);
        if (m2) gOrigAudioOverridePort = method_setImplementation(m2, (IMP)zizhan_avas_override_port);
    }
}

static void zizhan_app_sandbox_image(const struct mach_header *header, intptr_t slide) {
    zizhan_app_sandbox_compat_policy();
    zizhan_rebind_image_got(header, slide);
}

static bool is_core_daemon_or_shell(const char *name)
{
    static const char *const kCore[]={
        "SpringBoard","backboardd","Preferences","containermanagerd","containermanagerd_system",
        "usermanagerd","lsd","installd","pkd","runningboardd","PosterBoard","Photos","weatherd"
    };
    for(unsigned i=0;i<sizeof(kCore)/sizeof(kCore[0]);i++)
        if(!strcmp(name,kCore[i]))return true;
    return false;
}

__attribute__((constructor)) static void pong_ui18_start(void)
{
    const char *name=getprogname();
    if(!name)return;
    if(geteuid()==0 && !boot_option("zizhan_inject_apps=0")) {
        uint32_t one=1;
        (void)sysctlbyname("kern.zizhan_inject_apps",NULL,NULL,&one,sizeof(one));
    }
    if(!strcmp(name,"weatherd")) {
        if(boot_option("zizhan_net_diag=1")){app_network_start(name);app_weather_start();}
        return;
    }
    if(!is_core_daemon_or_shell(name)) {
        (void)dup2(2,1);setvbuf(stdout,NULL,_IONBF,0);setvbuf(stderr,NULL,_IONBF,0);
        zizhan_cllocation_policy();
        zizhan_avcapture_policy();
        zizhan_camera_app_bridge_init();
        zizhan_app_sandbox_compat_policy();
        for(uint32_t i=0,n=_dyld_image_count();i<n;i++)
            zizhan_rebind_image_got(_dyld_get_image_header(i),_dyld_get_image_vmaddr_slide(i));
        _dyld_register_func_for_add_image(zizhan_cllocation_image);
        _dyld_register_func_for_add_image(zizhan_avcapture_image);
        _dyld_register_func_for_add_image(zizhan_app_sandbox_image);
        if(boot_option("zizhan_app_diag=1"))app_diag_start(name);
        if(boot_option("zizhan_net_diag=1")) {
            app_network_start(name);
            if(!strcmp(name,"Weather"))app_weather_start();
        }
        return;
    }
    if(!strcmp(name,"SpringBoard") && boot_option("zizhan_app_diag=1"))app_diag_start(name);
    if(strcmp(name,"backboardd")==0 && boot_option("pong_backlight_diag=1")) {
        struct sigaction action={0};action.sa_sigaction=backboard_fatal;
        action.sa_flags=SA_SIGINFO|SA_RESETHAND;sigemptyset(&action.sa_mask);
        (void)sigaction(SIGSEGV,&action,NULL);(void)sigaction(SIGBUS,&action,NULL);(void)sigaction(SIGABRT,&action,NULL);
    }
    phone_ui_process=!strcmp(name,"Preferences") || !strcmp(name,"SpringBoard");
    photos_process=!strcmp(name,"Photos");
    if(photos_process) {
        fprintf(stderr,"Pong UI18: Photos adapter loaded\n");
        for(uint32_t i=0,n=_dyld_image_count();i<n;i++)
            photos_context_image(_dyld_get_image_header(i),_dyld_get_image_vmaddr_slide(i));
        _dyld_register_func_for_add_image(photos_context_image);
        return;
    }
    gestalt_process=phone_ui_process || !strcmp(name,"backboardd");
    // Install the capability answer before status-provider +initialize can
    // cache it during the regular all-images callback below.
    if(gestalt_process)for(uint32_t i=0,n=_dyld_image_count();i<n;i++)
        phone_gestalt_image(_dyld_get_image_header(i),_dyld_get_image_vmaddr_slide(i));
    if(!strcmp(name,"SpringBoard")) {
        lockdate_mode=boot_option("zizhan_lock_time=1")?1:(boot_option("zizhan_lock_time=2")?2:0);
        lockfix_enabled=!boot_option("zizhan_lock_fix=0");
        dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,10*NSEC_PER_SEC),dispatch_get_main_queue(),NULL,lockdate_install);
    }
    if(phone_ui_process) {
        dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,15*NSEC_PER_SEC),dispatch_get_main_queue(),NULL,phone_repair_existing);
        dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,35*NSEC_PER_SEC),dispatch_get_main_queue(),NULL,phone_repair_existing);
    }
    /* services42 enabled unsupported restore variants globally. Keep the
     * known phone catalog wrappers and leave native defaults unchanged. */
    wallpaper_paths_process=phone_ui_process || !strcmp(name,"PosterBoard") ||
        !strcmp(name,"CollectionsPoster") || !strcmp(name,"LegacyPoster") ||
        !strcmp(name,"WallpaperSettingsIntents");
    wallpaper_process=boot_option("pong_wallpaper_catalog_override=1") && wallpaper_paths_process;
    (void)dup2(2,1);setvbuf(stdout,NULL,_IONBF,0);setvbuf(stderr,NULL,_IONBF,0);
    fprintf(stderr,"Pong iPadOS18 adapter: %s (22H374)\n",name);
    if(strcmp(name,"Preferences")==0) {
        pong_settings18_start();
        if(boot_value("zizhan_accel_test=",(char[16]){0},16))
            dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,3*NSEC_PER_SEC),dispatch_get_main_queue(),NULL,orientation_watch);
    }
    if(strcmp(name,"Preferences")==0 && boot_option("pong_pane=DISPLAY")) {
        brightness_test_restore_only=boot_option("pong_brightness_restore_only=1");
        if(brightness_test_restore_only || boot_option("pong_brightness_ui_test=1"))
            dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,(brightness_test_restore_only?110:20)*NSEC_PER_SEC),
                dispatch_get_main_queue(),NULL,brightness_ui_test);
    }
    if(phone_ui_process && boot_option("touch_lat=1"))
        for(unsigned seconds=15;seconds<=45;seconds+=15)
            dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,(int64_t)seconds*NSEC_PER_SEC),dispatch_get_main_queue(),NULL,input_event_trace);
    // Keep SpringBoard/compositor adapters out of the poster renderer.
    // Only its native storage path and single-user identity are adapted.
    if(!strcmp(name,"PosterBoard")){
        poster_fatal_capture();_dyld_register_func_for_add_image(poster_image);
        if(boot_option("pong_poster_snapshot=1"))pong_boot_snapshot_start(name);
        return;
    }
    if(strcmp(name,"runningboardd")==0) {
        _dyld_register_func_for_add_image(poster_jetsam_image);
        if(boot_option("pong_boot_snapshot=1"))pong_boot_snapshot_start(name);
        return;
    }
    installd_process=strcmp(name,"installd")==0;
    pluginkit_process=strcmp(name,"pkd")==0;
    if(strcmp(name,"lsd")==0) {
        launchservices_process=true;
        struct sigaction action={0};
        action.sa_sigaction=lsd_abort;action.sa_flags=SA_SIGINFO|SA_RESETHAND;
        sigemptyset(&action.sa_mask);
        (void)sigaction(SIGABRT,&action,NULL);
    }
    if(strcmp(name,"usermanagerd")==0) {
        initialize_persona_manifest();return;
    }
    if(strcmp(name,"SpringBoard")==0) {
        springboard_process=true;
        display_link_rates();
        /* zizhan SENS-03 diagnostic: the device and interface orientation SpringBoard has, said when either changes */
        if(boot_value("zizhan_accel_test=",(char[16]){0},16))
            dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,8*NSEC_PER_SEC),dispatch_get_main_queue(),NULL,orientation_watch);
        char lock_buf[16];
        if(boot_value("zizhan_lock_orient=",lock_buf,sizeof(lock_buf))) {
            long lval=atol(lock_buf);
            if(lval>=1 && lval<=4)
                dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,12*NSEC_PER_SEC),dispatch_get_main_queue(),(void *)(intptr_t)lval,apply_boot_orientation_lock);
        }
        // The consumer reports are accepted but physical volume remains
        // unresponsive on this imported iPad board. Route those transitions
        // to the existing 22H374 controller after checking its actual ABI.
        // Power stays on HID; backboardd sends each volume transition once.
        if(!boot_option("pong_volume_direct=0"))
            dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,5*NSEC_PER_SEC),dispatch_get_main_queue(),NULL,volume18_prepare);
        if(boot_option("zizhan_audio_test=1")) {
            dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,12*NSEC_PER_SEC),
                dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),(void *)(uintptr_t)1u,audio18_test_session);
            dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,34*NSEC_PER_SEC),
                dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),(void *)(uintptr_t)2u,audio18_test_session);
        }
        if(boot_option("zizhan_audio_cap_test=1")) {
            dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,22*NSEC_PER_SEC),
                dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),(void *)(uintptr_t)1u,audio18_cap_test_session);
        }
        if(boot_option("zizhan_loc_probe=1")) {
            dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,16*NSEC_PER_SEC),
                dispatch_get_global_queue(QOS_CLASS_UTILITY,0),NULL,zizhan_loc_probe_tick);
        }
        skip_setup=boot_option("pong_skip_setup=1");
        /* zizhan: setup adaptation does not authorize forcing an unlock or
         * disabling automatic display sleep. Bring-up actions are opt-in. */
        if(zizhan_bringup_preferences())
            dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,10*NSEC_PER_SEC),dispatch_get_main_queue(),NULL,wake_home);
        char delay[16];
        if(boot_value("pong_url=",delay,sizeof(delay)) && atoi(delay)>0)
            dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,(int64_t)atoi(delay)*NSEC_PER_SEC),
                dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),NULL,open_pane);
        char openurl[192],openat[16]="15";
        if(boot_value("pong_open=",openurl,sizeof(openurl)) && openurl[0]) {
            (void)boot_value("pong_open_at=",openat,sizeof(openat));
            snprintf(pending_open_url,sizeof(pending_open_url),"%s",openurl);
            int at=atoi(openat);if(at<=0)at=15;
            dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,(int64_t)at*NSEC_PER_SEC),
                dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),NULL,open_pending_url);
        }
    }
    if(strcmp(name,"backboardd")==0) {
        if(!boot_option("zizhan_inject_apps=0")) {
            uint32_t one=1;
            (void)sysctlbyname("kern.zizhan_inject_apps",NULL,NULL,&one,sizeof(one));
        }
        software_composition=boot_option("pong_ui_software=1");
        scanout_format=true;
        backboard_container();
        pong_touch_start();
        if(boot_option("pong_surface_probe=1"))
            for(unsigned seconds=90;seconds<=130;seconds+=20)
                dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,(int64_t)seconds*NSEC_PER_SEC),
                    dispatch_get_main_queue(),NULL,late_surface_probe);
    }
    _dyld_register_func_for_add_image(compositor_image);
    if(boot_option("pong_boot_snapshot=1"))pong_boot_snapshot_start(name);
}
