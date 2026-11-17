/* SPDX-License-Identifier: MIT
 * Explicit, bounded APP-01 keyboard scenario. Real HID Command-L, URL keys
 * and Return go through Safari's normal address-entry path. Default off.
 */
#pragma once

static bool app_key(IOHIDServiceClientRef keyboard,unsigned usage,bool down)
{
    IOHIDEventRef event=IOHIDEventCreateKeyboardEvent(NULL,mach_absolute_time(),7,usage,down,0);
    if(!event)return false;
    IOHIDEventSetIntegerValue(event,FIELD_IS_BUILT_IN,1);
    bool sent=IOHIDVirtualServiceClientDispatchEvent(keyboard,event);
    CFRelease(event);return sent;
}

static unsigned app_ascii_usage(unsigned c,bool *shift)
{
    *shift=false;
    if(c>='a' && c<='z')return c-'a'+4;
    if(c>='A' && c<='Z'){*shift=true;return c-'A'+4;}
    if(c>='1' && c<='9')return c-'1'+30;
    if(c=='0')return 39;
    switch(c) {
        case '.':return 55;case '/':return 56;case '-':return 45;
        case ':':*shift=true;return 51;case '_':*shift=true;return 45;
        case '?':*shift=true;return 56;case '=':return 46;
        case '&':*shift=true;return 36;
        default:return 0;
    }
}

static bool app_type_option(const char *args,const char *key,char *out,size_t capacity)
{
    size_t key_bytes=strlen(key);
    for(const char *p=args;*p;) {
        while(*p==' ')p++;
        const char *end=strchr(p,' ');if(!end)end=p+strlen(p);
        if((size_t)(end-p)>key_bytes && !strncmp(p,key,key_bytes)) {
            size_t n=(size_t)(end-p)-key_bytes;
            if(n>=capacity)return false;
            memcpy(out,p+key_bytes,n);out[n]=0;return true;
        }
        p=end;
    }
    return false;
}

static void *app_keyboard_thread(void *unused)
{
    (void)unused;char args[1024]={0},url[160]={0},when[8]={0};size_t bytes=sizeof args;
    if(sysctlbyname("kern.bootargs",args,&bytes,NULL,0))return NULL;
    args[sizeof args-1]=0;
    if(!app_type_option(args,"zizhan_type_url=",url,sizeof url))return NULL;
    if(strncmp(url,"https://",8) && strncmp(url,"http://",7))return NULL;
    for(const unsigned char *p=(const unsigned char *)url;*p;p++){bool shifted;if(!app_ascii_usage(*p,&shifted))return NULL;}
    unsigned delay=80,key_ms=100,focus=1;
    if(app_type_option(args,"zizhan_type_at=",when,sizeof when))delay=(unsigned)strtoul(when,NULL,10);
    if(delay<30 || delay>180){say("Zizhan keyboard18: SKIP invalid bounded delay\n");return NULL;}
    if(app_type_option(args,"zizhan_key_ms=",when,sizeof when))key_ms=(unsigned)strtoul(when,NULL,10);
    if(key_ms<30 || key_ms>250){say("Zizhan keyboard18: SKIP invalid key interval\n");return NULL;}
    if(app_type_option(args,"zizhan_key_focus=",when,sizeof when))focus=(unsigned)strtoul(when,NULL,10);
    if(focus>1)return NULL;
    /* Wait until the initial UI is available; no polling or infinite retry. */
    sleep(delay);
    IOHIDEventSystemClientRef client=IOHIDEventSystemClientCreateWithType(NULL,HID_CLIENT_SIMPLE,NULL);
    if(!client){say("Zizhan keyboard18: SKIP no HID client\n");return NULL;}
    IOHIDEventSystemClientScheduleWithDispatchQueue(client,dispatch_queue_create("zizhan.app.keyboard",NULL));
    IOHIDServiceClientRef keys=make_service(client,"Zizhan APP01 test keyboard",1,6,false);
    if(!keys){CFRelease(client);say("Zizhan keyboard18: SKIP keyboard not opened\n");return NULL;}
    bool ok=true;
    if(focus) {
        ok=app_key(keys,0xe3,true); // Left GUI / Command
        usleep(key_ms*1000);ok=app_key(keys,15,true)&&ok;usleep(key_ms*1000);
        ok=app_key(keys,15,false)&&ok;usleep(key_ms*1000);ok=app_key(keys,0xe3,false)&&ok;
    }
    usleep(1500000);
    for(const unsigned char *p=(const unsigned char *)url;*p && ok;p++) {
        bool shifted;unsigned usage=app_ascii_usage(*p,&shifted);
        if(shifted)ok=app_key(keys,0xe1,true)&&ok;
        ok=app_key(keys,usage,true)&&ok;usleep(key_ms*1000);ok=app_key(keys,usage,false)&&ok;
        if(shifted)ok=app_key(keys,0xe1,false)&&ok;
        usleep(key_ms*1000);
    }
    usleep(500000);
    if(ok){ok=app_key(keys,40,true);usleep(key_ms*1000);ok=app_key(keys,40,false)&&ok;}
    /* Release modifiers even when event delivery failed. */
    (void)app_key(keys,0xe1,false);(void)app_key(keys,0xe3,false);
    say("Zizhan keyboard18: one real HID URL sequence dispatched=%u bytes=%zu; actual address/page requires observation\n",ok,strlen(url));
    IOHIDVirtualServiceClientRemove(keys);CFRelease(keys);CFRelease(client);return NULL;
}
