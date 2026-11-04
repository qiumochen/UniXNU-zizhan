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
# 2026-11-04: rear USB-A VBUS is shared by a USB2/USB3 companion pair.
# uhubctl identifies the pair; require the same physical port on both,
# and refuse unexpected hubs or an occupied companion before cycling.
kick() {
  local hp hub port extra line status lines
  hp=$(find_port)
  [ -z "$hp" ] || echo "$hp" > "$F"
  [ -f "$F" ] || { echo "$(ts) usbkick: no recorded port (run 'usbkick.sh remember' while the phone is visible)"; return 1; }
  read -r hub port extra < "$F"
  [[ "$hub" =~ ^[0-9]+-[0-9]+(\.[0-9]+)*$ && "$port" =~ ^[1-9][0-9]*$ && -z "$extra" ]] || return 1
  status=$("$UHUBCTL_BIN" -l "$hub" -p "$port" 2>/dev/null) || return $?
  lines=$(printf '%s\n' "$status" | awk -v want="$hub" -v p="$port:" '
    /^Current status for hub/ {
      h=$5; id[h]=$6; hubs[h]++; n++
      if (h!=want) other=h
      s=$0; sub(/^.* Hub /,"",s); sub(/,.*/,"",s); serial[h]=s
    }
    $1=="Port" { if ($2!=p || !h) bad=1; ports[h]++; print }
    END {
      if (bad || n<1 || n>2 || hubs[want]!=1 || ports[want]!=1) exit 1
      if (n==2 && (ports[other]!=1 ||
          substr(want,1,index(want,"-")-1)!=substr(other,1,index(other,"-")-1) ||
          serial[want]!=serial[other] || !serial[want] ||
          !((id[want]=="[05ac:8009" && id[other]=="[05ac:800a") ||
            (id[want]=="[05ac:800a" && id[other]=="[05ac:8009")))) exit 1
    }') || { echo "$(ts) usbkick: recorded hub/port or companion unavailable"; return 1; }
  while IFS= read -r line; do
    if [[ "$line" =~ \[[[:xdigit:]]{4}:[[:xdigit:]]{4} ]] &&
       ! [[ "$line" =~ \[$PHONE_VID:(4ee7|d00d)([[:space:]]|\]) ]]; then
      echo "$(ts) usbkick: refusing occupied hub $hub port $port or companion"
      return 1
    fi
  done <<< "$lines"
  "$UHUBCTL_BIN" -l "$hub" -p "$port" -a cycle -d 3 >/dev/null || return $?
  # macOS can retain the old USB object after uhubctl returns. On g279
  # it detached 1.3 s later, interrupting a transfer to that old object.
  sleep 3
  echo "$(ts) usbkick: power-cycled hub $hub port $port"
}
case ${1:-kick} in
  remember) remember ;;
  kick) kick ;;
  status) "$UHUBCTL_BIN" ;;
  *) echo "usage: usbkick.sh remember|kick|status"; exit 2 ;;
esac
