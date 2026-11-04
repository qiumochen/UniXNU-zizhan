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
UHUBCTL_BIN=${UHUBCTL_BIN:-uhubctl}
ts() { date '+%H:%M:%S'; }

# Prints "HUB PORT" for the phone's USB identity on a controllable hub.
find_port() {
  "$UHUBCTL_BIN" 2>/dev/null | awk -v vid="$PHONE_VID" '
    /^Current status for hub/ { hub=$5 }
    /^  Port [0-9]+:/ && ($0 ~ "\\[" vid ":(4ee7|d00d)([[:space:]]|\\])") {
      sub(":", "", $2); print hub, $2; exit }'
}
remember() {
  local hp; hp=$(find_port)
  # Explicit remember is called while Android is visible. If this is a
  # direct port, invalidate any location left by an earlier cable setup.
  [ -n "$hp" ] || { rm -f "$F"; return 1; }
  echo "$hp" > "$F"; echo "$(ts) phone on hub ${hp% *} port ${hp#* }"
}
# 2026-11-04: the exact rear USB2 hub port removes VBUS and fastboot
# re-enumerates. Leave sibling hubs and other devices alone.
kick() {
  local hp hub port extra line
  hp=$(find_port)
  [ -z "$hp" ] || echo "$hp" > "$F"
  [ -f "$F" ] || { echo "$(ts) usbkick: no recorded port (run 'usbkick.sh remember' while the phone is visible)"; return 1; }
  read -r hub port extra < "$F"
  [[ "$hub" =~ ^[0-9]+-[0-9]+(\.[0-9]+)*$ && "$port" =~ ^[1-9][0-9]*$ && -z "$extra" ]] || return 1
  line=$("$UHUBCTL_BIN" -e -l "$hub" -p "$port" 2>/dev/null | awk -v p="$port:" '$1=="Port" && $2==p {print; exit}')
  [ -n "$line" ] || { echo "$(ts) usbkick: recorded hub/port unavailable"; return 1; }
  if [[ "$line" =~ \[[[:xdigit:]]{4}:[[:xdigit:]]{4} ]] &&
     ! [[ "$line" =~ \[$PHONE_VID:(4ee7|d00d)([[:space:]]|\]) ]]; then
    echo "$(ts) usbkick: refusing occupied hub $hub port $port"
    return 1
  fi
  "$UHUBCTL_BIN" -e -l "$hub" -p "$port" -a cycle -d 3 >/dev/null || return $?
  echo "$(ts) usbkick: power-cycled hub $hub port $port"
}
case ${1:-kick} in
  remember) remember ;;
  kick) kick ;;
  status) "$UHUBCTL_BIN" ;;
  *) echo "usage: usbkick.sh remember|kick|status"; exit 2 ;;
esac
