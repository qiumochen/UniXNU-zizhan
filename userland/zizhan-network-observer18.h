/* SPDX-License-Identifier: MIT
 * Explicit APP-01 observation of real NSURLSession completion APIs. Original
 * requests, callbacks, bytes, HTTP status and errors are passed unchanged.
 */
#pragma once

typedef void (^app_network_completion)(id,id,id);
static id (*app_network_request_original)(id,SEL,id,app_network_completion);
static id (*app_network_url_original)(id,SEL,id,app_network_completion);
static unsigned app_network_sequence;
static id (*app_network_plain_request_original)(id,SEL,id);
static id (*app_network_plain_url_original)(id,SEL,id);
static id (*app_xpc_service_original)(id,SEL,id);
static id (*app_xpc_mach_original)(id,SEL,id,unsigned long long);
static id (*app_xpc_proxy_original)(id,SEL,void(^)(id));
static char app_xpc_name_key;
static unsigned app_xpc_sequence;

static void app_network_text(id value,const char *field,unsigned sequence)
{
    Class strings=objc_lookUpClass("NSString");
    if(!value || !strings || !((BOOL(*)(id,SEL,Class))objc_msgSend)(value,
        sel_registerName("isKindOfClass:"),strings))return;
    const unsigned char *text=(const unsigned char *)((const char *(*)(id,SEL))objc_msgSend)(value,sel_registerName("UTF8String"));
    if(!text)return;
    unsigned offset=0;
    while(text[offset] && offset<800) {
        char escaped[161];unsigned n=0,start=offset;
        while(text[offset] && offset<800 && n+5<sizeof escaped) {
            unsigned c=text[offset++];
            if(c>=32 && c<127 && c!='\\' && c!='"')escaped[n++]=(char)c;
            else n+=(unsigned)snprintf(escaped+n,sizeof escaped-n,"\\x%02x",c);
        }
        escaped[n]=0;
        fprintf(stderr,"Zizhan App18 %s %u %s byte=%u: %s\n",app_diag_process,sequence,field,start,escaped);
    }
}

static id app_network_task(id self,SEL selector,id value,app_network_completion completion,bool request)
{
    id (*original)(id,SEL,id,app_network_completion)=request?app_network_request_original:app_network_url_original;
    if(!completion)return original(self,selector,value,completion);
    unsigned sequence=__atomic_add_fetch(&app_network_sequence,1,__ATOMIC_RELAXED);
    if(sequence>32)return original(self,selector,value,completion);
    id url=request?app_diag_get(value,"URL"):value;
    app_network_text(app_diag_get(url,"absoluteString"),"network-url",sequence);
    fprintf(stderr,"Zizhan Net18 %s %u actual NSURLSession creation request=%d\n",app_diag_process,sequence,request);
    return original(self,selector,value,^(id data,id response,id error){
        long status=app_diag_responds(response,"statusCode")?
            ((long(*)(id,SEL))objc_msgSend)(response,sel_registerName("statusCode")):0;
        unsigned long length=app_diag_responds(data,"length")?
            ((unsigned long(*)(id,SEL))objc_msgSend)(data,sel_registerName("length")):0;
        long code=app_diag_responds(error,"code")?
            ((long(*)(id,SEL))objc_msgSend)(error,sel_registerName("code")):0;
        fprintf(stderr,"Zizhan Net18 %s %u actual completion status=%ld bytes=%lu error=%ld\n",
            app_diag_process,sequence,status,length,code);
        app_network_text(app_diag_get(error,"domain"),"network-error-domain",sequence);
        app_network_text(app_diag_get(error,"localizedDescription"),"network-error-description",sequence);
        completion(data,response,error);
    });
}

static id app_network_request(id self,SEL selector,id request,app_network_completion completion)
{return app_network_task(self,selector,request,completion,true);}

static id app_network_url(id self,SEL selector,id url,app_network_completion completion)
{return app_network_task(self,selector,url,completion,false);}

struct app_task_watch {id task;unsigned sequence,pass;};
static void app_task_sample(void *context)
{
    struct app_task_watch *watch=context;id task=watch->task;
    long state=app_diag_responds(task,"state")?((long(*)(id,SEL))objc_msgSend)(task,sel_registerName("state")):-1;
    id response=app_diag_get(task,"response"),error=app_diag_get(task,"error");
    long status=app_diag_responds(response,"statusCode")?((long(*)(id,SEL))objc_msgSend)(response,sel_registerName("statusCode")):0;
    long long bytes=app_diag_responds(task,"countOfBytesReceived")?((long long(*)(id,SEL))objc_msgSend)(task,sel_registerName("countOfBytesReceived")):0;
    fprintf(stderr,"Zizhan Net18 %s %u task sample=%u state=%ld status=%ld received=%lld\n",app_diag_process,watch->sequence,watch->pass,state,status,bytes);
    id request=app_diag_get(task,"currentRequest");
    app_network_text(app_diag_get(app_diag_get(request,"URL"),"absoluteString"),"task-url",watch->sequence);
    app_network_text(app_diag_get(error,"domain"),"task-error-domain",watch->sequence);
    app_network_text(app_diag_get(error,"localizedDescription"),"task-error-description",watch->sequence);
    if(++watch->pass<3 && state!=3) {
        dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,10*NSEC_PER_SEC),dispatch_get_global_queue(QOS_CLASS_UTILITY,0),watch,app_task_sample);
    } else {((void(*)(id,SEL))objc_msgSend)(task,sel_registerName("release"));free(watch);}
}

static id app_network_plain_task(id self,SEL selector,id value,bool request)
{
    id task=(request?app_network_plain_request_original:app_network_plain_url_original)(self,selector,value);
    unsigned sequence=__atomic_add_fetch(&app_network_sequence,1,__ATOMIC_RELAXED);
    if(sequence<=32) {
        app_network_text(app_diag_get(request?app_diag_get(value,"URL"):value,"absoluteString"),"network-delegate-url",sequence);
        fprintf(stderr,"Zizhan Net18 %s %u original delegate task created=%d\n",app_diag_process,sequence,task!=nil);
        struct app_task_watch *watch=task?malloc(sizeof(*watch)):NULL;
        if(watch) {
            watch->task=((id(*)(id,SEL))objc_msgSend)(task,sel_registerName("retain"));watch->sequence=sequence;watch->pass=0;
            dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC),dispatch_get_global_queue(QOS_CLASS_UTILITY,0),watch,app_task_sample);
        }
    }
    return task;
}
static id app_network_plain_request(id self,SEL selector,id value)
{return app_network_plain_task(self,selector,value,true);}
static id app_network_plain_url(id self,SEL selector,id value)
{return app_network_plain_task(self,selector,value,false);}

static id app_xpc_service(id self,SEL selector,id name)
{
    id connection=app_xpc_service_original(self,selector,name);
    if(connection)objc_setAssociatedObject(connection,&app_xpc_name_key,name,OBJC_ASSOCIATION_COPY_NONATOMIC);
    unsigned sequence=__atomic_add_fetch(&app_xpc_sequence,1,__ATOMIC_RELAXED);
    if(sequence<=64)app_network_text(name,"xpc-service",sequence);
    return connection;
}
static id app_xpc_mach(id self,SEL selector,id name,unsigned long long options)
{
    id connection=app_xpc_mach_original(self,selector,name,options);
    if(connection)objc_setAssociatedObject(connection,&app_xpc_name_key,name,OBJC_ASSOCIATION_COPY_NONATOMIC);
    unsigned sequence=__atomic_add_fetch(&app_xpc_sequence,1,__ATOMIC_RELAXED);
    if(sequence<=64)app_network_text(name,"xpc-mach-service",sequence);
    return connection;
}
static id app_xpc_proxy(id self,SEL selector,void(^handler)(id))
{
    if(!handler)return app_xpc_proxy_original(self,selector,handler);
    unsigned sequence=__atomic_add_fetch(&app_xpc_sequence,1,__ATOMIC_RELAXED);
    if(sequence>64)return app_xpc_proxy_original(self,selector,handler);
    app_network_text(objc_getAssociatedObject(self,&app_xpc_name_key),"xpc-proxy-service",sequence);
    return app_xpc_proxy_original(self,selector,^(id error){
        app_network_text(app_diag_get(error,"domain"),"xpc-error-domain",sequence);
        app_network_text(app_diag_get(error,"localizedDescription"),"xpc-error-description",sequence);
        handler(error);
    });
}

static bool app_network_abi(Method method,unsigned count,const char *first,const char *second)
{
    if(!method || method_getNumberOfArguments(method)!=count+2)return false;
    char *result=method_copyReturnType(method);
    bool valid=result && result[0]=='@';free(result);
    const char *expected[]={first,second};
    for(unsigned i=0;valid && i<count;i++) {
        char *type=method_copyArgumentType(method,i+2);
        valid=type && expected[i] && !strcmp(type,expected[i]);free(type);
    }
    return valid;
}

static void app_network_start(const char *process)
{
    app_diag_process=process;
    Class session=objc_lookUpClass("NSURLSession");
    const char *selectors[]={"dataTaskWithRequest:completionHandler:","dataTaskWithURL:completionHandler:"};
    IMP wrappers[]={(IMP)app_network_request,(IMP)app_network_url};
    for(unsigned i=0;i<2;i++) {
        Method method=session?class_getInstanceMethod(session,sel_registerName(selectors[i])):NULL;
        bool valid=method && method_getNumberOfArguments(method)==4;
        char *result=valid?method_copyReturnType(method):NULL;
        char *first=valid?method_copyArgumentType(method,2):NULL;
        char *second=valid?method_copyArgumentType(method,3):NULL;
        valid=valid && result && result[0]=='@' && first && first[0]=='@' && second && !strcmp(second,"@?");
        free(result);free(first);free(second);
        if(!valid){fprintf(stderr,"Zizhan Net18 %s: SKIP unavailable/incompatible %s\n",process,selectors[i]);continue;}
        IMP original=method_getImplementation(method);
        if(i==0)app_network_request_original=(__typeof__(app_network_request_original))original;
        else app_network_url_original=(__typeof__(app_network_url_original))original;
        method_setImplementation(method,wrappers[i]);
        fprintf(stderr,"Zizhan Net18 %s: observing original %s ABI=%s\n",process,selectors[i],method_getTypeEncoding(method));
    }
    const char *plain_names[]={"dataTaskWithRequest:","dataTaskWithURL:"};
    IMP plain_wrappers[]={(IMP)app_network_plain_request,(IMP)app_network_plain_url};
    for(unsigned i=0;i<2;i++) {
        Method method=session?class_getInstanceMethod(session,sel_registerName(plain_names[i])):NULL;
        if(!method || method_getNumberOfArguments(method)!=3)continue;
        char *result=method_copyReturnType(method),*argument=method_copyArgumentType(method,2);
        bool valid=result && result[0]=='@' && argument && argument[0]=='@';free(result);free(argument);
        if(!valid)continue;
        IMP original=method_getImplementation(method);
        if(i==0)app_network_plain_request_original=(__typeof__(app_network_plain_request_original))original;
        else app_network_plain_url_original=(__typeof__(app_network_plain_url_original))original;
        method_setImplementation(method,plain_wrappers[i]);
        fprintf(stderr,"Zizhan Net18 %s: observing original delegate %s\n",process,plain_names[i]);
    }
    Class connection=objc_lookUpClass("NSXPCConnection");
    Method service=connection?class_getInstanceMethod(connection,sel_registerName("initWithServiceName:")):NULL;
    Method mach=connection?class_getInstanceMethod(connection,sel_registerName("initWithMachServiceName:options:")):NULL;
    Method proxy=connection?class_getInstanceMethod(connection,sel_registerName("remoteObjectProxyWithErrorHandler:")):NULL;
    if(app_network_abi(service,1,"@",NULL))app_xpc_service_original=(__typeof__(app_xpc_service_original))method_setImplementation(service,(IMP)app_xpc_service);
    if(app_network_abi(mach,2,"@","Q"))app_xpc_mach_original=(__typeof__(app_xpc_mach_original))method_setImplementation(mach,(IMP)app_xpc_mach);
    if(app_network_abi(proxy,1,"@?",NULL))app_xpc_proxy_original=(__typeof__(app_xpc_proxy_original))method_setImplementation(proxy,(IMP)app_xpc_proxy);
    fprintf(stderr,"Zizhan Net18 %s: original XPC observers service=%d mach=%d proxy=%d\n",process,app_xpc_service_original!=NULL,app_xpc_mach_original!=NULL,app_xpc_proxy_original!=NULL);
}
