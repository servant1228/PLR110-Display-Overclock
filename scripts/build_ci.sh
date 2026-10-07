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
# LLVM=1 makes Kbuild drive the whole LLVM toolchain (ld.lld, llvm-ar,
# llvm-nm, llvm-objcopy, llvm-objdump ...) for target *and* host objects
# (e.g. tools/bpf/resolve_btfids when CONFIG_DEBUG_INFO_BTF=y). Passing
# CC/LD alone is not enough -- the bin dir must be on PATH, otherwise the
# host link step dies with "/bin/sh: 1: ld.lld: not found".
export PATH="$CLANG_DIR:$PATH"
mkdir -p "$OUT"
LOG="$OUT/build.log"
: > "$LOG"

log() { echo -e "\n\033[1;36m>>> $*\033[0m" | tee -a "$LOG"; }

# ---------------------------------------------------------------- sanity ----
log "toolchain"
"$CLANG" --version | tee -a "$LOG"
for t in clang clang++ ld.lld llvm-ar llvm-nm llvm-objcopy llvm-objdump \
         llvm-strip llvm-readelf; do
  if command -v "$t" >/dev/null 2>&1; then
    printf '  %-14s %s\n' "$t" "$(command -v "$t")" | tee -a "$LOG"
  else
    echo "::error::$t not found under $CLANG_DIR (required by LLVM=1)" | tee -a "$LOG"
    exit 1
  fi
done
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
       LD="${CLANG_DIR}/ld.lld"
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
         LD="${CLANG_DIR}/ld.lld" \
         olddefconfig 2>&1 | tee -a "$LOG"
  else
    make -C "$tree" O="$OUT" ARCH=arm64 LLVM=1 CC="$CLANG" \
         LD="${CLANG_DIR}/ld.lld" \
         defconfig 2>&1 | tee -a "$LOG"
  fi

  # The device config sets
  #   CONFIG_MODULE_SIG_PROTECT=y
  #   CONFIG_MODULE_SIG_PROTECT_LIST="protected_module_names_list"
  # and modpost has that file as a prerequisite. The vendor/OnePlus tree ships
  # it; the GKI common tree does not, which fails the build with
  #   No rule to make target '.../protected_module_names_list', needed by modpost
  # An empty list means "no module is required to be signed", which is what we
  # want for an unsigned self-built module.
  prot="$(sed -n 's/^CONFIG_MODULE_SIG_PROTECT_LIST="\(.*\)"$/\1/p' \
          "$OUT/.config" | head -1)"
  if [ -n "$prot" ] && [ ! -e "$tree/$prot" ]; then
    log "creating empty $prot (CONFIG_MODULE_SIG_PROTECT_LIST=$prot)"
    : > "$tree/$prot"
  fi

  make -C "$tree" O="$OUT" ARCH=arm64 LLVM=1 CC="$CLANG" \
       LD="${CLANG_DIR}/ld.lld" \
       modules_prepare 2>&1 | tee -a "$LOG"
}

log "modules_prepare ($KERNEL_SRC)"
if ! prepare_tree "$KERNEL_SRC"; then
  if [ -n "$FALLBACK_KERNEL_SRC" ] && [ -f "$FALLBACK_KERNEL_SRC/Makefile" ]; then
    log "::warning::prepare failed -> falling back to $FALLBACK_KERNEL_SRC"
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
              LD="${CLANG_DIR}/ld.lld")

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
