#!/system/bin/sh
# One-shot priv_info discovery; everything happens in a single adb round trip
# because the USB link keeps dropping.
KO=/data/local/tmp/plr110_display_oc.ko

echo "### md5"; md5sum "$KO"
echo "### reload"
rmmod plr110_display_oc 2>/dev/null
dmesg -c > /dev/null
insmod "$KO" enable=0 dump_modes=1 scan_structs=1
echo "insmod rc=$?"

echo "### force mode revalidation"
input keyevent 26
sleep 4
input keyevent 26
sleep 6

echo "### results"
dmesg | grep plr110_display_oc \
  | grep -aE "full mode|mode\+0x|priv scan|priv_info=|priv\+|phy165|validate_mode|connector|modes\[" \
  | head -60

echo "### done"
