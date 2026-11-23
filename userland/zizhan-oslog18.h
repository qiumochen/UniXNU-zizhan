/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * zizhan diagnostic: os_log lines of the processes this library is loaded into, on stderr (the console, with applog_name).
 * Boot-arg zizhan_oslog=<word>[,<word>...]: a message whose format string contains one of the words (at most four, case
 * sensitive) is printed with its arguments; while the option is set every log level counts as enabled in these processes, so
 * debug messages are composed too. Without the option nothing is patched.
 *
 * The unified log keeps arguments in a buffer next to the format (libtrace: a summary byte, the argument count, then per
 * argument a type/flags byte, a size byte and the bytes; strings and objects are still pointers when _os_log_impl is called).
 */
#ifndef ZIZHAN_OSLOG18_H
#define ZIZHAN_OSLOG18_H
#include <CoreFoundation/CoreFoundation.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include <dlfcn.h>
#include <libkern/OSCacheControl.h>
#include <mach/mach.h>


static char zizhan_oslog_words[4][24];
static int zizhan_oslog_state;          /* 0 not read yet, 1 off, 2 on */

static bool
zizhan_oslog_on(void)
{
    int state=__atomic_load_n(&zizhan_oslog_state,__ATOMIC_ACQUIRE);
    if (state) return state==2;
    static char args[2048];
    char words[4][24]={{0}};
    size_t bytes=sizeof(args)-1;
    state=1;
    if (!sysctlbyname("kern.zizhan_bootargs",args,&bytes,NULL,0) || ((bytes=sizeof(args)-1),!sysctlbyname("kern.bootargs",args,&bytes,NULL,0))) {
        args[sizeof(args)-1]=0;
        const char *option=strstr(args,"zizhan_oslog=");
        if (option && (option==args || option[-1]==' ')) {
            option+=13;
            for (unsigned word=0;word<4 && *option && *option!=' ';word++) {
                unsigned n=0;
                while (*option && *option!=' ' && *option!=',' && n<sizeof(words[0])-1) words[word][n++]=*option++;
                while (*option && *option!=' ' && *option!=',') option++;
                if (*option==',') option++;
                if (n) state=2;
            }
        }
    }
    /* several threads may get here at once: they all write the same words, then the same state */
    memcpy(zizhan_oslog_words,words,sizeof(words));
    __atomic_store_n(&zizhan_oslog_state,state,__ATOMIC_RELEASE);
    return state==2;
}

static void
zizhan_oslog_print(const char *format,const uint8_t *buffer,uint32_t size)
{
    char line[600];
    size_t out=0;
    uint32_t at=2;
    unsigned left=size>=2?buffer[1]:0;
#define PUT(...) do { if (out<sizeof(line)-1) { int n_=snprintf(line+out,sizeof(line)-out,__VA_ARGS__); if (n_>0) out+=(size_t)n_; if (out>sizeof(line)-1) out=sizeof(line)-1; } } while (0)
    for (const char *p=format;*p && out<sizeof(line)-1;) {
        if (*p!='%') {line[out++]=*p++;continue;}
        p++;
        if (*p=='%') {line[out++]=*p++;continue;}
        if (*p=='{') {while (*p && *p!='}') p++;if (*p) p++;}
        int precision=-1;
        while (*p && strchr("-+ #0123456789.*",*p)) {
            if (*p=='*' && left && at+2<=size && at+2+buffer[at+1]<=size) {   /* a count argument */
                if (buffer[at+1]>=4) memcpy(&precision,buffer+at+2,4);
                at+=2u+buffer[at+1];left--;
            }
            if (*p=='.' && p[1]>='0' && p[1]<='9') precision=p[1]-'0';
            p++;
        }
        while (*p && strchr("hlqjzt",*p)) p++;
        const char conversion=*p;
        if (!conversion) break;
        p++;
        if (!left || at+2>size || at+2+buffer[at+1]>size) {PUT("<?>");continue;}
        const uint8_t type=buffer[at]>>4,bytes=buffer[at+1];
        uint64_t value=0;
        memcpy(&value,buffer+at+2,bytes<8?bytes:8);
        at+=2u+bytes;left--;
        if (conversion=='s') {
            if (precision==0) continue;
            if (type==2 && value) PUT("%.160s",(const char *)(uintptr_t)value); else PUT("<s>");
        } else if (conversion=='@') {
            CFStringRef text=type==4 && value?CFCopyDescription((CFTypeRef)(uintptr_t)value):NULL;
            char shown[160]="<object>";
            if (text) {CFStringGetCString(text,shown,sizeof(shown),kCFStringEncodingUTF8);CFRelease(text);}
            PUT("%s",shown);
        } else if (strchr("fFeEgGaA",conversion)) {
            double number=0;
            if (bytes==8) memcpy(&number,&value,8); else if (bytes==4) {float f;memcpy(&f,&value,4);number=f;}
            PUT("%g",number);
        } else if (strchr("xXp",conversion)) {
            PUT("%llx",(unsigned long long)value);
        } else if (conversion=='u') {
            PUT("%llu",(unsigned long long)value);
        } else {
            PUT("%lld",bytes==4?(long long)(int32_t)value:bytes==2?(long long)(int16_t)value:bytes==1?(long long)(int8_t)value:(long long)value);
        }
    }
#undef PUT
    line[out]=0;
    fprintf(stderr,"oslog: %s\n",line);
}

static void
zizhan_oslog_tap(const char *format,const uint8_t *buffer,uint32_t size)
{
    static __thread int busy;           /* describing an object may log */
    if (!format || !buffer || busy || !zizhan_oslog_on()) return;
    busy=1;
    for (unsigned word=0;word<4;word++) {
        if (zizhan_oslog_words[word][0] && strstr(format,zizhan_oslog_words[word])) {
            zizhan_oslog_print(format,buffer,size);
            break;
        }
    }
    busy=0;
}

/*
 * Calls between libraries of the dyld shared cache are direct branches, so dyld interposing never sees a framework's os_log:
 * the entry points themselves get a jump to the taps below (code signing enforcement is off on these images). Their first
 * four instructions move to a trampoline that continues in the original; they must not depend on the pc.
 */
static bool
zizhan_oslog_pc_free(uint32_t i)
{
    return (i&0x1f000000)!=0x10000000 &&        /* adr, adrp */
           (i&0x7c000000)!=0x14000000 &&        /* b, bl */
           (i&0xff000010)!=0x54000000 &&        /* b.cond */
           (i&0x7e000000)!=0x34000000 &&        /* cbz, cbnz */
           (i&0x7e000000)!=0x36000000 &&        /* tbz, tbnz */
           (i&0x3b000000)!=0x18000000;          /* ldr literal */
}

/* *original is set before the entry is patched: another thread may log at any moment */
static bool
zizhan_oslog_hook(const char *symbol,const void *replacement,void **original)
{
    uint32_t *entry=(uint32_t *)dlsym(RTLD_DEFAULT,symbol);
    if (!entry) return false;
    for (unsigned i=0;i<4;i++) {
        if (!zizhan_oslog_pc_free(entry[i])) return false;
    }
    vm_address_t trampoline=0;
    if (vm_allocate(mach_task_self(),&trampoline,vm_page_size,VM_FLAGS_ANYWHERE)!=KERN_SUCCESS) return false;
    const uintptr_t resume=(uintptr_t)entry+16,target=(uintptr_t)replacement;
    const uint32_t back[4]={0x58000050,0xd61f0200,(uint32_t)resume,(uint32_t)(resume>>32)};       /* ldr x16, #8; br x16; address */
    const uint32_t jump[4]={0x58000050,0xd61f0200,(uint32_t)target,(uint32_t)(target>>32)};
    memcpy((void *)trampoline,entry,16);memcpy((void *)(trampoline+16),back,16);
    sys_icache_invalidate((void *)trampoline,32);
    if (vm_protect(mach_task_self(),trampoline,vm_page_size,FALSE,VM_PROT_READ|VM_PROT_EXECUTE)!=KERN_SUCCESS) {
        (void)vm_deallocate(mach_task_self(),trampoline,vm_page_size);return false;
    }
    const vm_address_t first=(vm_address_t)entry&~((vm_address_t)vm_page_size-1);
    const vm_size_t span=(((vm_address_t)entry+15)&~((vm_address_t)vm_page_size-1))-first+vm_page_size;
    if (vm_protect(mach_task_self(),first,span,FALSE,VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY)!=KERN_SUCCESS) {
        (void)vm_deallocate(mach_task_self(),trampoline,vm_page_size);return false;
    }
    if (original) __atomic_store_n(original,(void *)trampoline,__ATOMIC_RELEASE);
    memcpy(entry,jump,16);
    sys_icache_invalidate(entry,16);
    (void)vm_protect(mach_task_self(),first,span,FALSE,VM_PROT_READ|VM_PROT_EXECUTE);
    return true;
}

typedef void (*zizhan_oslog_fn)(void *dso,void *log,uint8_t type,const char *format,uint8_t *buffer,uint32_t size);
static zizhan_oslog_fn zizhan_oslog_original[3];

static void
zizhan_os_log_impl(void *dso,void *log,uint8_t type,const char *format,uint8_t *buffer,uint32_t size)
{
    zizhan_oslog_tap(format,buffer,size);
    /* a tail call: the original records its caller's return address */
    __attribute__((musttail)) return zizhan_oslog_original[0](dso,log,type,format,buffer,size);
}

static void
zizhan_os_log_error_impl(void *dso,void *log,uint8_t type,const char *format,uint8_t *buffer,uint32_t size)
{
    zizhan_oslog_tap(format,buffer,size);
    /* a tail call: the original records its caller's return address */
    __attribute__((musttail)) return zizhan_oslog_original[1](dso,log,type,format,buffer,size);
}

static void
zizhan_os_log_debug_impl(void *dso,void *log,uint8_t type,const char *format,uint8_t *buffer,uint32_t size)
{
    zizhan_oslog_tap(format,buffer,size);
    /* a tail call: the original records its caller's return address */
    __attribute__((musttail)) return zizhan_oslog_original[2](dso,log,type,format,buffer,size);
}

/* Not for a null log: callers pass one and rely on "not enabled" to skip the call (CoreTelephony's CCMonitor did, and
 * SpringBoard died in _os_log on it, sens-4..7). */
static bool
zizhan_os_log_type_enabled(void *log,uint8_t type)
{
    (void)type;
    return log!=NULL;
}

/* In every process the library is loaded into; nothing is touched without the boot-arg. */
static void __attribute__((constructor))
zizhan_oslog_start(void)
{
    if (!zizhan_oslog_on()) return;
    const bool a=zizhan_oslog_hook("_os_log_impl",(const void *)zizhan_os_log_impl,(void **)&zizhan_oslog_original[0]);
    const bool b=zizhan_oslog_hook("_os_log_error_impl",(const void *)zizhan_os_log_error_impl,(void **)&zizhan_oslog_original[1]);
    const bool c=zizhan_oslog_hook("_os_log_debug_impl",(const void *)zizhan_os_log_debug_impl,(void **)&zizhan_oslog_original[2]);
    const bool all=a && zizhan_oslog_hook("os_log_type_enabled",(const void *)zizhan_os_log_type_enabled,NULL);
    fprintf(stderr,"oslog: taps %d %d %d, every level %d, words %s %s %s %s\n",a,b,c,all,zizhan_oslog_words[0],
        zizhan_oslog_words[1],zizhan_oslog_words[2],zizhan_oslog_words[3]);
}
#endif
