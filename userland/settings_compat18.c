/* SPDX-License-Identifier: MIT
 * Preferences-only adaptations for the supplied 22H374 frameworks.
 * No 20G81 instruction addresses or private instance offsets are used.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <dlfcn.h>
#include <dispatch/dispatch.h>
#include <fcntl.h>
#include <libkern/OSCacheControl.h>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <math.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <unistd.h>

static bool icons_installed,radios_installed;
static IMP transcript_original;
static IMP localized_original;
static bool phone_identity;
static IMP asset_named_original;
static IMP asset_simple_original;
static _Thread_local bool asset_fallback;
static void (*previous_exception)(id);
static void record(const char *text)
{
    fprintf(stderr,"Pong Settings18: %s\n",text);
    int fd=open("/private/var/mobile/Library/Logs/Pong-Preferences18.log",
        O_WRONLY|O_APPEND|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0)return;
    struct stat st;
    if(fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size>4*1024*1024) {close(fd);return;}
    (void)write(fd,text,strnlen(text,16384));(void)write(fd,"\n",1);
    (void)fsync(fd);close(fd);
}
static bool abi(Method m,const char *ret,unsigned argc,const char *args)
{
    if(!m || method_getNumberOfArguments(m)!=argc+2)return false;
    char *s=method_copyReturnType(m);bool good=s && s[0] && strchr(ret,s[0]);free(s);
    for(unsigned i=0;good && i<argc;i++) {
        s=method_copyArgumentType(m,i+2);good=s && s[0]==args[i];free(s);
    }
    return good;
}
static void hook(const char *cls,const char *name,const char *ret,unsigned argc,const char *args,IMP imp)
{
    Class c=objc_lookUpClass(cls);Method m=c?class_getInstanceMethod(c,sel_registerName(name)):NULL;
    if(abi(m,ret,argc,args) && method_getImplementation(m)!=imp)method_setImplementation(m,imp);
}
static id unavailable(id self,SEL selector)
{(void)self;(void)selector;return nil;}
static void no_query(id self,SEL selector)
{(void)self;(void)selector;}
static BOOL no_sim(id self,SEL selector,id specifier)
{(void)self;(void)selector;(void)specifier;return NO;}
static id ios_label(id value)
{
    if(phone_identity && value && CFGetTypeID((CFTypeRef)value)==CFStringGetTypeID() &&
       CFStringFind((CFStringRef)value,CFSTR("iPadOS"),0).location!=kCFNotFound)
        return ((id(*)(id,SEL,id,id))objc_msgSend)(value,
            sel_registerName("stringByReplacingOccurrencesOfString:withString:"),(id)CFSTR("iPadOS"),(id)CFSTR("iOS"));
    return value;
}
/* softwareVersionString supplies the row title, not the version value.
 * Leave _productVersionStringFallback native so the right-hand 22H374 value
 * continues to come from this restore. */
static id local_version(id self,SEL selector)
{(void)self;(void)selector;return (id)CFSTR("iOS版本");}
static id localized_phone_label(id self,SEL selector,id key,id value,id table)
{return ios_label(((id(*)(id,SEL,id,id,id))localized_original)(self,selector,key,value,table));}
static id phone_model(id self,SEL selector)
{(void)self;(void)selector;return (id)CFSTR("iPhone 16 Pro");}
static id phone_system(id self,SEL selector)
{(void)self;(void)selector;return (id)CFSTR("iOS");}

/* The restore is still J171/iPad7,11. Phone presentation must not make its
 * stock asset catalogs invisible: retry a missing stock UIImage rendition
 * with this restore's pad idiom, retaining the native image loader/renderer.
 * This changes only a failed resource lookup in Preferences, not screen
 * identity, icon contents, the global trait collection or GPU selection. */
static id pad_asset(id cls,SEL selector,id name,id bundle,id traits)
{
    if(!phone_identity || asset_fallback || !name || !asset_named_original)return nil;
    Class bundles=objc_lookUpClass("NSBundle");
    id resolved=bundle?bundle:((id(*)(id,SEL))objc_msgSend)((id)bundles,sel_registerName("mainBundle"));
    id path=((id(*)(id,SEL))objc_msgSend)(resolved,sel_registerName("bundlePath"));
    const char *str=path?((const char *(*)(id,SEL))objc_msgSend)(path,sel_registerName("fileSystemRepresentation")):NULL;
    if(!str || (strncmp(str,"/System/Library/",16) && strncmp(str,"/Applications/Preferences.app",29)))return nil;
    Class tc=objc_lookUpClass("UITraitCollection");
    if(!tc)return nil;
    id pad=((id(*)(id,SEL,long))objc_msgSend)((id)tc,sel_registerName("traitCollectionWithUserInterfaceIdiom:"),1);
    if(!pad)return nil;
    id source[2]={traits,pad};
    CFArrayRef list=CFArrayCreate(NULL,(const void **)(traits?source:source+1),traits?2:1,&kCFTypeArrayCallBacks);
    if(!list)return nil;
    id actual=((id(*)(id,SEL,id))objc_msgSend)((id)tc,sel_registerName("traitCollectionWithTraitsFromCollections:"),(id)list);
    asset_fallback=true;
    id image=((id(*)(id,SEL,id,id,id))asset_named_original)(cls,selector,name,resolved,actual);
    asset_fallback=false;CFRelease(list);
    if(image) {
        static unsigned reported;
        if(__atomic_fetch_add(&reported,1,__ATOMIC_RELAXED)<6)record("native restore asset found with pad rendition fallback");
    }
    return image;
}
static id named_asset(id self,SEL selector,id name,id bundle,id traits)
{
    id image=((id(*)(id,SEL,id,id,id))asset_named_original)(self,selector,name,bundle,traits);
    return image?image:pad_asset(self,selector,name,bundle,traits);
}
static id simple_asset(id self,SEL selector,id name)
{
    id image=((id(*)(id,SEL,id))asset_simple_original)(self,selector,name);
    return image?image:pad_asset(self,sel_registerName("imageNamed:inBundle:compatibleWithTraitCollection:"),name,nil,nil);
}

/* The captured SIGABRT is optional AppIntents history serialization, not
 * the About table itself. A missing action archive cannot form a valid
 * LNTranscriptActionRecord. Fail that initializer normally; never invent
 * archives, suppress Foundation exceptions, or claim the action ran.
 * actionData, resolvedActionData and predictionsData are required by this
 * initializer's three null checks; keep native handling of complete records. */
static id transcript_init(id self,SEL selector,id bundle,unsigned short source,id client,
    id uuid,id date,id action,id resolved,id output,id predictions,BOOL has_next)
{
    if(!action || !resolved || !predictions) {
        record(!action?"optional intent history has no action archive; record omitted":
            !resolved?"optional intent history has no resolved action archive; record omitted":
            "optional intent history has no predictions archive; record omitted");
        ((void(*)(id,SEL))objc_msgSend)(self,sel_registerName("release"));
        return nil;
    }
    return ((id(*)(id,SEL,id,unsigned short,id,id,id,id,id,id,id,BOOL))transcript_original)
        (self,selector,bundle,source,client,uuid,date,action,resolved,output,predictions,has_next);
}
static void methods(void)
{
    if(phone_identity) {
        Class image=objc_lookUpClass("UIImage");
        Method full=image?class_getClassMethod(image,sel_registerName("imageNamed:inBundle:compatibleWithTraitCollection:")):NULL;
        if(!asset_named_original && abi(full,"@",3,"@@@")) {
            asset_named_original=method_setImplementation(full,(IMP)named_asset);
            Method simple=class_getClassMethod(image,sel_registerName("imageNamed:"));
            if(abi(simple,"@",1,"@"))asset_simple_original=method_setImplementation(simple,(IMP)simple_asset);
            record("native restore image rendition fallback installed");
        }
        // About's marketing row and UIKit branding use the selected identity;
        // version/build, user-assigned name and regulatory values stay native.
        hook("PSGAboutDataSource","_modelNameString","@",0,"",(IMP)phone_model);
        hook("UIDevice","systemName","@",0,"",(IMP)phone_system);
        if(!localized_original) {
            Class cls=objc_lookUpClass("NSBundle");
            Method m=cls?class_getInstanceMethod(cls,sel_registerName("localizedStringForKey:value:table:")):NULL;
            if(abi(m,"@",3,"@@@"))localized_original=method_setImplementation(m,(IMP)localized_phone_label);
        }
    }
    hook("PSGAboutDataSource","_getBootStrapIccid","v",0,"",(IMP)no_query);
    hook("PSGAboutDataSource","shouldShowSIMSpecifier:","B",1,"@",(IMP)no_sim);
    for(unsigned i=0;i<3;i++) {
        const char *names[]={"_macAddress","_bluetoothMACAddress","_myNumber"};
        hook("PSGAboutDataSource",names[i],"@",0,"",(IMP)unavailable);
    }
    hook("PSGAboutDataSource","softwareVersionString","@",0,"",(IMP)local_version);
    if(!transcript_original) {
        Class c=objc_lookUpClass("LNTranscriptActionRecord");
        SEL sel=sel_registerName("initWithBundleIdentifier:source:clientLabel:executionUUID:executionDate:actionData:resolvedActionData:actionOutputData:predictionsData:hasNextAction:");
        Method m=c?class_getInstanceMethod(c,sel):NULL;
        if(abi(m,"@",10,"@S@@@@@@@B")) {
            transcript_original=method_setImplementation(m,(IMP)transcript_init);
            record("22H374 optional intent history initializer adapted");
        }
    }
}
static bool image(const struct mach_header *head,intptr_t slide,const uint8_t uuid[16],uintptr_t target,size_t bytes)
{
    if(head->magic!=MH_MAGIC_64)return false;
    const struct mach_header_64 *h=(const void *)head;
    if(h->ncmds>1024 || h->sizeofcmds>1024*1024)return false;
    const char *p=(const void *)(h+1),*end=p+h->sizeofcmds;
    bool matched=false,in_code=false;
    for(unsigned i=0;i<h->ncmds;i++) {
        if((size_t)(end-p)<sizeof(struct load_command))return false;
        const struct load_command *lc=(const void *)p;
        if(lc->cmdsize<sizeof(*lc) || lc->cmdsize>(size_t)(end-p))return false;
        if(lc->cmd==LC_UUID && lc->cmdsize==sizeof(struct uuid_command))
            matched=memcmp(((const struct uuid_command *)lc)->uuid,uuid,16)==0;
        if(lc->cmd==LC_SEGMENT_64 && lc->cmdsize>=sizeof(struct segment_command_64)) {
            const struct segment_command_64 *s=(const void *)lc;
            uintptr_t start;
            if(slide>=0) {
                if(s->vmaddr>UINTPTR_MAX-(uintptr_t)slide)return false;
                start=(uintptr_t)s->vmaddr+(uintptr_t)slide;
            } else {
                uintptr_t delta=0-(uintptr_t)slide;
                if(s->vmaddr<delta)return false;
                start=(uintptr_t)s->vmaddr-delta;
            }
            if((s->initprot&VM_PROT_EXECUTE) && s->vmsize<=UINTPTR_MAX-start &&
               target>=start && target-start<=s->vmsize && bytes<=s->vmsize-(target-start))in_code=true;
        }
        p+=lc->cmdsize;
    }
    return matched && in_code;
}
static bool patch(uintptr_t target,const void *old,const void *replacement,size_t bytes)
{
    if(!bytes || bytes>vm_page_size || memcmp((const void *)target,old,bytes))return false;
    // The caller already matched the image UUID, executable segment and
    // original instructions. Use the same COW path as the working 22H374
    // compositor adapter, without a second legacy vm_region_64 prerequisite.
    vm_address_t page=target&~((vm_address_t)vm_page_size-1);
    if(target-page>vm_page_size-bytes)return false;
    kern_return_t kr=vm_protect(mach_task_self(),page,vm_page_size,FALSE,VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY);
    if(kr){char message[96];snprintf(message,sizeof(message),"22H374 code COW failed kr=%d",kr);record(message);return false;}
    memcpy((void *)target,replacement,bytes);sys_icache_invalidate((void *)target,bytes);
    kr=vm_protect(mach_task_self(),page,vm_page_size,FALSE,VM_PROT_READ|VM_PROT_EXECUTE);
    if(!kr)return true;
    memcpy((void *)target,old,bytes);sys_icache_invalidate((void *)target,bytes);
    (void)vm_protect(mach_task_self(),page,vm_page_size,FALSE,VM_PROT_READ|VM_PROT_EXECUTE);
    return false;
}
/* MobileIcons still enters an EAGL Lanczos helper on this restore. Use the
 * actual Metal CIContext and image transform, with the same CGImage/scale ABI.
 * The original AIR compiler/Turnip path performs the image rendering. */
static id icon_metal_context(void)
{
    static dispatch_once_t once;static id context;
    dispatch_once(&once, ^{
        void *metal=dlopen("/System/Library/Frameworks/Metal.framework/Metal",RTLD_NOW|RTLD_LOCAL);
        id (*create)(void)=metal?dlsym(metal,"MTLCreateSystemDefaultDevice"):NULL;
        id device=create?create():nil;
        Class cls=objc_lookUpClass("CIContext");
        Method m=cls?class_getClassMethod(cls,sel_registerName("contextWithMTLDevice:options:")):NULL;
        if(device && abi(m,"@",2,"@@")) {
            CFMutableDictionaryRef opts=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
            CFStringRef *software=dlsym(RTLD_DEFAULT,"kCIContextUseSoftwareRenderer");
            CFStringRef *cache=dlsym(RTLD_DEFAULT,"kCIContextCacheIntermediates");
            if(opts && software && *software)CFDictionarySetValue(opts,*software,kCFBooleanFalse);
            if(opts && cache && *cache)CFDictionarySetValue(opts,*cache,kCFBooleanFalse);
            context=((id(*)(id,SEL,id,id))objc_msgSend)((id)cls,sel_registerName("contextWithMTLDevice:options:"),device,(id)opts);
            if(context)context=((id(*)(id,SEL))objc_msgSend)(context,sel_registerName("retain"));
            if(opts)CFRelease(opts);
        }
        if(device)((void(*)(id,SEL))objc_msgSend)(device,sel_registerName("release"));
        record(context?"native Metal CoreImage icon context created":"native Metal icon context unavailable");
    });
    return context;
}
static CGImageRef scale_icon(CGImageRef input,double scale)
{
    if(!input || !isfinite(scale) || scale<=0)return NULL;
    const size_t iw=CGImageGetWidth(input),ih=CGImageGetHeight(input);
    const double dw=iw*scale,dh=ih*scale;
    if(!iw || !ih || !isfinite(dw) || !isfinite(dh) || dw<0.5 || dh<0.5 || dw>4096 || dh>4096)return NULL;
    size_t w=(size_t)floor(dw+0.5),h=(size_t)floor(dh+0.5);
    if(w==iw && h==ih)return CGImageRetain(input);
    id context=icon_metal_context();if(!context)return NULL;
    id image=((id(*)(id,SEL,CGImageRef))objc_msgSend)((id)objc_lookUpClass("CIImage"),sel_registerName("imageWithCGImage:"),input);
    if(!image)return NULL;
    CGAffineTransform transform=CGAffineTransformMakeScale((double)w/iw,(double)h/ih);
    image=((id(*)(id,SEL,CGAffineTransform))objc_msgSend)(image,sel_registerName("imageByApplyingTransform:"),transform);
    CGImageRef result=((CGImageRef(*)(id,SEL,id,CGRect))objc_msgSend)(context,sel_registerName("createCGImage:fromRect:"),image,CGRectMake(0,0,w,h));
    static unsigned reported;
    if(__atomic_fetch_add(&reported,1,__ATOMIC_RELAXED)<4) {
        char text[144];snprintf(text,sizeof(text),"Metal icon bitmap %zux%zu -> %zux%zu result=%s",iw,ih,w,h,result?"image":"unavailable");record(text);
    }
    return result;
}
static void added(const struct mach_header *head,intptr_t slide)
{
    methods();
    static const uint8_t mobile_uuid[]={0x8f,0xeb,0x56,0xd8,0x54,0x30,0x3e,0x27,0x88,0xe4,0x13,0x7f,0x30,0x53,0x42,0xa6};
    static const uint8_t about_uuid[]={0x24,0x82,0x62,0xd0,0x70,0xed,0x3e,0xd5,0xbf,0x6f,0x4c,0x4c,0x8f,0x8d,0xec,0xfb};
    uintptr_t entry=(uintptr_t)head+0x77bc;
    if(!icons_installed && image(head,slide,mobile_uuid,entry,16)) {
        const uint32_t old[]={0xd10283ff,0x6d0323e9,0xa9046ffc,0xa90567fa};
        const struct {uint32_t load,branch;uintptr_t target;} jump={0x58000050,0xd61f0200,(uintptr_t)scale_icon};
        _Static_assert(sizeof(jump)==16,"AArch64 literal tail branch");
        icons_installed=patch(entry,old,&jump,sizeof(jump));
        if(icons_installed)record("22H374 MobileIcons Metal image scaler installed");
    }
    entry=(uintptr_t)head+0xa868;
    if(!radios_installed && image(head,slide,about_uuid,entry,36)) {
        const uint32_t old[]={0xaa0003fa,0x90121780,0x91318000,0x94b68fc4,0xaa0003f9,
            0x90121780,0x91320000,0x94b68fc0,0xaa0003fb};
        uint32_t next[9];memcpy(next,old,sizeof(next));
        next[0]=0xaa1f03fa;next[4]=0xaa1f03f9;next[8]=0xaa1f03fb;
        radios_installed=patch(entry,old,next,sizeof(next));
        if(radios_installed)record("22H374 About omits unavailable radio sections; identity retained");
    }
}
static void exception(id object)
{
    const char *selectors[]={"name","reason","callStackSymbols"};
    for(unsigned i=0;i<3;i++) {
        id value=((id(*)(id,SEL))objc_msgSend)(object,sel_registerName(selectors[i]));
        if(i==2)value=((id(*)(id,SEL))objc_msgSend)(value,sel_registerName("description"));
        const char *text=value?((const char *(*)(id,SEL))objc_msgSend)(value,sel_registerName("UTF8String")):NULL;
        if(text)record(text);
    }
    if(previous_exception)previous_exception(object);
}
static void load_icons(void *unused)
{
    (void)unused;
    // Load the real image on the main queue, after dyld's constructor phase.
    // New Settings paths may request icons remotely before MobileIcons is a
    // local dependency. Keep its handle for the life of Preferences.
    if(!dlopen("/System/Library/PrivateFrameworks/MobileIcons.framework/MobileIcons",RTLD_LAZY|RTLD_LOCAL))
        record("MobileIcons framework unavailable");
    // Retry these two qualified images once after loader initialization.
    // Successful installs are idempotent and all UUID/entry guards remain.
    for(uint32_t i=0;i<_dyld_image_count();i++) {
        const char *path=_dyld_get_image_name(i);
        if(path && (strstr(path,"/MobileIcons.framework/") || strstr(path,"/GeneralSettingsUI.framework/")))
            added(_dyld_get_image_header(i),_dyld_get_image_vmaddr_slide(i));
    }
}
/* Unattended rounds that open a pane (boot-arg pong_url, ui_compat18.c open_pane) print what Settings shows, so a round can be judged
 * without a person: "Pong Settings18 ui <n>: <depth> <class> [text] [spinning]" for every visible label, button, text, spinner and
 * table (row count). 11-01: Settings > Battery Health kept spinning; the kernel screenshots only show the left of the canvas. */
static unsigned ui_lines;
static const char *ui_text(id object,const char *selector)
{
    SEL sel=sel_registerName(selector);
    if(!((BOOL(*)(id,SEL,SEL))objc_msgSend)(object,sel_registerName("respondsToSelector:"),sel))return NULL;
    id text=((id(*)(id,SEL))objc_msgSend)(object,sel);
    if(!text || !((BOOL(*)(id,SEL,Class))objc_msgSend)(text,sel_registerName("isKindOfClass:"),objc_lookUpClass("NSString")))return NULL;
    return ((const char *(*)(id,SEL))objc_msgSend)(text,sel_registerName("UTF8String"));
}
/* The console path drops the top bit of every byte: non-ASCII UTF-8 goes out as \xHH (tools/zizhan/mac/settings-ui.py decodes it). */
static const char *ui_escape(const char *text,char *out,size_t size)
{
    size_t k=0;
    for(const unsigned char *p=(const unsigned char *)text;*p && k+5<size;p++) {
        if(*p>=0x20 && *p<0x7f && *p!='\\')out[k++]=(char)*p;
        else k+=(size_t)snprintf(out+k,size-k,"\\x%02x",*p);
    }
    out[k]='\0';return out;
}
/* SwiftUI content (the iOS 18 Battery Health pane) has no UILabel/UIActivityIndicatorView: its accessibility elements carry the text,
 * so every element with a label or value is printed with its traits (raw, to tell a ProgressView from text). */
static void ui_accessibility(id element,unsigned depth,unsigned pass)
{
    if(!element || depth>30 || ui_lines>400)return;
    char a[384],b[384];
    const char *label=ui_text(element,"accessibilityLabel"),*value=ui_text(element,"accessibilityValue");
    SEL traits=sel_registerName("accessibilityTraits");
    const unsigned long long t=((BOOL(*)(id,SEL,SEL))objc_msgSend)(element,sel_registerName("respondsToSelector:"),traits)?
        ((unsigned long long(*)(id,SEL))objc_msgSend)(element,traits):0;
    if((label && *label) || (value && *value)) {
        fprintf(stderr,"Pong Settings18 ui %u: %u ax %s \"%s\" value \"%s\" traits 0x%llx\n",pass,depth,object_getClassName(element),
            ui_escape(label?label:"",a,sizeof(a)),ui_escape(value?value:"",b,sizeof(b)),t);
        ++ui_lines;
    }
    SEL elements=sel_registerName("accessibilityElements");
    if(!((BOOL(*)(id,SEL,SEL))objc_msgSend)(element,sel_registerName("respondsToSelector:"),elements))return;
    id children=((id(*)(id,SEL))objc_msgSend)(element,elements);
    if(!children || !((BOOL(*)(id,SEL,Class))objc_msgSend)(children,sel_registerName("isKindOfClass:"),objc_lookUpClass("NSArray")))return;
    const unsigned long count=((unsigned long(*)(id,SEL))objc_msgSend)(children,sel_registerName("count"));
    for(unsigned long i=0;i<count && i<200;i++)
        ui_accessibility(((id(*)(id,SEL,unsigned long))objc_msgSend)(children,sel_registerName("objectAtIndex:"),i),depth+1,pass);
}
/* pong_pane=H: iOS 18 ignores the URL's path=BATTERY_HEALTH, so the second dump (the first still shows Settings' root) selects the
 * Battery pane's health row itself (the cell whose specifier identifier or title names the health page), the way a tap would. */
static id health_cell;
static bool want_health;
static void ui_walk(id view,unsigned depth,unsigned pass)
{
    if(!view || depth>40 || ui_lines>400)return;
    if(((BOOL(*)(id,SEL))objc_msgSend)(view,sel_registerName("isHidden")))return;
    const char *name=object_getClassName(view);
    Class label=objc_lookUpClass("UILabel"),spinner=objc_lookUpClass("UIActivityIndicatorView"),table=objc_lookUpClass("UITableView"),
          collection=objc_lookUpClass("UICollectionView"),field=objc_lookUpClass("UITextView");
    BOOL (*is)(id,SEL,Class)=(BOOL(*)(id,SEL,Class))objc_msgSend;SEL kind=sel_registerName("isKindOfClass:");
    char line[512],escaped[400];line[0]='\0';
    if(strstr(name,"Hosting"))ui_accessibility(view,depth,pass);
    if(label && is(view,kind,label)) {
        const char *text=ui_text(view,"text");
        if(text && *text)snprintf(line,sizeof(line),"\"%s\"",ui_escape(text,escaped,sizeof(escaped)));
    } else if(field && is(view,kind,field)) {
        const char *text=ui_text(view,"text");
        if(text && *text)snprintf(line,sizeof(line),"\"%s\"",ui_escape(text,escaped,sizeof(escaped)));
    } else if(spinner && is(view,kind,spinner)) {
        snprintf(line,sizeof(line),"spinner %s",((BOOL(*)(id,SEL))objc_msgSend)(view,sel_registerName("isAnimating"))?"SPINNING":"stopped");
    } else if((table && is(view,kind,table)) || (collection && is(view,kind,collection))) {
        id cells=((id(*)(id,SEL))objc_msgSend)(view,sel_registerName("visibleCells"));
        snprintf(line,sizeof(line),"%lu visible cells",cells?(unsigned long)((unsigned long(*)(id,SEL))objc_msgSend)(cells,sel_registerName("count")):0ul);
    } else if(objc_lookUpClass("UITableViewCell") && is(view,kind,objc_lookUpClass("UITableViewCell"))) {
        id specifier=((BOOL(*)(id,SEL,SEL))objc_msgSend)(view,sel_registerName("respondsToSelector:"),sel_registerName("specifier"))?
            ((id(*)(id,SEL))objc_msgSend)(view,sel_registerName("specifier")):nil;
        const char *identifier=specifier?ui_text(specifier,"identifier"):NULL,*title=specifier?ui_text(specifier,"name"):NULL;
        if(identifier)snprintf(line,sizeof(line),"cell id \"%s\"",ui_escape(identifier,escaped,sizeof(escaped)));
        if(want_health && pass==2 && !health_cell &&
           ((identifier && (strstr(identifier,"HEALTH") || strstr(identifier,"Health"))) ||
            (title && (strstr(title,"\xe5\x81\xa5\xe5\xba\xb7") || strstr(title,"Health")))))   /* 健康 */
            health_cell=view;
    } else if(strstr(name,"Chart") || strstr(name,"Graph")) {
        CGRect frame=((CGRect(*)(id,SEL))objc_msgSend)(view,sel_registerName("frame"));
        snprintf(line,sizeof(line),"%.0fx%.0f",frame.size.width,frame.size.height);
    }
    if(line[0]) {
        fprintf(stderr,"Pong Settings18 ui %u: %u %s %s\n",pass,depth,name,line);
        ++ui_lines;
    }
    id children=((id(*)(id,SEL))objc_msgSend)(view,sel_registerName("subviews"));
    const unsigned long count=children?((unsigned long(*)(id,SEL))objc_msgSend)(children,sel_registerName("count")):0;
    for(unsigned long i=0;i<count;i++)
        ui_walk(((id(*)(id,SEL,unsigned long))objc_msgSend)(children,sel_registerName("objectAtIndex:"),i),depth+1,pass);
}
/* Battery Health shows its spinner while +[BatteryUIResourceClass batteryDataUnavailable] is true (BatteryUsageUI, 22H374); print
 * each input it checks, from the class itself, so a round shows which one holds it (11-01 g231). */
static void ui_battery_inputs(unsigned pass)
{
    Class c=objc_lookUpClass("BatteryUIResourceClass");
    if(!c)return;
    const char *ints[]={"getManagementState","getBatteryHealthServiceState","getBatteryHealthServiceFlags","isQMaxUnknown",
                        "genuineBatteryStatus","batteryDataUnavailable","deviceSupportsCPMS"};
    char out[400];size_t k=0;
    for(unsigned i=0;i<sizeof(ints)/sizeof(ints[0]);i++) {
        SEL sel=sel_registerName(ints[i]);
        if(!class_getClassMethod(c,sel))continue;
        const bool boolean=!strncmp(ints[i],"is",2) || !strncmp(ints[i],"device",6) || !strncmp(ints[i],"battery",7);
        const long value=boolean?(long)((BOOL(*)(id,SEL))objc_msgSend)((id)c,sel):(long)((int(*)(id,SEL))objc_msgSend)((id)c,sel);
        k+=(size_t)snprintf(out+k,sizeof(out)-k,"%s=%ld ",ints[i],value);
        if(k>=sizeof(out))break;
    }
    SEL percent=sel_registerName("getBatteryMaximumCapacityPercentWithError:");
    if(class_getClassMethod(c,percent)) {
        id error=nil;
        const long value=(long)((int(*)(id,SEL,id *))objc_msgSend)((id)c,percent,&error);
        const char *text=error?ui_text(error,"description"):NULL;char e[200];
        fprintf(stderr,"Pong Settings18 ui %u: battery %smaxCapacityPercent=%ld error \"%s\"\n",pass,out,value,
            ui_escape(text?text:"",e,sizeof(e)));
    }
}
static void ui_dump(void *context)
{
    const unsigned pass=(unsigned)(uintptr_t)context;
    if(pass>=3)ui_battery_inputs(pass);
    id app=((id(*)(id,SEL))objc_msgSend)((id)objc_lookUpClass("UIApplication"),sel_registerName("sharedApplication"));
    id windows=app?((id(*)(id,SEL))objc_msgSend)(app,sel_registerName("windows")):nil;
    const unsigned long count=windows?((unsigned long(*)(id,SEL))objc_msgSend)(windows,sel_registerName("count")):0;
    fprintf(stderr,"Pong Settings18 ui %u: begin, %lu windows\n",pass,(unsigned long)count);
    ui_lines=0;
    for(unsigned long i=0;i<count;i++)ui_walk(((id(*)(id,SEL,unsigned long))objc_msgSend)(windows,sel_registerName("objectAtIndex:"),i),0,pass);
    fprintf(stderr,"Pong Settings18 ui %u: end, %u lines\n",pass,ui_lines);
    if(!health_cell || pass!=2)return;
    id table=((id(*)(id,SEL))objc_msgSend)(health_cell,sel_registerName("superview"));
    Class tables=objc_lookUpClass("UITableView");
    while(table && !((BOOL(*)(id,SEL,Class))objc_msgSend)(table,sel_registerName("isKindOfClass:"),tables))
        table=((id(*)(id,SEL))objc_msgSend)(table,sel_registerName("superview"));
    id path=table?((id(*)(id,SEL,id))objc_msgSend)(table,sel_registerName("indexPathForCell:"),health_cell):nil;
    id delegate=table?((id(*)(id,SEL))objc_msgSend)(table,sel_registerName("delegate")):nil;
    SEL select=sel_registerName("tableView:didSelectRowAtIndexPath:");
    const bool can=path && delegate && ((BOOL(*)(id,SEL,SEL))objc_msgSend)(delegate,sel_registerName("respondsToSelector:"),select);
    if(can)((void(*)(id,SEL,id,id))objc_msgSend)(delegate,select,table,path);
    fprintf(stderr,"Pong Settings18 ui %u: selected the health row %d\n",pass,can);
}
static bool opened_by_round(void)
{
    char args[2048];size_t size=sizeof(args)-1;
    if(sysctlbyname("kern.bootargs",args,&size,NULL,0))return false;
    args[size<sizeof(args)?size:sizeof(args)-1]='\0';
    want_health=strstr(args," pong_pane=H")!=NULL;
    return strstr(args," pong_url=")!=NULL;
}
void pong_settings18_start(void)
{
    if(opened_by_round()) {
        static const unsigned at[]={6,12,20,30};
        for(unsigned i=0;i<4;i++)dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,(int64_t)at[i]*NSEC_PER_SEC),dispatch_get_main_queue(),
            (void *)(uintptr_t)(i+1),ui_dump);
    }
    char product[64]={0};size_t size=sizeof(product);
    phone_identity=sysctlbyname("hw.product",product,&size,NULL,0)==0 && !strcmp(product,"iPhone17,1");
    void (*set)(void(*)(id))=dlsym(RTLD_DEFAULT,"NSSetUncaughtExceptionHandler");
    void (*(*get)(void))(id)=dlsym(RTLD_DEFAULT,"NSGetUncaughtExceptionHandler");
    if(set && get){previous_exception=get();set(exception);}
    _dyld_register_func_for_add_image(added);
    dispatch_async_f(dispatch_get_main_queue(),NULL,load_icons);
}
