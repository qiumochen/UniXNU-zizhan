/* SPDX-License-Identifier: MIT
 * Explicit APP-01 observations only: no service replies, navigation, settings
 * changes or media control. The caller confines this to Weather/MobileSafari.
 */
#pragma once
#include <time.h>

static unsigned app_diag_nodes,app_diag_lines,app_diag_webviews;
static const char *app_diag_process;

static void app_diag_fatal(int number,siginfo_t *info,void *context)
{
    (void)info;
    char line[320];int n=0;
#if defined(__aarch64__)
    ucontext_t *state=(ucontext_t *)context;
    if(state && state->uc_mcontext)n=snprintf(line,sizeof line,
        "Zizhan App18 fatal signal=%d pc=%llx lr=%llx sp=%llx x0=%llx x1=%llx x2=%llx x3=%llx\n",
        number,(unsigned long long)state->uc_mcontext->__ss.__pc,
        (unsigned long long)state->uc_mcontext->__ss.__lr,
        (unsigned long long)state->uc_mcontext->__ss.__sp,
        (unsigned long long)state->uc_mcontext->__ss.__x[0],
        (unsigned long long)state->uc_mcontext->__ss.__x[1],
        (unsigned long long)state->uc_mcontext->__ss.__x[2],
        (unsigned long long)state->uc_mcontext->__ss.__x[3]);
#else
    (void)context;
#endif
    if(n>0)(void)write(2,line,(size_t)n<sizeof line?(size_t)n:sizeof line-1);
    /* Keep the real fatal result and the normal crash path. */
    signal(number,SIG_DFL);raise(number);
}

static bool app_diag_responds(id object,const char *name)
{
    return object && ((BOOL(*)(id,SEL,SEL))objc_msgSend)(object,
        sel_registerName("respondsToSelector:"),sel_registerName(name));
}

static id app_diag_get(id object,const char *name)
{
    return app_diag_responds(object,name)?((id(*)(id,SEL))objc_msgSend)(object,sel_registerName(name)):nil;
}

static void app_diag_text(id value,const char *field,unsigned pass)
{
    Class strings=objc_lookUpClass("NSString");
    if(!value || !strings || !((BOOL(*)(id,SEL,Class))objc_msgSend)(value,
        sel_registerName("isKindOfClass:"),strings))return;
    const unsigned char *text=(const unsigned char *)((const char *(*)(id,SEL))objc_msgSend)(value,sel_registerName("UTF8String"));
    if(!text)return;
    /* Short escaped chunks survive the console's high-bit removal and line
     * limit. Offset is a UTF-8 byte offset; the host joins before decoding. */
    unsigned offset=0;
    while(text[offset] && offset<1600 && app_diag_lines<100) {
        char escaped[161];unsigned n=0,start=offset;
        while(text[offset] && offset<1600 && n+5<sizeof escaped) {
            unsigned c=text[offset++];
            if(c>=32 && c<127 && c!='\\' && c!='"')escaped[n++]=(char)c;
            else n+=(unsigned)snprintf(escaped+n,sizeof escaped-n,"\\x%02x",c);
        }
        escaped[n]=0;++app_diag_lines;
        fprintf(stderr,"Zizhan App18 %s %u %s byte=%u: %s\n",app_diag_process,pass,field,start,escaped);
    }
}

static void app_diag_walk(id object,unsigned depth,unsigned pass)
{
    if(!object || depth>18 || app_diag_nodes++>=220 || app_diag_lines>=100)return;
    if(app_diag_responds(object,"isHidden") && ((BOOL(*)(id,SEL))objc_msgSend)(object,sel_registerName("isHidden")))return;
    const char *fields[]={"text","accessibilityLabel","accessibilityValue","placeholder"};
    for(unsigned i=0;i<sizeof fields/sizeof fields[0];i++)app_diag_text(app_diag_get(object,fields[i]),fields[i],pass);
    Class web=objc_lookUpClass("WKWebView");
    if(web && ((BOOL(*)(id,SEL,Class))objc_msgSend)(object,sel_registerName("isKindOfClass:"),web) && app_diag_webviews++<2) {
        app_diag_text(app_diag_get(app_diag_get(object,"URL"),"absoluteString"),"web-url",pass);
        app_diag_text(app_diag_get(object,"title"),"web-title",pass);
        if(app_diag_responds(object,"isLoading") && app_diag_responds(object,"estimatedProgress"))
            fprintf(stderr,"Zizhan App18 %s %u web loading=%d progress=%.3f\n",app_diag_process,pass,
                ((BOOL(*)(id,SEL))objc_msgSend)(object,sel_registerName("isLoading")),
                ((double(*)(id,SEL))objc_msgSend)(object,sel_registerName("estimatedProgress")));
        SEL evaluate=sel_registerName("evaluateJavaScript:completionHandler:");
        if(app_diag_responds(object,"evaluateJavaScript:completionHandler:")) {
            id script=(id)CFSTR("JSON.stringify({url:location.href,title:document.title,scrollY:scrollY,viewport:innerHeight,height:document.documentElement.scrollHeight,text:document.body?document.body.innerText.slice(0,1200):null,video:Array.from(document.querySelectorAll('video')).slice(0,2).map(v=>({time:v.currentTime,paused:v.paused,ready:v.readyState,error:v.error?{code:v.error.code,message:v.error.message}:null}))})");
            ((void(*)(id,SEL,id,void(^)(id,id)))objc_msgSend)(object,evaluate,script,^(id value,id error){
                app_diag_text(value,"web-document",pass);
                if(error)app_diag_text(app_diag_get(error,"description"),"web-observation-error",pass);
            });
        }
    }
    const char *collections[]={"subviews","accessibilityElements"};
    for(unsigned k=0;k<2;k++) {
        id values=app_diag_get(object,collections[k]);
        if(!app_diag_responds(values,"count") || !app_diag_responds(values,"objectAtIndex:"))continue;
        unsigned long count=((unsigned long(*)(id,SEL))objc_msgSend)(values,sel_registerName("count"));
        if(count>48)count=48;
        for(unsigned long i=0;i<count;i++)app_diag_walk(
            ((id(*)(id,SEL,unsigned long))objc_msgSend)(values,sel_registerName("objectAtIndex:"),i),depth+1,pass);
    }
}

static void app_diag_sample(void *context)
{
    unsigned pass=(unsigned)(uintptr_t)context;
    app_diag_nodes=app_diag_lines=app_diag_webviews=0;
    id application=((id(*)(id,SEL))objc_msgSend)((id)objc_lookUpClass("UIApplication"),sel_registerName("sharedApplication"));
    id windows=app_diag_get(application,"windows");
    unsigned long count=app_diag_responds(windows,"count")?
        ((unsigned long(*)(id,SEL))objc_msgSend)(windows,sel_registerName("count")):0;
    fprintf(stderr,"Zizhan App18 %s %u begin windows=%lu wall=%lld abs=%llu\n",app_diag_process,pass,count,
        (long long)time(NULL),(unsigned long long)mach_absolute_time());
    const unsigned long lowest=count>8?count-8:0;
    /* System prompts live above application content. Observe top windows first
     * so the bounded node budget cannot be consumed by the home screen. */
    for(unsigned long i=count;i>lowest;i--) {
        id window=((id(*)(id,SEL,unsigned long))objc_msgSend)(windows,sel_registerName("objectAtIndex:"),i-1);
        id controller=app_diag_get(window,"rootViewController");
        for(unsigned depth=0;controller && depth<5;depth++) {
            app_diag_text(app_diag_get(controller,"title"),"controller-title",pass);
            app_diag_text(app_diag_get(controller,"message"),"controller-message",pass);
            controller=app_diag_get(controller,"presentedViewController");
        }
        app_diag_walk(window,0,pass);
    }
    fprintf(stderr,"Zizhan App18 %s %u end nodes=%u lines=%u webviews=%u\n",
        app_diag_process,pass,app_diag_nodes,app_diag_lines,app_diag_webviews);
}

static void app_diag_start(const char *process)
{
    app_diag_process=process;
    struct sigaction action={0};action.sa_sigaction=app_diag_fatal;
    action.sa_flags=SA_SIGINFO|SA_RESETHAND;sigemptyset(&action.sa_mask);
    (void)sigaction(SIGILL,&action,NULL);(void)sigaction(SIGTRAP,&action,NULL);
    (void)sigaction(SIGABRT,&action,NULL);(void)sigaction(SIGSEGV,&action,NULL);
    void *(*cache_range)(size_t *)=dlsym(RTLD_DEFAULT,"_dyld_get_shared_cache_range");
    size_t cache_bytes=0;void *cache=cache_range?cache_range(&cache_bytes):NULL;
    fprintf(stderr,"Zizhan App18 %s image=%s slide=%llx cache=%p bytes=%zx\n",process,
        _dyld_get_image_name(0),(unsigned long long)_dyld_get_image_vmaddr_slide(0),cache,cache_bytes);
    static const unsigned seconds[]={15,35,65,100};
    fprintf(stderr,"Zizhan App18 %s: bounded read-only UI/WebKit observation enabled\n",process);
    for(unsigned i=0;i<4;i++)dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,(int64_t)seconds[i]*NSEC_PER_SEC),
        dispatch_get_main_queue(),(void *)(uintptr_t)(i+1),app_diag_sample);
}
