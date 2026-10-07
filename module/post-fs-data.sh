#!/system/bin/sh
# Runs in post-fs-data stage (before zygote). Keep it short.
MODDIR=${0%/*}

# nothing to do here yet; the kernel module is loaded in service.sh
exit 0
