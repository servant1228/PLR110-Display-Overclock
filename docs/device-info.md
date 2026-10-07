# 设备地面真相（实测，2026-10）

全部数据来自 USB + KernelSU root 直读设备，非推测。

## 1. 设备 / 系统

| 项 | 值 |
| --- | --- |
| 型号 | OnePlus Ace 6T |
| `ro.product.model` | `PLR110` |
| `ro.product.device` | `OP6117L1` |
| 固件 | `PLR110_17.0.0.100(CN01)` |
| `ro.build.version.oplusrom` | `V17.0.0` |
| Android | 17（SDK 37） |
| fingerprint | `OnePlus/PLR110/OP6117L1:17/CP2A.260605.016/B.4934c7f-2341852-2356a2f:user/release-keys` |
| Root | KernelSU（`context=u:r:ksu:s0`），`/data/adb/ksu` |
| SoC | Qualcomm SM8845（Snapdragon 8 Gen 5） |

## 2. 内核

```
Linux version 6.12.69-android16-6-g242e1a07f878-ab15865446-4k
  (kleaf@build-host)
  (Android (14043575, +pgo, +bolt, +lto, +mlgo, based on r536225)
   clang version 19.0.1, LLD 19.0.1)
  #1 SMP PREEMPT Wed Jul 15 11:18:49 UTC 2026
```

编译器 = **Android Clang r536225 / 19.0.1**（与官方内核一致，building flag 里带 `+pgo +bolt +lto +mlgo`）。

### 开源对照

| 仓库（OnePlusOSS，分支 `oneplus/sm8845_b_16.0.0_ace_6t`） | tip | 对应固件 |
| --- | --- | --- |
| `android_kernel_common_oneplus_sm8845`（GKI common，`SUBLEVEL=38` → 6.12.38） | `c1102892` | `PLR110_16.0.10.500(CN01)` |
| `android_kernel_oneplus_sm8845`（vendor/msm-kernel 树） | `2774f857` | 同上 |
| `android_kernel_modules_and_devicetree_oneplus_sm8845`（vendor 模块 + DT） | `6265826`（2026-09-15） | 同上 |

**设备内核 commit `242e1a07f878` 在开源仓库中不存在（HTTP 404，对照组短 SHA 返回 200）。**
说明开源码落后于设备固件 `17.0.0.100`。

### 关键 config（来自设备 `/proc/config.gz`，8151 行）

```
CONFIG_MODVERSIONS=y
CONFIG_CFI_CLANG=y
CONFIG_CFI_ICALL_NORMALIZE_INTEGERS=y
CONFIG_LTO_NONE=y
CONFIG_RANDSTRUCT_NONE=y
CONFIG_MODULE_SIG=y
CONFIG_MODULE_SIG_FORCE is not set
CONFIG_MODULE_SIG_PROTECT=y
CONFIG_LOCALVERSION="-4k"
CONFIG_LOCALVERSION_AUTO=y
```

## 3. ⭐ vermagic 真相

设备上 `msm_drm.ko`（vendor 模块，实际能加载）：

```
vermagic=6.12.69-android16-6-o-gb79517a1cbef-4k SMP preempt mod_unload modversions aarch64
```

内核 `/proc/version` 却是 `...-g242e1a07f878-ab15865446-4k`。

**两者不同却能加载**，原因在 `kernel/module/version.c`：

```c
/* First part is kernel version, which we ignore if module has crcs. */
int same_magic(const char *amagic, const char *bmagic, bool has_crcs)
{
	if (has_crcs) {
		amagic += strcspn(amagic, " ");
		bmagic += strcspn(bmagic, " ");
	}
	return strcmp(amagic, bmagic) == 0;
}
```

✅ **结论：带 CRC 的模块，vermagic 的内核版本号部分被完全忽略**，只比较尾部的
` SMP preempt mod_unload modversions aarch64`。

⇒ 自编译模块真正需要的是**符号 CRC 正确**（含 `module_layout`），vermagic 无所谓。

## 4. 符号 CRC（Module.symvers）

`configs/Module.symvers` 由 `scripts/extract_symvers.py` 从设备上真实 `.ko` 的
`__version_ext_crcs` + `__version_ext_names` 提取后合并（1267 条），来源模块：

```
msm_drm, msm_kgsl, msm_hw_fence, msm_performance, sync_fence,
oplus_power_hook, oplus_patch, inte, oplus_bsp_sched_ext,
oplus_bsp_zram_opt, oplus_bsp_dfr_kp_freeze_detect,
oplus_network_vip_task, lt9611uxc
```

关键条目：

| 符号 | CRC |
| --- | --- |
| `module_layout` | `0x797f2b3e` |
| `register_kprobe` | `0xcca4d86f` |
| `unregister_kprobe` | `0x1d045bd3` |
| `register_kretprobe` | `0x2a38fe47` |
| `unregister_kretprobe` | `0x3d5e2716` |
| `drm_mode_probed_add` | `0xd9851c07` |
| `drm_mode_duplicate` | `0x9410715e` |

刷新（换固件后需重跑）：

```sh
adb shell su -c 'cat /vendor_dlkm/lib/modules/msm_drm.ko' > msm_drm.ko
python scripts/extract_symvers.py msm_drm.ko ... > configs/Module.symvers
```

## 5. 面板

DT 节点：`/soc/qcom,mdss_mdp@9800000/qcom,mdss_dsi_panel_AA607_P_7_A0020_dsc_cmd`

```
qcom,mdss-dsi-panel-name        = "AA607 P 7 A0020 dsc cmd mode panel"
qcom,mdss-dsi-panel-type        = "dsi_cmd_mode"      ← 命令模式
qcom,mdss-dsi-traffic-mode      = "burst_mode"
qcom,mdss-dsi-bpp               = 30                  (10 bit/component)
qcom,mdss-dsi-te-pin-select     = 1
qcom,mdss-dsi-te-dcs-command    = 1
qcom,qsync-enable               (min refresh 30)
oplus,mdss-dsi-vendor-name      = "A0020"
oplus,mdss-dsi-manufacture      = "P_7"
```

DSC：`slice 636x20`、`2 encoders`、`8 bpp`、`10 bpc`、`version 18`、`lm-split 636/636`。

### Timing 表（DT 里 5 档，全部 1272×2800）

| timing | fps | panel-clockrate | phy-timings | mdp-transfer-us | hfp/hbp/hpw | vfp/vbp/vpw |
| --- | --- | --- | --- | --- | --- | --- |
| `sdc_fhd_120` | 120 | 1107000000 | `00240a0a1a180a0a090204001e0f` | 6680 | 26/26/2 | 56/24/2 |
| `sdc_fhd_90`  | 90  | 1107000000 | 同上 | 6680 | 26/26/2 | 56/24/2 |
| `sdc_fhd_60`  | 60  | 1107000000 | 同上 | 6680 | 26/26/2 | 56/24/2 |
| `sdc_fhd_144` | 144 | 1363200000 | `002c0c0c1d1a0c0c0b0204002411` | 5380 | 26/26/2 | 56/24/2 |
| `sdc_fhd_165` | 165 | 1363200000 | 同上 | 5380 | 26/26/2 | 56/24/2 |

⇒ htotal = 1326，vtotal = 2882，**所有档位的 porch 完全相同**。
档位差异只体现在 `panel-clockrate` / `phy-timings` / `mdp-transfer-time` 上。

### 框架侧（`dumpsys display`）

- 原生：**1272 × 2800**；降分辨率组：**1080 × 2378**（`dynamic-resolution-switch-immediate`）
- 每组 5 个 DRM mode（120 / 60 / 90 / 144 / 165）
- 额外刷新率由 ADFR/DCS 命令实现：`165 / 144 / 120 / 90 / 82.5 / 72 / 60 / 55 / 48 / 45 / 41.25`
- 60↔120 切换走 DDIC 命令 `qcom,mdss-dsi-fps-switch-60-to-120-command`（1153 B）

## 6. 高通 hook 点（来自 vendor 源码）

| 作用 | 函数 | 文件 |
| --- | --- | --- |
| 生成 DRM mode 列表 | `dsi_connector_get_modes()` | `msm/dsi/dsi_drm.c:1274` |
| 向上报 mode | `dsi_display_get_modes()` / `_helper()` | `dsi_display.c:8106 / 7888` |
| 解析单条 timing | `dsi_panel_parse_timing()` | `dsi_panel.c:1175` |
| 应用 mode | `dsi_display_set_mode()` | `dsi_display.c:8779` |
| 计算最小 DSI bit clock | `dsi_panel_calc_dsi_transfer_time()` | `dsi_panel.c:5212` |

`struct dsi_mode_info`（`dsi_defs.h:483`）前 22 个字段已镜像到 `src/plr110_display_oc.c`
用于运行期 ABI 自检。

## 7. 待验证 / 风险

1. 面板是 **command mode**，刷新率主要由 DDIC 决定（ADFR/fps-switch 命令）。
   165 → 185 是否物理可行取决于 DDIC，host timing 不一定能强制。
   需要实测（高速相机 / 光电探测器 / DDIC 帧计数寄存器）确认。
2. 开源码 6.12.38 vs 设备 6.12.69：KMI 覆盖内核导出符号；
   **vendor display 内部结构体不在 KMI 内**，若 17.0.0.100 改过 display-drivers
   则结构体偏移可能不同 → 模块内做运行期自检，不匹配则拒绝 hook。
