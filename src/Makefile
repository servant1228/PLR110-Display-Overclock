# SPDX-License-Identifier: GPL-2.0-only
#
# Out-of-tree module makefile (Kbuild).
#
# Built by scripts/build_ci.sh which passes:
#   -C <kernel tree> O=<out> ARCH=arm64 LLVM=1 M=<this dir>
#   KBUILD_EXTRA_SYMBOLS=<device-derived Module.symvers>
#
# PLR110_DISPLAY_SRC optionally points at
#   .../android_kernel_modules_and_devicetree_oneplus_sm8845/vendor/qcom/opensource/display-drivers
# so we can include the vendor display headers verbatim.

obj-m += plr110_display_oc.o

ccflags-y += -Wall -Wno-unused-variable -Wno-unused-function

ifneq ($(PLR110_DISPLAY_SRC),)
ccflags-y += -DPLR110_HAVE_DISPLAY_SRC=1
ccflags-y += -I$(PLR110_DISPLAY_SRC)
ccflags-y += -I$(PLR110_DISPLAY_SRC)/msm
ccflags-y += -I$(PLR110_DISPLAY_SRC)/msm/dsi
ccflags-y += -I$(PLR110_DISPLAY_SRC)/include
endif

KBUILD_CFLAGS += -Wno-error=incompatible-pointer-types

# Suppress "unknown symbol" failures only when explicitly asked, so CI surfaces
# real problems instead of hiding them.
ifdef PLR110_MODPOST_WARN
KBUILD_MODPOST_WARN := 1
endif
