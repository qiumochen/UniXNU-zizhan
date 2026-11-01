#!/bin/bash
# Mac: power-cycle the phone's USB port from the host (docs/progress/2026-10-31-frame-timeline.md §8).
# Why: on this Mac Studio the phone's first USB attach after `adb reboot bootloader` always fails at the internal Apple USB2 hub
# ("AppleUSB20HubPort::resetAndCreateDevice: reset did not enable port", 11/11 transitions on 2026-10-31); normally the bootloader
# re-attaches 3 s later and enumerates, but about one time in ten it does not (g146, g148, 10-30 20:28) and the phone sits in fastboot
# invisible until the cable is replugged. The Apple hubs support per-port power switching (uhubctl lists them "ppps"), and a 3 s VBUS
# drop makes the bootloader attach again (16:48: enumerated 6 s after the cycle). The standard remote-lab trick (uhubctl README).
#   usbkick.sh remember   while the phone is visible: record its hub/port in logs/phone-port.txt
#   usbkick.sh kick       power-cycle the recorded port (re-recording first if the phone is visible); exit 1 if nothing recorded
#   usbkick.sh status     show uhubctl's view of the hubs
set -u
export PATH=/opt/homebrew/bin:$PATH
F=${ZIZHAN_SRC:-$HOME/zizhan}/logs/phone-port.txt
PHONE_VID=${PHONE_VID:-18d1}   # Google's VID: 18d1:4ee7 adb (Xiaomi MIX Fold 2), 18d1:d00d fastboot
ts() { date '+%H:%M:%S'; }

# Prints "HUB PORT" for the port that currently has the phone, from uhubctl's status listing.
find_port() {
  uhubctl 2>/dev/null | awk -v vid="$PHONE_VID" '
    /^Current status for hub/ { hub=$5 }
    /^  Port [0-9]+:/ && index($0, "[" vid ":") { sub(":", "", $2); print hub, $2; exit }'
}
remember() {
  local hp; hp=$(find_port)
  [ -n "$hp" ] || return 1
  echo "$hp" > $F; echo "$(ts) phone on hub ${hp% *} port ${hp#* }"
}
# Cycles the recorded port, plus the same port of the sibling hub on the same bus (a USB-C connector's USB2 and USB3 lanes sit on two
# internal hubs, 8-3 and 8-1 here, and VBUS is switched by whichever owns it).
kick() {
  remember >/dev/null 2>&1
  [ -f $F ] || { echo "$(ts) usbkick: no recorded port (run 'usbkick.sh remember' while the phone is visible)"; return 1; }
  read -r hub port < $F
  local bus=${hub%%-*} h
  for h in $(uhubctl 2>/dev/null | awk -v bus="$bus" '/^Current status for hub/ && index($5, bus "-") == 1 { print $5 }'); do
    uhubctl -l "$h" -p "$port" -a cycle -d 3 >/dev/null 2>&1
  done
  echo "$(ts) usbkick: power-cycled hub $hub (bus $bus siblings) port $port"
}
case ${1:-kick} in
  remember) remember ;;
  kick) kick ;;
  status) uhubctl ;;
  *) echo "usage: usbkick.sh remember|kick|status"; exit 2 ;;
esac
