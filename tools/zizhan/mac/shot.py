#!/usr/bin/env python3
"""Decode PONG SHOT log lines into PNGs. Usage: shot.py LOG [OUTDIR].

Current kernels print each row in 32-pixel chunks to stay under IOLog's line
limit. Older logs contain a truncated whole-row line; crop those screenshots to
the pixels actually logged instead of silently filling the missing area black.
"""
import os
import re
import struct
import sys
import zlib


def write_png(path, width, height, rows):
    raw = b''.join(b'\0' + row for row in rows)

    def chunk(kind, data):
        return (struct.pack('>I', len(data)) + kind + data +
                struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff))

    with open(path, 'wb') as output:
        output.write(b'\x89PNG\r\n\x1a\n' +
                     chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def decode(log_path, out_dir):
    text = open(log_path, 'rb').read().decode('latin-1')
    shots = {}
    for number, width, height, at in re.findall(r'PONG SHOT (\d+) begin (\d+) (\d+) at (\d+)', text):
        shots[int(number)] = dict(width=int(width), height=int(height), at=int(at), rows={})

    # New format has a pixel-column offset. Legacy lines have no offset and
    # normally end at IOLog's 255-byte limit, midway through the row.
    for match in re.finditer(r'PONG SHOT (\d+) (\d+) (?:(\d+) )?([0-9a-f]+)', text):
        number, y, x, hex_data = match.groups()
        number, y = int(number), int(y)
        shot = shots.get(number)
        if shot is None or y >= shot['height']:
            continue
        if x is None:
            shot['legacy'] = True
        x = int(x) if x is not None else 0
        pixel_count = len(hex_data) // 6  # discard an incomplete trailing RGB pixel
        if not pixel_count or x >= shot['width'] or x + pixel_count > shot['width']:
            continue
        data = bytes.fromhex(hex_data[:pixel_count * 6])
        row = shot['rows'].setdefault(y, bytearray(shot['width'] * 3))
        coverage = shot.setdefault('coverage', {}).setdefault(y, bytearray(shot['width']))
        for pixel in range(pixel_count):
            target = x + pixel
            if coverage[target] and row[target * 3:target * 3 + 3] != data[pixel * 3:pixel * 3 + 3]:
                shot['conflict'] = True
            row[target * 3:target * 3 + 3] = data[pixel * 3:pixel * 3 + 3]
            coverage[target] = 1

    os.makedirs(out_dir, exist_ok=True)
    stem = os.path.splitext(os.path.basename(log_path))[0]
    for number, shot in sorted(shots.items()):
        width, height = shot['width'], shot['height']
        if len(shot['rows']) != height or shot.get('conflict'):
            print(f'shot {number}: incomplete or conflicting rows; no PNG written')
            continue
        # Every row must have a contiguous prefix of real data. For legacy
        # logs this is usually 39 or 40 of 135 samples.
        captured_width = min(next((x for x, used in enumerate(shot['coverage'][y]) if not used), width)
                             for y in range(height))
        if captured_width == 0:
            print(f'shot {number}: no complete pixel columns; no PNG written')
            continue
        scale = 3
        rows = []
        for y in range(height):
            row = shot['rows'][y]
            wide = b''.join(row[x * 3:x * 3 + 3] * scale for x in range(captured_width))
            rows.extend([wide] * scale)
        path = os.path.join(out_dir, f'{stem}.shot{number}.png')
        write_png(path, captured_width * scale, height * scale, rows)
        reason = 'old IOLog truncation' if shot.get('legacy') else 'missing log chunks'
        note = 'complete' if captured_width == width else f'cropped to {captured_width}/{width} columns ({reason})'
        print(f'shot {number} at {shot["at"] / 1e6:.3f} s: {height}/{height} rows, {note} -> {path}')


if __name__ == '__main__':
    if len(sys.argv) not in (2, 3):
        raise SystemExit(__doc__)
    decode(sys.argv[1], sys.argv[2] if len(sys.argv) == 3 else os.path.dirname(os.path.abspath(sys.argv[1])))
