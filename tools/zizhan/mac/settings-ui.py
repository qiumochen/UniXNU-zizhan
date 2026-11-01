#!/usr/bin/env python3
"""Mac: print the Settings UI dumps of a round (settings_compat18.c ui_dump, rounds with boot-arg pong_url), decoding the \\xHH escapes
back to UTF-8. usage: settings-ui.py LOG [PASS]"""
import re, sys
t = open(sys.argv[1], 'rb').read().decode('latin-1')
want = sys.argv[2] if len(sys.argv) > 2 else None
for m in re.finditer(r'Pong Settings18 ui (\d+): ([^\n]*)', t):
    if want and m.group(1) != want:
        continue
    raw = re.sub(r'\\x([0-9a-f]{2})', lambda h: chr(int(h.group(1), 16)), m.group(2))
    print(f'{m.group(1)}: {raw.encode("latin-1", "replace").decode("utf-8", "replace")}')
