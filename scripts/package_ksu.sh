#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Package the built .ko + KernelSU module template into a flashable zip.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${PLR110_OUT:-$ROOT/out}"
KO="$(find "$OUT" -maxdepth 1 -name '*.ko' | head -1)"
test -n "$KO" || { echo "no .ko in $OUT"; exit 1; }

NAME="$(basename "$KO")"
VER="$(grep -E '^version=' "$ROOT/module/module.prop" | cut -d= -f2 | tr -d '\r')"
ZIP="$OUT/PLR110-Display-OC-${VER}.zip"

STAGE="$OUT/ksu-stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/system/lib/modules"

cp "$ROOT/module/module.prop"    "$STAGE/"
cp "$ROOT/module/customize.sh"   "$STAGE/"
cp "$ROOT/module/post-fs-data.sh" "$STAGE/"
cp "$ROOT/module/service.sh"     "$STAGE/"
cp "$ROOT/module/uninstall.sh"   "$STAGE/"
cp -r "$ROOT/module/webroot"     "$STAGE/"
cp "$KO" "$STAGE/system/lib/modules/$NAME"

# ship the target vermagic so runtime can self-check
if [ -f "$ROOT/configs/kernel.release" ]; then
  cp "$ROOT/configs/kernel.release" "$STAGE/system/lib/modules/kernel.release"
fi

chmod 0755 "$STAGE/customize.sh" "$STAGE/post-fs-data.sh" "$STAGE/service.sh" "$STAGE/uninstall.sh"
chmod 0644 "$STAGE/module.prop" "$STAGE/system/lib/modules/$NAME"

rm -f "$ZIP"
( cd "$STAGE" && zip -qr9 "$ZIP" . )
echo "packaged: $ZIP"
unzip -l "$ZIP" | sed -n '1,40p'
