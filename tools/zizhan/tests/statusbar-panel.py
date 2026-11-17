#!/usr/bin/env python3
"""Board/realpanel/default-screen provider policy; no UIKit or device claim."""
from pathlib import Path
import subprocess,os,tempfile
R=Path(__file__).resolve().parents[3]
source=r'''
#include <assert.h>
#include <math.h>
#include "zizhan-statusbar18.h"
int main(void){
 assert(zizhan_statusbar_native_pad(1,1,1,2160,1916,2));
 assert(zizhan_statusbar_native_pad(1,1,1,1916,2160,2));
 assert(zizhan_statusbar_native_pad(1,1,0,0,0,0));
 assert(!zizhan_statusbar_native_pad(0,1,0,0,0,0));
 assert(!zizhan_statusbar_native_pad(1,0,0,0,0,0));
 assert(!zizhan_statusbar_native_pad(2,1,0,0,0,0));
 assert(!zizhan_statusbar_native_pad(1,1,1,1080,2412,3));
 assert(!zizhan_statusbar_native_pad(1,1,1,2160,1916,3));
 assert(!zizhan_statusbar_native_pad(1,1,1,2160,1920,2));
 assert(!zizhan_statusbar_native_pad(1,1,1,2160,1916,NAN));
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='zizhan-statusbar-') as d:
 p=Path(d)/'panel.c';p.write_text(source);binary=Path(d)/'panel'
 subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(R/'userland'),str(p),'-o',str(binary)],check=True,timeout=30)
 subprocess.run([str(binary)],check=True,timeout=10)
print('PASS actual panel policy10controls/optin+realboard/nil-screen-without-recursion/scale2/foreignscreen refusal;notUIKit/device')
