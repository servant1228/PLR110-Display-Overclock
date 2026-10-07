#!/system/bin/sh
# Runs late_start service stage.
MODDIR=${0%/*}
KO="$MODDIR/system/lib/modules/plr110_display_oc.ko"
STATE=/data/adb/plr110_display_oc
mkdir -p "$STATE"
LOG="$STATE/module.log"

log() { echo "$(date '+%F %T') $*" >> "$LOG"; }

# User switches are stored as files so the WebUI can flip them without a module
# rebuild. Defaults: module inert.
[ -f "$STATE/enable" ]    || echo 0   > "$STATE/enable"
[ -f "$STATE/target_hz" ] || echo 185 > "$STATE/target_hz"
[ -f "$STATE/dump_modes" ] || echo 1  > "$STATE/dump_modes"

ENABLE=$(cat "$STATE/enable")
TARGET=$(cat "$STATE/target_hz")
DUMP=$(cat "$STATE/dump_modes")

if [ "$ENABLE" != "1" ]; then
  log "disabled (enable=$ENABLE) - not loading ko"
  exit 0
fi

# wait for the display driver to be up
i=0
while [ $i -lt 60 ]; do
  [ -d /sys/kernel/oplus_display ] && break
  sleep 1
  i=$((i+1))
done

if grep -q '^plr110_display_oc ' /proc/modules; then
  log "already loaded"
  exit 0
fi

log "insmod $KO enable=1 target_hz=$TARGET dump_modes=$DUMP"
insmod "$KO" enable=1 target_hz="$TARGET" dump_modes="$DUMP" >> "$LOG" 2>&1
log "insmod rc=$?"
