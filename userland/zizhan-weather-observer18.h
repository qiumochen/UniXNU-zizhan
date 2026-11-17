/* SPDX-License-Identifier: MIT
 * Opt-in observations at the same-restore WeatherDaemon ObjC request boundary.
 * Never invoke requests, generate tokens, change results or print token bodies.
 */
#pragma once

typedef void (^app_weather_completion)(id,id);
typedef void (*app_weather_request_imp)(id,SEL,id,app_weather_completion);
static app_weather_request_imp app_weather_original[3];
static unsigned app_weather_sequence;

static bool app_weather_completion_abi(app_weather_completion completion)
{
    /* Apple's public Blocks ABI: signature follows descriptor size and the
     * optional copy/dispose helpers. Unknown callbacks pass through untouched. */
    struct block_layout {void *isa;int flags,reserved;void *invoke;const void *descriptor;};
    const struct block_layout *block=(const void *)completion;
    if(!block || !(block->flags&(1u<<30)) || !block->descriptor)return false;
    const unsigned char *descriptor=block->descriptor;
    descriptor+=2*sizeof(unsigned long);
    if(block->flags&(1u<<25))descriptor+=2*sizeof(void *);
    const char *types=NULL;memcpy(&types,descriptor,sizeof types);
    Class cls=objc_lookUpClass("NSMethodSignature");
    if(!types || !cls)return false;
    id signature=((id(*)(id,SEL,const char *))objc_msgSend)((id)cls,
        sel_registerName("signatureWithObjCTypes:"),types);
    if(!signature || ((unsigned long(*)(id,SEL))objc_msgSend)(signature,
        sel_registerName("numberOfArguments"))!=3)return false;
    const char *result=((const char *(*)(id,SEL))objc_msgSend)(signature,sel_registerName("methodReturnType"));
    if(!result || strcmp(result,"v"))return false;
    for(unsigned long i=1;i<3;i++) {
        const char *argument=((const char *(*)(id,SEL,unsigned long))objc_msgSend)(signature,
            sel_registerName("getArgumentTypeAtIndex:"),i);
        if(!argument || argument[0]!='@' || argument[1]=='?')return false;
    }
    return true;
}

static void app_weather_request(id self,SEL selector,id request,
    app_weather_completion completion,unsigned slot)
{
    app_weather_request_imp original=app_weather_original[slot];
    unsigned sequence=__atomic_add_fetch(&app_weather_sequence,1,__ATOMIC_RELAXED);
    if(sequence>32 || !app_weather_completion_abi(completion)) {
        original(self,selector,request,completion);return;
    }
    fprintf(stderr,"Zizhan Weather18 %s %u begin operation=%u request-class=%s abs=%llu\n",
        app_diag_process,sequence,slot,request?object_getClassName(request):"nil",
        (unsigned long long)mach_absolute_time());
    original(self,selector,request,^(id result,id error){
        long code=app_diag_responds(error,"code")?
            ((long(*)(id,SEL))objc_msgSend)(error,sel_registerName("code")):0;
        fprintf(stderr,"Zizhan Weather18 %s %u complete operation=%u result-class=%s error=%ld abs=%llu\n",
            app_diag_process,sequence,slot,result?object_getClassName(result):"nil",code,
            (unsigned long long)mach_absolute_time());
        /* The result may be an authentication token: do not describe it. */
        app_network_text(app_diag_get(error,"domain"),"weather-error-domain",sequence);
        app_network_text(app_diag_get(error,"localizedDescription"),"weather-error-description",sequence);
        completion(result,error);
    });
}

static void app_weather_forecast(id self,SEL selector,id request,app_weather_completion completion)
{app_weather_request(self,selector,request,completion,0);}
static void app_weather_availability(id self,SEL selector,id request,app_weather_completion completion)
{app_weather_request(self,selector,request,completion,1);}
static void app_weather_auth(id self,SEL selector,id request,app_weather_completion completion)
{app_weather_request(self,selector,request,completion,2);}

static bool app_weather_abi(Method method)
{
    if(!method || method_getNumberOfArguments(method)!=4)return false;
    char *result=method_copyReturnType(method);
    char *request=method_copyArgumentType(method,2);
    char *completion=method_copyArgumentType(method,3);
    bool valid=result && !strcmp(result,"v") && request && request[0]=='@' &&
        request[1]!='?' && completion && !strcmp(completion,"@?");
    free(result);free(request);free(completion);return valid;
}

static void app_weather_install(void *context)
{
    const unsigned pass=(unsigned)(uintptr_t)context;
    static const char *classes[]={
        "_TtC13WeatherDaemon23WeatherDataServiceProxy",
        "_TtC13WeatherDaemon23WeatherDataServiceProxy",
        "_TtC13WeatherDaemon34WDSJWTAuthenticatorServiceListener"};
    static const char *selectors[]={"performRequestsWith:completion:",
        "fetchAvailableDataSetsWith:completion:","generateTokenWithUrl:completion:"};
    IMP wrappers[]={(IMP)app_weather_forecast,(IMP)app_weather_availability,(IMP)app_weather_auth};
    for(unsigned i=0;i<3;i++) {
        if(app_weather_original[i])continue;
        Class cls=objc_lookUpClass(classes[i]);
        Method method=cls?class_getInstanceMethod(cls,sel_registerName(selectors[i])):NULL;
        if(!app_weather_abi(method)) {
            fprintf(stderr,"Zizhan Weather18 %s pass=%u operation=%u SKIP class=%d compatible=0\n",
                app_diag_process,pass,i,cls!=Nil);continue;
        }
        /* Publish the original before installing a wrapper another thread can call. */
        app_weather_original[i]=(app_weather_request_imp)method_getImplementation(method);
        method_setImplementation(method,wrappers[i]);
        fprintf(stderr,"Zizhan Weather18 %s pass=%u observing operation=%u ABI=%s\n",
            app_diag_process,pass,i,method_getTypeEncoding(method));
    }
}

static void app_weather_start(void)
{
    /* Confined to one queue; retries cover lazily loaded framework classes. */
    static const unsigned seconds[]={0,2,10,25};
    for(unsigned i=0;i<4;i++)dispatch_after_f(
        dispatch_time(DISPATCH_TIME_NOW,(int64_t)seconds[i]*NSEC_PER_SEC),
        dispatch_get_main_queue(),(void *)(uintptr_t)(i+1),app_weather_install);
}
