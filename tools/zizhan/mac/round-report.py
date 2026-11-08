#!/usr/bin/env python3
"""Summarize one unattended zizhan round from its klog and screenshots.

usage: round-report.py RUN [--logs DIR] [--json OUT] [--ascii N]

Reads DIR/RUN.txt (the klog the kernel copied to the pinned file) and decodes
every PONG SHOT into DIR/RUN.shot<N>.png with shot.py, then prints one
structured report: boot/panic, panel pool and canvas slots, memory failures,
Wi-Fi milestones, app lifecycle, and a per-screenshot content analysis.

The point is to replace ad-hoc grepping after every device round. It never
touches the phone and never writes outside DIR.
"""
import argparse
import json
import re
import struct
import subprocess
import sys
import zlib
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent


def decode_png(path):
    """Minimal PNG reader for the RGB8 non-interlaced files shot.py writes."""
    data = Path(path).read_bytes()
    width, height, depth, colour = struct.unpack('>IIBB', data[16:26])
    if depth != 8 or colour != 2:
        raise ValueError(f'{path}: unsupported PNG depth/colour {depth}/{colour}')
    offset, idat = 8, b''
    while offset < len(data):
        length = struct.unpack('>I', data[offset:offset + 4])[0]
        kind = data[offset + 4:offset + 8]
        if kind == b'IDAT':
            idat += data[offset + 8:offset + 8 + length]
        offset += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 3
    rows, previous, offset = [], bytearray(stride), 0
    for _ in range(height):
        filter_type = raw[offset]
        offset += 1
        line = bytearray(raw[offset:offset + stride])
        offset += stride
        if filter_type == 1:
            for x in range(3, stride):
                line[x] = (line[x] + line[x - 3]) & 255
        elif filter_type == 2:
            for x in range(stride):
                line[x] = (line[x] + previous[x]) & 255
        elif filter_type == 3:
            for x in range(stride):
                left = line[x - 3] if x >= 3 else 0
                line[x] = (line[x] + ((left + previous[x]) >> 1)) & 255
        elif filter_type == 4:
            for x in range(stride):
                left = line[x - 3] if x >= 3 else 0
                up = previous[x]
                corner = previous[x - 3] if x >= 3 else 0
                estimate = left + up - corner
                pa, pb, pc = abs(estimate - left), abs(estimate - up), abs(estimate - corner)
                predictor = left if (pa <= pb and pa <= pc) else (up if pb <= pc else corner)
                line[x] = (line[x] + predictor) & 255
        rows.append(bytes(line))
        previous = line
    return width, height, rows


def analyse_shot(path, ascii_rows=0):
    width, height, rows = decode_png(path)
    pixels = [tuple(rows[y][x * 3:x * 3 + 3]) for y in range(height) for x in range(width)]
    lit = sum(1 for r, g, b in pixels if r + g + b > 30)
    colours = len(set(pixels))
    # A row with high horizontal spread is carrying content (text/icons); a
    # smooth gradient or flat fill is not.
    spread = []
    for y in range(height):
        values = [rows[y][x * 3] + rows[y][x * 3 + 1] + rows[y][x * 3 + 2] for x in range(width)]
        mean = sum(values) / len(values)
        spread.append((sum((v - mean) ** 2 for v in values) / len(values)) ** 0.5)
    busy = [y for y, s in enumerate(spread) if s > 12]
    report = {
        'file': str(path), 'width': width, 'height': height,
        'lit_percent': round(100.0 * lit / len(pixels), 1),
        'unique_colours': colours,
        'content_rows': len(busy),
        'content_row_span': [busy[0], busy[-1]] if busy else None,
        'top_colours': [[list(c), n] for c, n in Counter(pixels).most_common(4)],
    }
    if ascii_rows:
        preview = []
        columns = ascii_rows * 2
        palette = ' .:-=+*#%@'
        for row in range(ascii_rows):
            y = min(height - 1, row * height // ascii_rows)
            line = ''
            for column in range(columns):
                x = min(width - 1, column * width // columns)
                r, g, b = rows[y][x * 3:x * 3 + 3]
                line += palette[min(9, ((r * 299 + g * 587 + b * 114) // 1000) * 10 // 256)]
            preview.append(line)
        report['ascii'] = preview
    return report


def summarize(log_path, ascii_rows):
    text = Path(log_path).read_bytes().decode('latin-1')
    lines = text.splitlines()
    report = {'log': str(log_path), 'bytes': len(text)}

    first = re.search(r'PONG RAM: (\d+) owned banks, (\d+) MiB managed', text)
    if first:
        report['memory_managed_mib'] = int(first.group(2))

    pool = re.search(r'PONG DBUF: panel pool (0x[0-9a-f]+) \((\d+) MiB from the loader\): (\d+) buffers, (\d+)/(\d+) canvas slots', text)
    if pool:
        report['panel_pool'] = {
            'base': pool.group(1), 'mib': int(pool.group(2)),
            'buffers': int(pool.group(3)), 'slots_active': int(pool.group(4)),
            'slots_total': int(pool.group(5)),
        }
    report['slot_exhausted'] = len(re.findall(r'canvas slot NONE FREE', text))
    report['slots_given'] = len(re.findall(r'canvas slot given', text))

    report['surface_failures'] = [int(n) for n in re.findall(r'MiniIOSurface: no memory for (\d+) bytes', text)]
    report['renderer_killed'] = len(re.findall(r'JETSAM_REASON_MEMORY_PERPROCESSLIMIT', text))
    report['renderer_watermarks'] = re.findall(r'process (\S+) \[\d+\] crossed memory high watermark \((\d+) MB\)', text)

    cs4k = re.search(r'PONG CS4K: storage allocation failed kr=(\d+) bytes=(\d+) code-limit=(\d+) pages=(\d+)', text)
    if cs4k:
        report['cs4k'] = {'kr': int(cs4k.group(1)), 'bytes': int(cs4k.group(2)),
                          'code_limit': int(cs4k.group(3)), 'pages': int(cs4k.group(4))}
    map_sizes = re.search(r'PONG CS4K: kernel_map size=(\d+) free=(\d+) largest=(\d+)', text)
    if map_sizes:
        report['cs4k_map'] = {'size': int(map_sizes.group(1)), 'free': int(map_sizes.group(2)),
                              'largest': int(map_sizes.group(3))}
    panic = re.search(r'panic\(cpu \d+[^)]*\):\s*(.+)', text)
    report['panic'] = panic.group(1).strip() if panic else None

    states = re.findall(r'PongWifi: runtime state=(\d+) scans=(\d+) BSS=(\d+) frames=(\d+)', text)
    if states:
        state, scans, bss, frames = (int(v) for v in states[-1])
        report['wifi'] = {'state': state, 'scans': scans, 'bss': bss, 'frames': frames}
    dhcp = re.findall(r'wifi-net: in arp (\d+) ip4 (\d+) dhcp (\d+)', text)
    if dhcp:
        report['wifi_dhcp_in'] = int(dhcp[-1][2])
    report['wifi_arena_failed'] = len(re.findall(r'WiFi wired DMA arena allocation failed', text))

    interfaces = re.findall(r'PONG INTF_(\d) .* frames (\d+)', text)
    if interfaces:
        report['interfaces'] = {int(n): int(f) for n, f in interfaces[-2:]}

    apps = {}
    for bundle in ('com.apple.weather', 'com.apple.mobilesafari', 'com.apple.Preferences', 'com.apple.mobiletimer', 'com.apple.mobileslideshow'):
        spawned = re.findall(rf'UIKitApplication:{re.escape(bundle)}\[[^\]]*\] \[\d+\].*service spawned with pid (\d+)', text)
        foreground = len(re.findall(rf'application session started:{re.escape(bundle)}', text))
        exited = len(re.findall(rf'UIKitApplication:{re.escape(bundle)}.*exited', text))
        if spawned or foreground:
            apps[bundle] = {'spawned': len(spawned), 'foreground_sessions': foreground, 'exits': exited}
    report['apps'] = apps
    report['weatherd'] = len(re.findall(r'weatherd', text))
    report['shots'] = re.findall(r'PONG SHOT (\d+) begin (\d+) (\d+) at (\d+)', text)
    report['shot_skipped'] = re.findall(r'PONG SHOT: SKIP (.+)', text)
    report['lines'] = len(lines)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run')
    parser.add_argument('--logs', type=Path, default=Path('logs'))
    parser.add_argument('--json', type=Path)
    parser.add_argument('--ascii', type=int, default=0, help='rows of ASCII preview per screenshot')
    args = parser.parse_args()

    log = args.logs / f'{args.run}.txt'
    if not log.is_file():
        raise SystemExit(f'no klog at {log}')

    print(f'== {args.run} ==')
    report = summarize(log, args.ascii)
    print(f"log {report['bytes']} bytes, {report['lines']} lines")
    print(f"panic: {report['panic'] or 'none'}")

    pool = report.get('panel_pool')
    if pool:
        print(f"panel pool: {pool['mib']} MiB, {pool['buffers']} buffers, "
              f"{pool['slots_active']}/{pool['slots_total']} canvas slots")
    print(f"canvas: {report['slots_given']} given, {report['slot_exhausted']} NONE FREE")

    surfaces = report['surface_failures']
    print(f"MiniIOSurface failures: {len(surfaces)}"
          + (f" (sizes {sorted(set(surfaces))})" if surfaces else ''))
    print(f"renderer jetsam kills: {report['renderer_killed']}"
          + (f", watermarks {report['renderer_watermarks']}" if report['renderer_watermarks'] else ''))

    if 'cs4k' in report:
        c, m = report['cs4k'], report.get('cs4k_map', {})
        print(f"CS4K: kr={c['kr']} bytes={c['bytes']} code-limit={c['code_limit']} pages={c['pages']}")
        if m:
            print(f"       kernel_map free={m['free']} largest={m['largest']}")
    if 'wifi' in report:
        w = report['wifi']
        print(f"wifi: state={w['state']} scans={w['scans']} BSS={w['bss']} frames={w['frames']}"
              + (f" dhcp={report['wifi_dhcp_in']}" if 'wifi_dhcp_in' in report else '')
              + (', ARENA FAILED' if report['wifi_arena_failed'] else ''))
    if 'interfaces' in report:
        print('interfaces: ' + ', '.join(f'INTF_{n}={f} frames' for n, f in report['interfaces'].items()))
    for bundle, info in report['apps'].items():
        print(f"app {bundle}: spawned={info['spawned']} foreground={info['foreground_sessions']} exits={info['exits']}")
    if report['weatherd'] == 0 and 'com.apple.weather' in report['apps']:
        print('weatherd: never started')

    if report['shot_skipped']:
        for reason in report['shot_skipped']:
            print(f'shot skipped: {reason}')

    out_dir = args.logs
    subprocess.run([sys.executable, str(HERE / 'shot.py'), str(log), str(out_dir)], check=False,
                   stdout=subprocess.DEVNULL)
    shots = []
    for path in sorted(out_dir.glob(f'{args.run}.shot*.png')):
        shot = analyse_shot(path, args.ascii)
        shots.append(shot)
        span = shot['content_row_span']
        print(f"{path.name}: {shot['width']}x{shot['height']} lit={shot['lit_percent']}% "
              f"colours={shot['unique_colours']} content_rows={shot['content_rows']}"
              + (f" span={span[0]}..{span[1]}" if span else ''))
        for line in shot.get('ascii', []):
            print('  |' + line)
    report['shots_analysis'] = shots

    if args.json:
        args.json.write_text(json.dumps(report, indent=2) + '\n')
        print(f'json -> {args.json}')


if __name__ == '__main__':
    main()
