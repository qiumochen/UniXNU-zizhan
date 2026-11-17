#!/usr/bin/env python3
"""Exercise the actual Weather observation boundary with real ObjC/Blocks ABI."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]


def main():
    source = r'''
#import <Foundation/Foundation.h>
#import <objc/message.h>
#import <objc/runtime.h>
#include <mach/mach_time.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
static const char *app_diag_process="host-test";
static bool app_diag_responds(id value,const char *name) {
    return value && [value respondsToSelector:sel_registerName(name)];
}
static id app_diag_get(id value,const char *name) {
    return app_diag_responds(value,name)?((id(*)(id,SEL))objc_msgSend)(value,sel_registerName(name)):nil;
}
static unsigned observed;
static void app_network_text(id value,const char *field,unsigned sequence) {
    (void)field;(void)sequence;
    // Result/token payloads must never reach the text logger.
    assert(![value isEqual:@"private-token-result"]);++observed;
}
#include "zizhan-weather-observer18.h"
static id wantedSelf,wantedRequest,wantedResult,wantedError;
static SEL wantedSelector;
static unsigned called;
static bool deferred;
static app_weather_completion saved;
static void original(id self,SEL selector,id request,app_weather_completion completion) {
    assert(self==wantedSelf && selector==wantedSelector && request==wantedRequest);++called;
    if(!completion)return;
    if(deferred)saved=[completion copy];else completion(wantedResult,wantedError);
}
int main(void) {@autoreleasepool {
    // Install only compatible methods on the real ObjC runtime, exactly once.
    Class proxy=objc_allocateClassPair([NSObject class],"_TtC13WeatherDaemon23WeatherDataServiceProxy",0);
    assert(proxy);
    SEL forecast=sel_registerName("performRequestsWith:completion:");
    SEL availability=sel_registerName("fetchAvailableDataSetsWith:completion:");
    assert(class_addMethod(proxy,forecast,(IMP)original,"v32@0:8@16@?24"));
    assert(class_addMethod(proxy,availability,(IMP)original,"@32@0:8@16@?24"));
    objc_registerClassPair(proxy);
    app_weather_install((void *)1);
    assert(app_weather_original[0]==original && !app_weather_original[1] && !app_weather_original[2]);
    app_weather_install((void *)2);assert(app_weather_original[0]==original);
    wantedSelf=[[proxy alloc]init];wantedSelector=forecast;wantedRequest=@[@"real-request"];
    wantedResult=@"private-token-result";
    for(unsigned mode=0;mode<2;mode++) {
        wantedError=mode?[NSError errorWithDomain:@"ActualError" code:41 userInfo:nil]:nil;
        for(unsigned async=0;async<2;async++) {
            deferred=async;
            __block unsigned completions=0;
            app_weather_completion completion=^(id result,id error) {
                assert(result==wantedResult && error==wantedError);++completions;
            };
            assert(app_weather_completion_abi(completion));
            ((app_weather_request_imp)objc_msgSend)(wantedSelf,forecast,wantedRequest,completion);
            if(deferred){assert(!completions);saved(wantedResult,wantedError);[saved release];saved=nil;}
            assert(completions==1);
        }
    }
    unsigned logs=observed;
    ((app_weather_request_imp)objc_msgSend)(wantedSelf,forecast,wantedRequest,nil);
    assert(observed==logs && called==5);
    void(^wrong)(id)=^(id value){(void)value;};
    assert(!app_weather_completion_abi((app_weather_completion)wrong));
    assert(!app_weather_completion_abi(nil));
    // A budget-exhausted request still calls the original with the same callback.
    deferred=false;app_weather_sequence=32;
    __block unsigned completionCount=0;
    ((app_weather_request_imp)objc_msgSend)(wantedSelf,forecast,wantedRequest,^(id result,id error){
        assert(result==wantedResult && error==wantedError);++completionCount;
    });
    assert(completionCount==1 && observed==logs && called==6);
    [wantedSelf release];
    // Reference the scheduling entry; device class availability remains unqualified.
    (void)&app_weather_start;
    puts("PASS actual Weather observer: ABI rejection, install-once, request/result/error identity, async/nil callbacks and budget forwarding");
}return 0;}
'''
    with tempfile.TemporaryDirectory(prefix="zizhan-weather-observer-") as temporary:
        directory = Path(temporary)
        code, executable = directory / "observer.m", directory / "observer"
        code.write_text(source)
        subprocess.run(["xcrun", "clang", "-fblocks", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        "-I", str(ROOT / "userland"), "-framework", "Foundation",
                        str(code), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True, timeout=15)


if __name__ == "__main__":
    main()
