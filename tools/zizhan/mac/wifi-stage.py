#!/usr/bin/env python3
"""Summarize observed QCA6490 milestones in a retrieved XNU log.

Usage: wifi-stage.py LOG. Output is a local triage aid, not a device PASS.
Milestones are literal messages emitted by PongWifiPlatform.cpp/PongWifiNet.cpp.
"""
from pathlib import Path
import re
import sys


MILESTONES = (
    ('rails', 'SM8475 WLAN rail requests acknowledged'),
    ('pcie', 'QCA6490 PCIe0 link and BAR established'),
    ('dma', 'exclusive SID1c01 native4K DMA and retained MSI landing page established'),
    ('msi_armed', 'DesignWare MSI receiver armed on Pong PCIe0 SPI140'),
    ('firmware_file', 'PongWifi: firmware '),
    ('msi_seen', 'QCA6490 MSI delivered by GIC'),
    ('station', 'QCA6490 station ready; starting actual scan'),
    ('scan_result', 'wifi-net: scan result '),
)
START = 'PCIe0 cold start; power and bus clocks precede host MMIO'
ERROR = re.compile(r'PongWifi\[3\]: (.+)|PongWifi: (ath11k start failed=.+|owned SMMU fault=.+)|PongWifi\[4\]: (ath11k: firmware crashed: .+)')
SUBSYSTEM = re.compile(r'PongWifi: QCA6490 PCI subsystem=([0-9a-fA-F]{1,8})(?![0-9A-Za-z])')
SCAN_FINISHED = re.compile(r'\bwifi: scan \d+ finished, ([0-9]+) networks\b')
TERMINAL = 'WiFi initialization stopped; normal iOS boot continues, DMA retained'
HTTPS_PASS = re.compile(r'^Pong WiFi18: HTTPS test PASS status=(2\d\d) bytes=([1-9]\d*) error=0(?: attempt=\d+)?$')
NETWORK_RESULTS = {
    'Pong WiFi18: DHCP renewal ACK PASS': 'renewal_ack',
    'Pong WiFi18: disconnect cleanup PASS': 'disconnect_cleanup',
    'Pong WiFi18: network lifecycle PASS': 'lifecycle',
    'PongWifi[6]: WiFi lifecycle test AP switch PASS: associated to distinct BSSID': 'distinct_ap',
}


def summarize(raw):
    lines = raw.decode('latin-1', errors='replace').splitlines()
    # A ramoops zone can contain more than one attempted boot. Only summarize
    # the last cold-start sequence when that boundary is present.
    starts = [i for i, line in enumerate(lines) if START in line]
    offset = starts[-1] if starts else 0
    observed = []
    failure = None
    subsystem = None
    wifi_lines = 0
    stopped = False
    for number, line in enumerate(lines[offset:], offset + 1):
        if 'PongWifi' not in line and 'wifi-net:' not in line:
            continue
        wifi_lines += 1
        if TERMINAL in line:
            stopped = True
        match = SUBSYSTEM.search(line)
        if match:
            subsystem = (number, int(match.group(1), 16))
        for label, message in MILESTONES:
            if message in line and label not in {item[0] for item in observed}:
                observed.append((label, number))
        scan = SCAN_FINISHED.search(line)
        if scan and int(scan.group(1)) > 0 and 'scan_bss' not in {item[0] for item in observed}:
            observed.append(('scan_bss', number))
        match = ERROR.search(line)
        if match and TERMINAL not in line and failure is None:
            detail = next(group for group in match.groups() if group).strip()
            failure = (number, detail[:200])
    if not wifi_lines:
        status = 'NO_WIFI_LOG'
    elif failure:
        status = 'ERROR_OBSERVED'
    elif stopped:
        status = 'STOPPED_WITHOUT_ERROR_LOGGED'
    else:
        status = 'MILESTONES_ONLY'
    return status, observed, failure, offset + 1 if starts else None, subsystem


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    raw = Path(sys.argv[1]).read_bytes()
    status, observed, failure, start, subsystem = summarize(raw)
    print(f'Wi-Fi log status: {status}')
    if start:
        print(f'last cold-start begins at line {start}')
    print('observed: ' + (', '.join(f'{label}@{line}' for label, line in observed) if observed else 'none'))
    if subsystem:
        print(f'PCI subsystem: 0x{subsystem[1]:08x} at line {subsystem[0]}')
    if failure:
        print(f'first logged error: line {failure[0]}: {failure[1]}')
    tail = raw.decode('latin-1').splitlines()[(start or 1) - 1:]
    https = sum(HTTPS_PASS.fullmatch(line) is not None for line in tail)
    network = dict.fromkeys(NETWORK_RESULTS[line] for line in tail if line in NETWORK_RESULTS)
    if https or network:
        print(f'logged HTTPS successes: {https}')
        print('logged network checks: ' + (', '.join(network) if network else 'none'))
    print('Use the retrieved device log to confirm the boot and each test result.')


if __name__ == '__main__':
    main()
