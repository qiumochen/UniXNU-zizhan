#!/system/bin/sh
# zizhan THERM-01: stream read-only observations to the host; no device files.
# Run through bounded adb shell -T [su -c] sh -s; stdout uses NUL-delimited fields.
set -u

read_field() {
    # These sysfs attributes are single-line. Builtin read avoids launching a
    # separate cat/subshell for each of the board's many trip/cooling fields.
    value=
    if { IFS= read -r value < "$1"; } 2>/dev/null || [ -n "$value" ]; then
        printf 'OK\000%s\000%s\000' "$1" "$value"
    else
        printf 'ERROR\000%s\000\000' "$1"
    fi
}

printf 'BEGIN\000'
read_field /proc/uptime
for directory in /sys/class/thermal/thermal_zone*; do
    [ -d "$directory" ] || continue
    for field in type temp mode policy; do
        read_field "$directory/$field"
    done
    for field in "$directory"/trip_point_*_temp "$directory"/trip_point_*_hyst \
                 "$directory"/trip_point_*_type; do
        [ -e "$field" ] && read_field "$field"
    done
done
for directory in /sys/class/thermal/cooling_device*; do
    [ -d "$directory" ] || continue
    for field in type cur_state max_state; do
        read_field "$directory/$field"
    done
done
for directory in /sys/devices/system/cpu/cpufreq/policy*; do
    [ -d "$directory" ] || continue
    for field in related_cpus affected_cpus scaling_governor scaling_min_freq \
                 scaling_max_freq scaling_cur_freq cpuinfo_min_freq \
                 cpuinfo_max_freq cpuinfo_cur_freq scaling_available_frequencies; do
        read_field "$directory/$field"
    done
done
for field in temp status capacity voltage_now current_now charge_full \
             charge_full_design cycle_count; do
    read_field "/sys/class/power_supply/battery/$field"
done
printf 'END\000'
read_field /proc/uptime
