#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Build plr110_display_oc.ko out-of-tree against the OnePlus SM8845 vendor kernel.
#
# Required env:
#   PLR110_KERNEL_SRC   vendor kernel tree (android_kernel_oneplus_sm8845)
#   PLR110_CLANG        path to clang (Android Clang r536225)
# Optional env:
#   PLR110_DISPLAY_SRC  display-drivers source (for headers)
#   PLR110_OUT          output dir (default: ./out)
#   PLR110_VERMAGIC_FILE  file containing the target kernel release string
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src"
OUT="${PLR110_OUT:-$ROOT/out}"
KERNEL_SRC="${PLR110_KERNEL_SRC:?PLR110_KERNEL_SRC is required}"
FALLBACK_KERNEL_SRC="${PLR110_FALLBACK_KERNEL_SRC:-}"
CLANG="${PLR110_CLANG:?PLR110_CLANG is required}"
DISPLAY_SRC="${PLR110_DISPLAY_SRC:-}"
VERMAGIC_FILE="${PLR110_VERMAGIC_FILE:-$ROOT/configs/kernel.release}"

CLANG_DIR="$(cd "$(dirname "$CLANG")" && pwd)"
mkdir -p "$OUT"
LOG="$OUT/build.log"
: > "$LOG"

log() { echo -e "\n\033[1;36m>>> $*\033[0m" | tee -a "$LOG"; }

# ---------------------------------------------------------------- sanity ----
log "toolchain"
"$CLANG" --version | tee -a "$LOG"
CVER="$("$CLANG" --version | head -1)"
case "$CVER" in
  *"19.0.1"*) : ;;
  *) echo "::warning::unexpected clang version: $CVER (expect Clang 19.0.1 / r536225)" | tee -a "$LOG" ;;
esac

log "kernel tree: $KERNEL_SRC"
test -f "$KERNEL_SRC/Makefile" || { echo "::error::not a kernel tree"; exit 1; }
grep -E "^(VERSION|PATCHLEVEL|SUBLEVEL)" "$KERNEL_SRC/Makefile" | tee -a "$LOG"

# ----------------------------------------------------------------- config ---
# Device config wins; fall back to whatever the vendor tree ships.
CFG="$ROOT/configs/device.config"
if [ ! -f "$CFG" ]; then
  CFG="$ROOT/configs/device.config.gz"
fi
if [ -f "$CFG" ]; then
  log "using device config: $CFG"
else
  log "no configs/device.config* found - will use vendor tree default"
fi

make_args() {
  echo -C "$1" O="$OUT" ARCH=arm64 LLVM=1 CC="$CLANG" \
       LD="${CLANG_DIR}/ld.lld" HOSTCC=gcc HOSTCXX=g++
}

# --------------------------------------------------------------- prepare ----
prepare_tree() {
  local tree="$1"

  mkdir -p "$OUT"
  if [ -f "$CFG" ]; then
    case "$CFG" in
      *.gz) zcat "$CFG" > "$OUT/.config" ;;
      *)    cp "$CFG" "$OUT/.config" ;;
    esac
    make -C "$tree" O="$OUT" ARCH=arm64 LLVM=1 CC="$CLANG" \
         LD="${CLANG_DIR}/ld.lld" HOSTCC=gcc HOSTCXX=g++ \
         olddefconfig 2>&1 | tee -a "$LOG"
  else
    make -C "$tree" O="$OUT" ARCH=arm64 LLVM=1 CC="$CLANG" \
         LD="${CLANG_DIR}/ld.lld" HOSTCC=gcc HOSTCXX=g++ \
         defconfig 2>&1 | tee -a "$LOG"
  fi

  make -C "$tree" O="$OUT" ARCH=arm64 LLVM=1 CC="$CLANG" \
       LD="${CLANG_DIR}/ld.lld" HOSTCC=gcc HOSTCXX=g++ \
       modules_prepare 2>&1 | tee -a "$LOG"
}

log "modules_prepare (vendor tree: $KERNEL_SRC)"
if ! prepare_tree "$KERNEL_SRC"; then
  if [ -n "$FALLBACK_KERNEL_SRC" ] && [ -f "$FALLBACK_KERNEL_SRC/Makefile" ]; then
    log "::warning::vendor tree prepare failed -> falling back to $FALLBACK_KERNEL_SRC"
    rm -rf "$OUT"
    mkdir -p "$OUT"
    KERNEL_SRC="$FALLBACK_KERNEL_SRC"
    prepare_tree "$KERNEL_SRC"
  else
    echo "::error::modules_prepare failed and no usable fallback tree"
    exit 1
  fi
fi

COMMON_ARGS=(-C "$KERNEL_SRC" O="$OUT" ARCH=arm64 LLVM=1 CC="$CLANG"
              LD="${CLANG_DIR}/ld.lld" HOSTCC=gcc HOSTCXX=g++)

# ---------------------------------------------------------- kernel.release --
# The published source (6.12.38) is older than the target device (6.12.69).
# vermagic must match the running kernel exactly or insmod is rejected.
if [ -f "$VERMAGIC_FILE" ]; then
  REL="$(tr -d '\r\n' < "$VERMAGIC_FILE")"
  log "forcing kernel.release = $REL"
  echo "$REL" > "$OUT/include/config/kernel.release"
  echo "$REL" > "$OUT/include/generated/utsrelease.h.tmp" 2>/dev/null || true
  if [ -f "$OUT/include/generated/utsrelease.h" ]; then
    sed -i "s/^#define UTS_RELEASE.*/#define UTS_RELEASE \"$REL\"/" \
      "$OUT/include/generated/utsrelease.h"
  fi
  grep -H UTS_RELEASE "$OUT/include/generated/utsrelease.h" | tee -a "$LOG"
else
  echo "::warning::$VERMAGIC_FILE missing - vermagic will NOT match device" | tee -a "$LOG"
fi

# --------------------------------------------------------------- symvers ----
# modversions kernels require a CRC entry (__versions) for every imported
# symbol, including module_layout. We take those from the device itself.
SYMVERS="$ROOT/configs/Module.symvers"
if [ -f "$SYMVERS" ]; then
  cp "$SYMVERS" "$OUT/Module.symvers"
  log "installed Module.symvers: $(wc -l < "$SYMVERS") entries"
else
  echo "::warning::configs/Module.symvers missing; using tree's (possibly empty) symvers" | tee -a "$LOG"
fi

# ---------------------------------------------------------------- module ----
log "building module against $KERNEL_SRC"
make "${COMMON_ARGS[@]}" \
  M="$SRC" \
  PLR110_DISPLAY_SRC="$DISPLAY_SRC" \
  KBUILD_EXTRA_SYMBOLS="$OUT/Module.symvers" \
  modules 2>&1 | tee -a "$LOG"

KO="$(find "$SRC" -maxdepth 1 -name '*.ko' | head -1)"
test -n "$KO" || { echo "::error::no .ko produced"; exit 1; }
cp "$KO" "$OUT/" 
log "built: $OUT/$(basename "$KO")"
ls -la "$OUT/"
