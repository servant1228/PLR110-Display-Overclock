#!/system/bin/sh
# Runs on module uninstall / disable.
if grep -q '^plr110_display_oc ' /proc/modules; then
  rmmod plr110_display_oc 2>/dev/null
fi
rm -f /data/adb/plr110_display_oc/enable
exit 0
