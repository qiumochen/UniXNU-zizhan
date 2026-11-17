#!/usr/bin/env python3
"""Exercise actual opt-in URL token and HID usage parsing without a device."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]


def main():
    text = (ROOT / 'userland/zizhan-keyboard-test18.h').read_text()
    actual = text[text.index('static unsigned app_ascii_usage'):text.index('static void *app_keyboard_thread')]
    checks = r'''
int main(void){
 bool shift;char value[160];
 assert(app_ascii_usage('a',&shift)==4&&!shift);
 assert(app_ascii_usage('A',&shift)==4&&shift);
 assert(app_ascii_usage(':',&shift)==51&&shift);
 assert(app_ascii_usage('/',&shift)==56&&!shift);
 assert(app_ascii_usage(0x80,&shift)==0);
 assert(app_type_option("x=1 zizhan_type_url=https://example.com/ end=1","zizhan_type_url=",value,sizeof value));
 assert(!strcmp(value,"https://example.com/"));
 assert(!app_type_option("xxzizhan_type_url=https://bad","zizhan_type_url=",value,sizeof value));
 assert(!app_type_option("zizhan_type_url=https://example.com/","zizhan_type_url=",value,5));
 assert(!app_type_option("zizhan_type_url=","zizhan_type_url=",value,sizeof value));
 puts("PASS actual URL token bounds and keyboard usage/modifier parsing; no device input claim");
 return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='zizhan-keyboard-') as temporary:
        source, executable = Path(temporary) / 'keys.c', Path(temporary) / 'keys'
        source.write_text('#include <stdbool.h>\n#include <stddef.h>\n#include <stdint.h>\n'
                          '#include <string.h>\n#include <assert.h>\n#include <stdio.h>\n' + actual + checks)
        subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        str(source), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True, timeout=10)


if __name__ == '__main__':
    main()
