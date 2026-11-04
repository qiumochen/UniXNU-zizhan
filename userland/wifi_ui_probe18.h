/* SPDX-License-Identifier: MIT */
/* Opt-in, bounded interaction with the real Settings controls. The private
 * fixture stays in the local test image; no SSID or password is logged. */
static CFStringRef wifi_probe_ssid, wifi_probe_password;
static bool wifi_probe_selected, wifi_probe_filled, wifi_probe_toggled;
static uint64_t wifi_probe_until;
static id wifi_probe_cell, wifi_probe_field, wifi_probe_join, wifi_probe_switch;
static id wifi_probe_table;
static id wifi_probe_baritem;
static unsigned long wifi_probe_section, wifi_probe_row;
static unsigned wifi_probe_scrolls;

static bool wifi_probe_is(id object, const char *class_name)
{
    Class cls=objc_lookUpClass(class_name);
    return cls && ((BOOL(*)(id,SEL,Class))objc_msgSend)(object,sel_registerName("isKindOfClass:"),cls);
}
static bool wifi_probe_title(const char *text)
{
    return text && (!strcmp(text,"Join") || !strcmp(text,"\xe5\x8a\xa0\xe5\x85\xa5") ||
                    !strcmp(text,"\xe8\xbf\x9e\xe6\x8e\xa5"));
}
static id wifi_probe_parent_cell(id view)
{
    for(unsigned i=0;view && i<48;i++) {
        if(wifi_probe_is(view,"UITableViewCell"))return view;
        view=((id(*)(id,SEL))objc_msgSend)(view,sel_registerName("superview"));
    }
    return nil;
}
static void wifi_probe_walk(id view,unsigned depth,unsigned *budget)
{
    if(!view || depth>40 || !*budget)return;
    --*budget;
    if(((BOOL(*)(id,SEL))objc_msgSend)(view,sel_registerName("isHidden")))return;
    if(wifi_probe_is(view,"UILabel")) {
        const char *text=ui_text(view,"text");
        if(wifi_probe_title(text)) {
            id control=((id(*)(id,SEL))objc_msgSend)(view,sel_registerName("superview"));
            for(unsigned i=0;control && i<6;i++) {
                if(wifi_probe_is(control,"UIControl") && ((BOOL(*)(id,SEL))objc_msgSend)(control,sel_registerName("isEnabled"))) {wifi_probe_join=control;break;}
                control=((id(*)(id,SEL))objc_msgSend)(control,sel_registerName("superview"));
            }
        }
        if(text && wifi_probe_ssid) {
            CFStringRef label=CFStringCreateWithCString(NULL,text,kCFStringEncodingUTF8);
            if(label && CFEqual(label,wifi_probe_ssid))wifi_probe_cell=wifi_probe_parent_cell(view);
            if(label)CFRelease(label);
        }
    }
    if(wifi_probe_is(view,"UITextField") &&
       ((BOOL(*)(id,SEL))objc_msgSend)(view,sel_registerName("isSecureTextEntry")))wifi_probe_field=view;
    if(wifi_probe_is(view,"UINavigationBar")) {
        id item=((id(*)(id,SEL))objc_msgSend)(view,sel_registerName("topItem"));
        id button=item?((id(*)(id,SEL))objc_msgSend)(item,sel_registerName("rightBarButtonItem")):nil;
        if(button && wifi_probe_title(ui_text(button,"title")) &&
           ((BOOL(*)(id,SEL))objc_msgSend)(button,sel_registerName("isEnabled")))wifi_probe_baritem=button;
    }
    if(wifi_probe_is(view,"UIControl") &&
       ((BOOL(*)(id,SEL))objc_msgSend)(view,sel_registerName("isEnabled"))) {
        const char *title=ui_text(view,"currentTitle");
        if(!title)title=ui_text(view,"accessibilityLabel");
        if(wifi_probe_title(title))wifi_probe_join=view;
    }
    if(wifi_probe_is(view,"UISwitch")) {
        id cell=wifi_probe_parent_cell(view);
        id label=cell && ((BOOL(*)(id,SEL,SEL))objc_msgSend)(cell,sel_registerName("respondsToSelector:"),sel_registerName("textLabel"))?
            ((id(*)(id,SEL))objc_msgSend)(cell,sel_registerName("textLabel")):nil;
        const char *title=label?ui_text(label,"text"):NULL;
        if(title && (!strcmp(title,"Wi-Fi") || !strcmp(title,"Wi\xe2\x80\x91" "Fi"))) {
            wifi_probe_switch=view;
            id parent=cell;
            while(parent && !wifi_probe_is(parent,"UITableView"))parent=((id(*)(id,SEL))objc_msgSend)(parent,sel_registerName("superview"));
            if(parent)wifi_probe_table=parent;
        }
    }
    id children=((id(*)(id,SEL))objc_msgSend)(view,sel_registerName("subviews"));
    const unsigned long count=children?((unsigned long(*)(id,SEL))objc_msgSend)(children,sel_registerName("count")):0;
    for(unsigned long i=0;i<count && *budget;i++)
        wifi_probe_walk(((id(*)(id,SEL,unsigned long))objc_msgSend)(children,sel_registerName("objectAtIndex:"),i),depth+1,budget);
}
static void wifi_probe_finish(const char *result)
{
    fprintf(stderr,"Pong WiFi UI test: %s (selected=%d password-field=%d)\n",result,wifi_probe_selected,wifi_probe_filled);
    if(wifi_probe_ssid)CFRelease(wifi_probe_ssid);
    if(wifi_probe_password)CFRelease(wifi_probe_password);
    wifi_probe_ssid=wifi_probe_password=NULL;
}
static void wifi_probe_tick(void *unused)
{
    (void)unused;
    if(clock_gettime_nsec_np(CLOCK_UPTIME_RAW)>=wifi_probe_until) {wifi_probe_finish("FAIL: control deadline");return;}
    wifi_probe_cell=wifi_probe_field=wifi_probe_join=wifi_probe_switch=nil;
    wifi_probe_baritem=nil;
    id app=((id(*)(id,SEL))objc_msgSend)((id)objc_lookUpClass("UIApplication"),sel_registerName("sharedApplication"));
    id windows=app?((id(*)(id,SEL))objc_msgSend)(app,sel_registerName("windows")):nil;
    unsigned budget=1600;
    const unsigned long count=windows?((unsigned long(*)(id,SEL))objc_msgSend)(windows,sel_registerName("count")):0;
    for(unsigned long i=0;i<count && budget;i++)
        wifi_probe_walk(((id(*)(id,SEL,unsigned long))objc_msgSend)(windows,sel_registerName("objectAtIndex:"),i),0,&budget);
    if(!wifi_probe_toggled && wifi_probe_switch &&
       ((BOOL(*)(id,SEL))objc_msgSend)(wifi_probe_switch,sel_registerName("isEnabled")) &&
       !((BOOL(*)(id,SEL))objc_msgSend)(wifi_probe_switch,sel_registerName("isOn"))) {
        wifi_probe_toggled=true;
        ((void(*)(id,SEL,BOOL,BOOL))objc_msgSend)(wifi_probe_switch,sel_registerName("setOn:animated:"),YES,NO);
        ((void(*)(id,SEL,unsigned long))objc_msgSend)(wifi_probe_switch,sel_registerName("sendActionsForControlEvents:"),1ul<<12);
        fprintf(stderr,"Pong WiFi UI test: RUNNING native Wi-Fi switch action\n");
    } else if(!wifi_probe_selected && wifi_probe_cell) {
        id table=((id(*)(id,SEL))objc_msgSend)(wifi_probe_cell,sel_registerName("superview"));
        while(table && !wifi_probe_is(table,"UITableView"))table=((id(*)(id,SEL))objc_msgSend)(table,sel_registerName("superview"));
        id path=table?((id(*)(id,SEL,id))objc_msgSend)(table,sel_registerName("indexPathForCell:"),wifi_probe_cell):nil;
        id delegate=table?((id(*)(id,SEL))objc_msgSend)(table,sel_registerName("delegate")):nil;
        SEL select=sel_registerName("tableView:didSelectRowAtIndexPath:");
        if(path && delegate && ((BOOL(*)(id,SEL,SEL))objc_msgSend)(delegate,sel_registerName("respondsToSelector:"),select)) {
            wifi_probe_selected=true;
            ((void(*)(id,SEL,id,id))objc_msgSend)(delegate,select,table,path);
            fprintf(stderr,"Pong WiFi UI test: RUNNING selected exact private fixture row\n");
        }
    } else if(wifi_probe_selected && !wifi_probe_filled && wifi_probe_field) {
        wifi_probe_filled=true;
        ((BOOL(*)(id,SEL))objc_msgSend)(wifi_probe_field,sel_registerName("becomeFirstResponder"));
        ((void(*)(id,SEL,id))objc_msgSend)(wifi_probe_field,sel_registerName("selectAll:"),nil);
        ((void(*)(id,SEL,id))objc_msgSend)(wifi_probe_field,sel_registerName("insertText:"),(id)wifi_probe_password);
        fprintf(stderr,"Pong WiFi UI test: RUNNING inserted text through native secure field\n");
    } else if(wifi_probe_filled && wifi_probe_baritem) {
        id target=((id(*)(id,SEL))objc_msgSend)(wifi_probe_baritem,sel_registerName("target"));
        SEL action=((SEL(*)(id,SEL))objc_msgSend)(wifi_probe_baritem,sel_registerName("action"));
        if(action) {
            BOOL sent=((BOOL(*)(id,SEL,SEL,id,id,id))objc_msgSend)(app,sel_registerName("sendAction:to:from:forEvent:"),action,target,wifi_probe_baritem,nil);
            wifi_probe_finish(sent?"RUNNING native navigation Join action sent; verify driver and HTTPS separately":"FAIL: native navigation Join action refused");return;
        }
    } else if(wifi_probe_filled && wifi_probe_join) {
        ((void(*)(id,SEL,unsigned long))objc_msgSend)(wifi_probe_join,sel_registerName("sendActionsForControlEvents:"),1ul<<6);
        wifi_probe_finish("RUNNING native Join action sent; verify driver and HTTPS separately");return;
    } else if(!wifi_probe_selected && wifi_probe_table && wifi_probe_scrolls<48) {
        const unsigned long sections=((unsigned long(*)(id,SEL))objc_msgSend)(wifi_probe_table,sel_registerName("numberOfSections"));
        if(wifi_probe_section>=sections) {wifi_probe_section=0;wifi_probe_row=0;}
        if(sections) {
            unsigned long rows=((unsigned long(*)(id,SEL,unsigned long))objc_msgSend)(wifi_probe_table,sel_registerName("numberOfRowsInSection:"),wifi_probe_section);
            if(wifi_probe_row>=rows) {wifi_probe_section++;wifi_probe_row=0;}
            else {
                id path=((id(*)(id,SEL,unsigned long,unsigned long))objc_msgSend)((id)objc_lookUpClass("NSIndexPath"),sel_registerName("indexPathForRow:inSection:"),wifi_probe_row,wifi_probe_section);
                ((void(*)(id,SEL,id,unsigned long,BOOL))objc_msgSend)(wifi_probe_table,sel_registerName("scrollToRowAtIndexPath:atScrollPosition:animated:"),path,2,NO);
                wifi_probe_row+=4;wifi_probe_scrolls++;
                if((wifi_probe_scrolls%8)==1)fprintf(stderr,"Pong WiFi UI test: RUNNING bounded network-list scroll %u\n",wifi_probe_scrolls);
            }
        }
    }
    dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC),dispatch_get_main_queue(),NULL,wifi_probe_tick);
}
static void wifi_ui_probe_start(void)
{
    char args[2048];size_t bytes=sizeof(args)-1;
    if(sysctlbyname("kern.bootargs",args,&bytes,NULL,0))return;
    args[bytes<sizeof(args)?bytes:sizeof(args)-1]='\0';
    const char *option=" pong_wifi_ui_test=1",*flag=strstr(args,option);
    if(!flag)return;
    const char next=flag[strlen(option)];
    if(next && next!=' ' && next!='\t')return;
    int fd=open("/pong/wifi-ui-test.plist",O_RDONLY|O_NOFOLLOW);struct stat st;
    if(fd<0) {fprintf(stderr,"Pong WiFi UI test: SKIP no private fixture\n");return;}
    if(fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_uid!=geteuid() || (st.st_mode&077) || st.st_size<1 || st.st_size>4096) {
        close(fd);fprintf(stderr,"Pong WiFi UI test: SKIP private fixture permissions/size\n");return;
    }
    unsigned char raw[4096];ssize_t n=read(fd,raw,(size_t)st.st_size);close(fd);
    CFDataRef data=n==st.st_size?CFDataCreate(NULL,raw,n):NULL;
    memset(raw,0,sizeof(raw));
    CFPropertyListRef fixture=data?CFPropertyListCreateWithData(NULL,data,kCFPropertyListImmutable,NULL,NULL):NULL;
    if(data)CFRelease(data);
    if(fixture && CFGetTypeID(fixture)==CFDictionaryGetTypeID()) {
        CFTypeRef ssid=CFDictionaryGetValue(fixture,CFSTR("ssid")),password=CFDictionaryGetValue(fixture,CFSTR("psk"));
        char s[129],p[257];
        if(ssid && password && CFGetTypeID(ssid)==CFStringGetTypeID() && CFGetTypeID(password)==CFStringGetTypeID() &&
           CFStringGetCString(ssid,s,sizeof(s),kCFStringEncodingUTF8) && CFStringGetCString(password,p,sizeof(p),kCFStringEncodingUTF8) &&
           strlen(s)>=1 && strlen(s)<=32 && strlen(p)>=8 && strlen(p)<=64) {
            wifi_probe_ssid=CFRetain(ssid);wifi_probe_password=CFRetain(password);
        }
        memset(s,0,sizeof(s));memset(p,0,sizeof(p));
    }
    if(fixture)CFRelease(fixture);
    if(!wifi_probe_ssid) {fprintf(stderr,"Pong WiFi UI test: SKIP invalid private fixture\n");return;}
    wifi_probe_until=clock_gettime_nsec_np(CLOCK_UPTIME_RAW)+180ull*NSEC_PER_SEC;
    dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,6*NSEC_PER_SEC),dispatch_get_main_queue(),NULL,wifi_probe_tick);
}
