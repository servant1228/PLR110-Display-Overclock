#!/system/bin/sh
# KernelSU install-time customization
SKIPUNZIP=0

MODDIR=${0%/*}
KO="$MODDIR/system/lib/modules/plr110_display_oc.ko"

ui_print "- PLR110 Display Overclock"
ui_print "- target kernel: $(cat "$MODDIR/system/lib/modules/kernel.release" 2>/dev/null)"
ui_print "- module      : $(basename "$KO")"

if [ ! -f "$KO" ]; then
  abort "! plr110_display_oc.ko missing from package"
fi

set_perm_recursive "$MODDIR" 0 0 0755 0644
set_perm "$KO" 0 0 0644
set_perm "$MODDIR/customize.sh" 0 0 0755
set_perm "$MODDIR/post-fs-data.sh" 0 0 0755
set_perm "$MODDIR/service.sh" 0 0 0755
set_perm "$MODDIR/uninstall.sh" 0 0 0755
set_perm_recursive "$MODDIR/webroot" 0 0 0755 0644

ui_print "- installed (module is inert until enable=1)"
