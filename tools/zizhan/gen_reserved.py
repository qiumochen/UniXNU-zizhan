#!/usr/bin/env python3
"""Print memory + static no-map reserved-memory regions of a decompiled live DTS (dtc -O dts)."""
import re, sys

text = open(sys.argv[1]).read()

def cells(s):
    return [int(x, 16) for x in re.findall(r'0x[0-9a-f]+', s)]

def pairs(c):
    return [((c[i] << 32) | c[i + 1], (c[i + 2] << 32) | c[i + 3]) for i in range(0, len(c), 4)]

mem = re.search(r'device_type = "memory";\s*reg = <([^>]*)>', text)
print('memory:', ', '.join(f'<{a:#x} {s:#x}>' for a, s in sorted(pairs(cells(mem.group(1))))))
start = text.index('reserved-memory {')
depth, i = 0, start
while True:
    if text[i] == '{': depth += 1
    elif text[i] == '}':
        depth -= 1
        if depth == 0: break
    i += 1
body = text[start:i]
regions = []
for m in re.finditer(r'\n\t\t([^\n{]+?) \{(.*?)\n\t\t\};', body, re.S):
    name, node = m.group(1).strip(), m.group(2)
    r = re.search(r'\n\t\t\treg = <([^>]*)>;', node)
    if not r or 'no-map;' not in node:
        continue
    for a, s in pairs(cells(r.group(1))):
        regions.append((a, s, name))
for a, s, n in sorted(regions):
    print(f'{a:#012x} {s:#010x} {n}')
