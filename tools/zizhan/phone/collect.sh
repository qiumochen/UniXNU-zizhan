O=/data/local/tmp/collect; rm -rf $O; mkdir -p $O/fw
cp /sys/firmware/fdt $O/live.dtb
cat /proc/cmdline > $O/cmdline.txt 2>&1
cat /proc/bootconfig > $O/bootconfig.txt 2>&1
cat /proc/iomem > $O/iomem.txt; cat /proc/cpuinfo > $O/cpuinfo.txt; cat /proc/meminfo > $O/meminfo.txt
cat /proc/interrupts > $O/interrupts.txt
dmesg > $O/dmesg.txt 2>&1
getprop > $O/getprop.txt
uname -a > $O/uname.txt
ls -laR /vendor/firmware /vendor/firmware_mnt/image /odm/firmware /vendor/etc/firmware > $O/fw_list.txt 2>&1
for f in $(ls /vendor/firmware/ | grep -iE 'a7|gmu|sqe|zap|gen7|fts|st_|\.ftb|tp_|touch|wl2866'); do cp /vendor/firmware/$f $O/fw/; done
for f in $(ls /vendor/firmware_mnt/image/ 2>/dev/null | grep -iE 'a7|gmu|zap|gen7'); do cp /vendor/firmware_mnt/image/$f $O/fw/; done
dumpsys display > $O/dumpsys_display.txt 2>&1
dumpsys SurfaceFlinger --display-id > $O/sf_displays.txt 2>&1
ls -la /sys/class/drm/ > $O/drm.txt 2>&1
for d in /sys/class/drm/card0-*; do echo "== $d"; cat $d/status $d/modes 2>/dev/null; done >> $O/drm.txt
cat /sys/kernel/debug/gpu_model /sys/class/kgsl/kgsl-3d0/gpu_model /sys/class/kgsl/kgsl-3d0/gpu_chipid > $O/gpu.txt 2>&1
ls /sys/bus/spi/devices/ /sys/bus/i2c/devices/ > $O/buses.txt 2>&1
for d in /sys/bus/spi/devices/* /sys/bus/i2c/devices/*; do echo "$d $(cat $d/name 2>/dev/null) $(readlink $d/driver)"; done >> $O/buses.txt
cat /sys/kernel/debug/regulator/regulator_summary > $O/regulators.txt 2>&1 || cat /sys/kernel/debug/regulator/supply_map > $O/regulators.txt 2>&1
find /sys/class/input -maxdepth 1 > $O/input.txt; getevent -pl >> $O/input.txt 2>&1
cat /sys/class/power_supply/battery/uevent > $O/battery.txt 2>&1
cd /data/local/tmp && tar czf collect.tgz collect && ls -la collect.tgz
