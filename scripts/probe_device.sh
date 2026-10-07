#!/system/bin/sh
# push to /data/local/tmp/probe_device.sh and run with: su -c sh /data/local/tmp/probe_device.sh
echo "############ 1. device-tree top ############"
ls /proc/device-tree/
echo
echo "############ 2. dirs containing 'panel' ############"
find /proc/device-tree -type d 2>/dev/null | grep -i panel | head -40
echo
echo "############ 3. dirs containing 'dsi' ############"
find /proc/device-tree -type d 2>/dev/null | grep -i dsi | head -40
echo
echo "############ 4. soc children ############"
ls /proc/device-tree/soc*/ 2>/dev/null | head -60
echo
echo "############ 5. oplus,panel / panel-name strings anywhere shallow ############"
for p in /proc/device-tree/soc*; do
  echo "--- $p"
  ls "$p" 2>/dev/null | head -5
done
echo
echo "############ 6. modules importing register_kprobe ############"
grep -rl register_kprobe /vendor_dlkm/lib/modules /vendor/lib/modules /system/lib/modules 2>/dev/null | head -20
echo
echo "############ 7. modules importing kallsyms_lookup_name ############"
grep -rl kallsyms_lookup_name /vendor_dlkm/lib/modules /vendor/lib/modules /system/lib/modules 2>/dev/null | head -20
echo
echo "############ 8. symvers-relevant: modules importing drm_mode_probed_add ############"
grep -rl drm_mode_probed_add /vendor_dlkm/lib/modules /vendor/lib/modules 2>/dev/null | head -10
echo
echo "############ 9. protected module names list ############"
find / -name "protected_module_names_list*" 2>/dev/null | head -5
