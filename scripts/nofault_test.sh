#!/system/bin/sh
# Self-test for the fault-safe read primitive. Everything runs on the device
# and lands in a file, so a USB drop cannot lose the result.
KO=/data/local/tmp/plr110_display_oc.ko
LOG=/data/local/tmp/nofault.log

exec > "$LOG" 2>&1

echo "### started $(date)"
echo "### uptime before: $(cat /proc/uptime)"
md5sum "$KO"

rmmod plr110_display_oc 2>/dev/null
dmesg -c > /dev/null

insmod "$KO" enable=0 dump_modes=1 scan_structs=1
echo "### insmod rc=$?"

sleep 2
input keyevent 26
sleep 4
input keyevent 26
sleep 6

echo "### uptime after: $(cat /proc/uptime)"
echo "### module loaded: $(grep -c '^plr110_display_oc ' /proc/modules)"

echo "### nofault self-test output"
dmesg | grep -a plr110_display_oc \
  | grep -aE "nofault|self-test" | head -20

echo "### panel / priv lines"
dmesg | grep -a plr110_display_oc \
  | grep -aE "panel->name|full mode|priv scan" | head -20

echo "### done"
