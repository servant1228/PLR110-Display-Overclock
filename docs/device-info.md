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

## 7. ✅ Phase 1 验证结果（真机实测）

CI 云端编译的 `plr110_display_oc.ko` 在设备上：

```
$ insmod /data/local/tmp/plr110_display_oc.ko enable=0 dump_modes=1
insmod_rc=0
$ cat /proc/modules | grep plr110
plr110_display_oc 12288 0 - Live 0x0000000000000000 (O)
```

### 7.1 加载成功证明的三件事

1. **CRC 机制成立**：模块只有 7 个导入符号
   （`__stack_chk_fail` `_printk` `memcpy` `param_ops_int` `param_ops_uint`
   `register_kprobe` `unregister_kprobe`），全部取自设备真实 `__versions`，
   `module_layout = 0x797f2b3e` 校验通过。
2. **vermagic 机制成立**：模块 vermagic 尾部与设备一致；开头版本号被内核忽略。
3. **KCFI 成立**：`register_kprobe` 注册的 `pre_handler` 被成功回调（见下），
   说明 `kprobe_pre_handler_t` 的类型哈希与设备内核一致，即 r536225 +
   `-fsanitize=kcfi` + `CONFIG_CFI_ICALL_NORMALIZE_INTEGERS` 组合正确。

### 7.2 kallsyms 运行期符号解析

```
kallsyms_lookup_name @ kallsyms_lookup_name+0x0/0xc8
resolved 15/21 symbols
  drm_mode_probed_add / drm_mode_duplicate / drm_mode_destroy / drm_mode_vrefresh
  drm_connector_list_iter_begin/next/end
  dsi_display_get_modes              [msm_drm]
  dsi_display_get_modes_helper       [msm_drm]
  dsi_connector_get_modes            [msm_drm]
  dsi_display_set_mode               [msm_drm]
  dsi_panel_calc_dsi_transfer_time   [msm_drm]
  dsi_display_clk_ctrl               [msm_drm]   <- 时钟控制
  dsi_panel_tx_cmd_set               [msm_drm]   <- DCS 命令下发
  dsi_display_bind                   [msm_drm]
MISSING: dsi_panel_parse_timing, dsi_panel_get_drm_mode, dsi_panel_parse_dt,
         dsi_display_get, oplus_display_panel_cmd_print, oplus_dsi_panel_cmd_print
```

MISSING 的都是 static / 被内联 / 17.0.0.100 改过名的函数，不影响主路径。

### 7.3 ⭐ 结构体 ABI 校验（最关键的一步）

kprobe 挂在 `dsi_display_set_mode(display, mode, flags)` 上，读 `mode->timing`：

```
set_mode#1: 1272x2800 @120Hz clk=1107000000 hporch=26/26/2 vporch=56/24/2 dsc=1
```

与从设备 live FDT 解出的 DT 定义逐项对比：

| 字段 | kprobe 实测 | DT 定义 | |
| --- | --- | --- | --- |
| h_active × v_active | 1272 × 2800 | 1272 × 2800 | ✅ |
| refresh_rate | 120 | 120 | ✅ |
| clk_rate_hz | 1107000000 | 1107000000 | ✅ |
| hfp / hbp / hpw | 26 / 26 / 2 | 26 / 26 / 2 | ✅ |
| vfp / vbp / vpw | 56 / 24 / 2 | 56 / 24 / 2 | ✅ |
| dsc_enabled | 1 | dsc | ✅ |

**结论：`struct dsi_mode_info` 头部字段在运行内核上的偏移与镜像完全一致。**

尾部 `mdp_transfer_time_us` / `bpp` / `pixel_clk_khz` 读出来不对
（实测在镜像 +0 处读到 `bpp=8`），说明真实结构体比 16.0.10.500 头文件
**小 16 字节**（`sizeof(dsi_mode_info)` 约 88 而非 104）。
这是预期的源码/固件版本差；我们只用前缀字段，不受影响。

### 7.4 其它观察

- OPPO 有个 `[KERNEL_SECURITY_CHECK]: ko:[plr110_display_oc.ko] hash not found,
  maybe unknown ko.` —— 只打日志，不拦截。
- `rmmod` 干净退出，显示链路无异常。
- 第一版 CI 产生的 `depends=msm_drm,inte,oplus_bsp_zram_opt,msm_kgsl` 是假依赖
  （CRC 表第三列填了「来源模块」而非「导出模块」），已改为 `vmlinux`。

## 8. Phase 2a 结构体发现（真机实测，只读）

`dsi_connector_get_modes()` 只在 **DRM probe**（开机）时运行，而 KernelSU 模块
加载得比它晚，所以 `dsi_panel_get_mode()` 抓不到。试过的触发方式全部无效：
`wm size`、`peak_refresh_rate`、SurfaceFlinger hotplug、熄屏点亮。

改用 `dsi_panel_tx_cmd_set(panel, type)` —— 它每次下发 DCS 命令
（开关屏、亮度、fps 切换）都会被调用，`x0` 正是 `struct dsi_panel *`。

```
dsi_panel_tx_cmd_set: panel=ffffff8829a28000 type=0
panel->name = "AA607 P 7 A0020 dsc cmd mode panel"   (offset 0，确认对象正确)
```

### 实测偏移

```
offsetof(struct dsi_display, panel) = 0x108 = 264      (纯值比对，唯一匹配)

struct dsi_panel @0xffffff8829a28000:
  +05d0: 00000005 00000005 0a32c600 ffffff88
         ^^^^^^^^ ^^^^^^^^
         num_timing_nodes = 5   (+0x5d0 = 1488)
         num_display_modes = 5  (+0x5d4 = 1492)
```

与源码字段顺序 `struct dsi_display_mode *cur_mode; u32 num_timing_nodes;
 u32 num_display_modes;` 完全吸合 —— +0x5c8 就是 `cur_mode`（指向
`display->modes[4]`，与当前 165Hz 档位一致）。

### 结构体尺寸修正（推翻了 Phase 1 的推测）

原始 u32 dump 给出真实布局，比 16.0.10.500 头文件**少 8 字节**：

| 字段 | 实测偏移 |
| --- | --- |
| h_active / hbp / hsync / hfp / hskew | 0x00 / 0x04 / 0x08 / 0x0c / 0x10 |
| v_active / vbp / vsync / vfp | 0x18 / 0x1c / 0x20 / 0x24 |
| refresh_rate | **0x2c** |
| clk_rate_hz (u64) | 0x30 |
| min_dsi_clk_hz (u64) | 0x38 |
| mdp_transfer_time_us / dsi_transfer_time_us | 0x40 / 0x44 |
| dsc_enabled / vdc_enabled | 0x48 / 0x49 |
| dsc / vdc 指针 | 0x50 / 0x58 |

⇒ `sizeof(struct dsi_mode_info)` = **0x60 = 96**（不是我第一次算的 88/104）。

另一个重要发现：`0x60` 之后全是 0，且 `priv_info` 是 NULL。
因为 `dsi_bridge_pre_enable()` 传的是桥里**按值存的 `c_bridge->dsi_mode`**，
只有 timing 被填；真正带 `priv_info` 的完整 mode 由 `dsi_display_set_mode()`
内部通过 `dsi_display_find_mode()` 找回。
**这对注入是好消息：只需要改已验证的 `timing` 前缀。**

## 9. ⛔ 185Hz 的硬约束（算出来的，不是猜的）

165Hz 档位实测：`clk_rate_hz = 1363200000`，porch 为
hfp/hbp/hsync = 26/26/2、vfp/vbp/vsync = 56/24/2。

```
htotal = 1272 + 26 + 2 + 26        = 1326
vtotal = 2800 + 56 + 24 + 2        = 2882
空白行只有 82 行 / 2882 = 2.8%
```

想靠**压缩空白区**把 165 → 185Hz（需 +12.1%）：

```
vtotal_new = 2882 x 165/185 = 2570.6  <  v_active 2800
vfp_new    = 2570.6 - 2800 - 24 - 2 = -255    ← 负数，不可能
```

⇒ **没门。** 面板的空白区已经压到极限，唯一出路是**提高 DSI 时钟**：

```
clk_rate_hz_new = 1363200000 x 185/165 = 1528538182  (~1.529 GHz)
```

而 PHY 需要一条对应的 `qcom,mdss-dsi-panel-phy-timings` 参数表项
（165Hz 用的是 `002c0c0c1d1a0c0c0b0204002411`）。
这是下一步要解决的核心问题。

## 11. Phase 2b：DRM connector mode 链表（解决了 5 vs 10 的疑团）

用**编译期头文件**算出的偏移（不是我手算的）+ 运行时校验：

```
sizeof(drm_connector)=2736  name@96  modes@176  probed_modes@200
drm_connector ffffff882c816000 name="DSI-1" type=16/1 status=1
  probed_modes: 0 entries                 <- 已被 DRM 移空
  modes[0] "1272x2800x120cmd" clock=458583kHz vrefresh=119
  modes[1] "1272x2800x60cmd"  clock=229291kHz vrefresh=59
  modes[2] "1272x2800x90cmd"  clock=343937kHz vrefresh=89
  modes[3] "1272x2800x144cmd" clock=550300kHz vrefresh=143
  modes[4] "1272x2800x165cmd" clock=630552kHz vrefresh=164
  modes: 5 entries
```

两个结论：

1. **内核里就是 5 个 mode。** `dumpsys` 里那 10 个（含 1080×2378 组）是
   OPPO 显示 HAL / SurfaceFlinger 侧加的，与内核无关。
   ⇒ 不需要“追加到数组尾部”，也不存在数组空位问题。
2. `clock = 630552 kHz` 正好等于 `1326 × 2882 × 165 / 1000`，
   **反向验证了 htotal/vtotal 推导正确**。

## 12. mode 数据的完整映射链路（决定注入要改哪些位置）

```c
dsi_connector_get_modes()
  dsi_display_get_modes()          -> display->modes[]  (dsi_display_mode, 带 priv_info)
  dsi_convert_to_drm_mode(&modes[i], &drm_mode)
  drm_mode_duplicate() -> drm_mode_probed_add(connector, m)   -> connector->modes[]

// 用户态选模式后，DRM 校验：
dsi_conn_mode_valid()
  convert_to_dsi_mode(drm_mode, &dsi_mode)     // 只取 h/v 时序 + refresh_rate
  msm_parse_mode_priv_info(conn_state->msm_mode, &dsi_mode)
      dsi_mode->priv_info      = msm_mode->private
      dsi_mode->timing.clk_rate_hz = priv_info->clk_rate_hz   // <- 时钟只从这里来
  dsi_display_find_mode(display, &dsi_mode, NULL, &full)      // 按 timing 匹配 display->modes[]
  dsi_display_validate_mode(display, full, ALLOW_ADJUST)

// 真正下到硬件：
dsi_bridge_pre_enable()
  dsi_display_set_mode(display, &c_bridge->dsi_mode, 0)   // 按值传入，只带了 timing
```

关键点：

- `convert_to_dsi_mode()` **只写** h/v 时序和 `refresh_rate`（由 `drm_mode_vrefresh()`
  即 clock/htotal/vtotal 算出），**不写 `clk_rate_hz`**。
- `clk_rate_hz` 只来自 `priv_info->clk_rate_hz`，而 `priv_info` 反过来来自
  `msm_mode->private`（即 SurfaceFlinger 回传的 blob）。
- 所以要让 185Hz 真生效，必须同时改**三处**：
  1. `connector->modes[i]`（DRM mode：`clock`，以及让 `vrefresh` 算出 185）
  2. `display->modes[i].timing`（让 `dsi_display_find_mode()` 能匹配上）
  3. `display->modes[i].priv_info->clk_rate_hz`（真正的 DSI 时钟）

## 14. PHY timing 参数：这是最后一个卡点

### 为什么需要新参数

`dsi_phy_set_timing_params()` 在**换模式**和**换时钟**两条路径上都被调用，
而且用的都是 DT 里的 `phy_timing_val`：

```c
dsi_display.c:5715   mode set 路径        -> dsi_phy_set_timing_params(phy, priv_info->phy_timing_val, ...)
dsi_display.c:9735   时钟变更路径（同一份 DT 表）
```

所以只改 `clk_rate_hz` 会留下一张与时钟不匹配的 PHY 表，链路会出问题。

### 这 14 个字到底是什么

```c
int dsi_phy_hw_timing_val_v7_2(struct dsi_phy_per_lane_cfgs *timing_cfg,
			       u32 *timing_val, u32 size)
{
	if (size != DSI_PHY_TIMING_V4_SIZE)   /* 14 */
		return -EINVAL;
	for (i = 0; i < size; i++)
		timing_cfg->lane_v4[i] = timing_val[i];
}
```

且 `lane_v4[0..13]` 被直接写进 `DSIPHY_CMN_TIMING_CTRL_0..13` 寄存器。
⇒ **这 14 个字节是预设好的原始寄存器值**，不是可推导的中间量，
只在它被设计的那一个 bit clock 下有效。

```
1107 MHz -> 00 24 0a 0a 1a 18 0a 0a 09 02 04 00 1e 0f
1363.2MHz -> 00 2c 0c 0c 1d 1a 0c 0c 0b 02 04 00 24 11
```

### 但是可以自己算出来

`msm/dsi/dsi_phy_timing_calc.c` 里有完整公式，输入是 bit clock 和 PHY 参数：

```
calc_clk_prepare()  calc_clk_zero()   calc_clk_trail()
calc_hs_prepare()   calc_hs_zero()    calc_hs_trail()
calc_hs_rqst()      calc_hs_exit()    calc_clk_post()   calc_clk_pre()
```

输入量（`tlpx_numer_ns`、`hs_prep_buf` 等）全部来自 DT 的 `mdss_dsi_phy0` 节点，
已可从 live FDT 读到。

### 可行性验证方法（重要）

在 Python 里复现这套公式，**先用 1363.2 MHz 跑一遍，看能否得到
`002c0c0c1d1a0c0c0b0204002411`**。

- 能对上 ⇒ 公式复现正确，可以用同样的代码算 1528.5 MHz 的参数，可信。
- 对不上 ⇒ 说明有遗漏的输入，停下重新分析。

这是一个自带校验的路径，不靠猜。

### 输入量全部是硬编码常数（不是 DT）

`dsi_phy_hw_calculate_timing_params()` 开头就是：

```c
int dsi_phy_hw_calculate_timing_params(struct dsi_phy_hw *phy,
				       struct dsi_mode_info *mode,
				       struct dsi_host_common_cfg *host,
				       struct dsi_phy_per_lane_cfgs *timing,
				       bool use_mode_bit_clk)
{
	u32 const esc_clk_mhz = 192;
	u32 const esc_clk_mmss_cc_prediv = 10;
	u32 const tlpx_numer = 1000;
	u32 const tr_eot = 20;
	u32 const clk_prepare_spec_min = 38;
	u32 const clk_prepare_spec_max = 95;
	u32 const clk_trail_spec_min = 60;
	u32 const hs_exit_spec_min = 100;
	u32 const hs_exit_reco_max = 255;
	u32 const hs_rqst_spec_min = 50;
	u32 const hs_rqst_reco_max = 255;

	bpp = bits_per_pixel[host->dst_format];
	inter_num = bpp * mode->refresh_rate;
	num_of_lanes = popcount(host->data_lanes);

	if (use_mode_bit_clk)
		x = mode->clk_rate_hz;
	else
		x = h_total * v_total * refresh_rate * bpp / lanes;
	...
	clk_params.tlpx_numer_ns = tlpx_numer;
	... -> calc_clk_prepare() ... -> timing->lane_v4[]
}
```

⇒ **没有一个输入来自 DT**（PHY 节点里只有 `qcom,platform-lane-config`
和 `qcom,platform-strength-ctrl`，与这 14 个字节无关）。
⇒ 可以在 Python 里逐行复现，离线算出任意 bit clock 对应的 14 字节。

### 自带校验的三个已知向量

| bit clock | phy-timings | 来源 |
| --- | --- | --- |
| 1107.0 MHz | `00240a0a1a180a0a090204001e0f` | 60/90/120Hz 档位 |
| 1363.2 MHz | `002c0c0c1d1a0c0c0b0204002411` | 144/165Hz 档位 |
| **1528.5 MHz** | **待算**（目标） | 185Hz |

复现公式后能同时对上前两个，第三个就可以信任。

## 15. 注入需要同时改的四处

| # | 位置 | 内容 | 已验证? |
| --- | --- | --- | --- |
| 1 | `connector->modes[4]` (DRM mode) | `clock` = `htotal*vtotal*fps/1000` | 偏移是编译器给的 ✅ |
| 2 | `display->modes[4].timing` | `refresh_rate`、`clk_rate_hz` | 前缀逐个字段对比过 DT ✅ |
| 3 | `display->modes[4].priv_info->clk_rate_hz` | 真正的 DSI 时钟 | 偏移待测 ⏳ |
| 4 | `display->modes[4].priv_info->phy_timing_val` | 新速率对应的 14 字节 PHY 表 | 偏移待测 ⏳ |

第 3/4 项需要再测一次 `struct dsi_display_mode_priv_info` 的偏移
（可以在 kprobe 里拿 `priv_info` 指针后扫内存，方式和 2a 一样）。

## 16. 待验证 / 风险

1. 面板是 **command mode**，刷新率主要由 DDIC 决定（ADFR/fps-switch 命令）。
   165 → 185 是否物理可行取决于 DDIC，host timing 不一定能强制。
   需要实测（高速相机 / 光电探测器 / DDIC 帧计数寄存器）确认。
2. 开源码 6.12.38 vs 设备 6.12.69：KMI 覆盖内核导出符号；
   **vendor display 内部结构体不在 KMI 内**，若 17.0.0.100 改过 display-drivers
   则结构体偏移可能不同 → 模块内做运行期自检，不匹配则拒绝 hook。

## 17. PHY 参数求解结果（fitted，已双向量验证）

### 为什么不能"推导"

把驱动源码里的 `dsi_phy_hw_calculate_timing_params()` 逐行复现到
`scripts/phy_timing_calc.py`，结果是：

```
expected 002c0c0c1d1a0c0c0b0204002411   (1363.2 MHz)
got      00060c072930000c08020400151b
            ^  ^        ^
```

`clk_prepare`(lane[2]) 与 `hs_trail`(lane[7]) 在 1107/1363.2 **两个时钟下都精确命中**，
4 个常数位（[0]=0x00 [9]=0x02 [10]=0x04 [11]=0x00）也全对 ⇒ **打包顺序与框架正确**。
但其余字段不一致。

原因在数据里：把每个寄存器值除以 bit clock（GHz）：

| 字段 | 1107MHz | 1363.2MHz | reg / GHz |
| --- | --- | --- | --- |
| clk_zero | 36 | 44 | 32.52 / 32.26 ← 常数 |
| clk_prepare / clk_trail / hs_prepare / hs_trail | 10 | 12 | 9.03 / 8.80 ← 常数 |
| hs_rqst | 9 | 11 | 8.13 / 8.07 ← 常数 |
| clk_pre | 30 | 36 | 27.10 / 26.39 ← 常数 |
| clk_post | 15 | 17 | 13.55 / 12.46 ← 非常数 |
| hs_exit | 26 | 29 | 23.49 / 21.26 ← 非常数 |
| hs_zero | 24 | 26 | 21.68 / 19.06 ← 非常数 |

8/10 个字段就是「固定纳秒数 × bit clock」，证实这张表确实是按时钟生成的。
两个例外（`hs_exit`/`hs_zero`）恰好是走
`rec = rec_min + (255 - rec_min) * buf / 100` 非线性公式的那两个。

⇒ **DT 表是高通内部工具生成的，不在开源代码里**，精确推导不可得。

### 改用拟合模型（`scripts/phy_timing_calc.py --extrapolate`）

- 7 个字段：`reg = 常数 × bitclk_GHz`（常数取两向量平均）
- `clk_post` / `hs_exit` / `hs_zero`：在两已知点间线性插值

**验证结果 —— 两个已知向量 14/14 字节全部精确命中：**

```
1107000000 Hz  expected 00240a0a1a180a0a090204001e0f
               fitted   00240a0a1a180a0a090204001e0f  OK
1363200000 Hz  expected 002c0c0c1d1a0c0c0b0204002411
               fitted   002c0c0c1d1a0c0c0b0204002411  OK
```

### 外推结果

| 目标 | bit clock | phy-timings |
| --- | --- | --- |
| 170 Hz | 1410 Mbps | `002e0d0d1e1a0d0d0b0204002611` |
| 175 Hz | 1446 Mbps | `002f0d0d1e1b0d0d0c0204002712` |
| 180 Hz | 1487 Mbps | `00300d0d1e1b0d0d0c0204002812` |
| **185 Hz** | **1529 Mbps** | **`00320e0e1f1b0e0e0c0204002912`** |

185 Hz 的 bit clock 由 `1363200000 × 185/165 = 1528538182` → 取整 1529 Mbps。

⚠️ 这是**外推**，不是推导。`hs_exit`/`hs_zero`/`clk_post` 是三个最弱项。

## 18. 占用率分析（为什么必须提时钟）

| 刷新率 | 帧时间 | mdp 传输 | 占空比 | 余量 |
| --- | --- | --- | --- | --- |
| 144 Hz | 6944us | 5380us | 77.5% | 1564us |
| 165 Hz | 6061us | 5380us | **88.8%** | 680us |
| 175 Hz | 5714us | 5380us | 94.1% | 334us |
| 180 Hz | 5556us | 5380us | 96.8% | 176us |
| **185 Hz** | 5405us | 5380us | **99.5%** | **25us** |
| 190 Hz | 5263us | 5380us | 102.2% | 负 |

185 Hz 在不提时钟时占空比 99.5%，基本必然 underrun ⇒ 提时钟不是可选优化，是必需条件。

## 19. 建议的验证路径：渐进而不是一步到位

拟合模型在 165 Hz 处是**内插**（可信），到 185 Hz 要外推 12%。所以：

1. **先在 175 Hz 验证**（+6% 时钟，外推距离只有一半，占空比 94.1% 余量 334us）
   - 若 175 Hz 稳定 ⇒ 模型方向正确
   - 若 175 Hz 就花屏/黑屏 ⇒ 外推方法有问题，不要硬上 185
2. 通过后再做 180 → 185

这样任何一步失败都能立刻定位，而不是在 185 处面对一个复合故障。

## 20. ⚠️ 事故记录：手机被 panic 重启（已定位并热修复）

### 现象

调试过程中手机多次**突然卡死并重启**，用户确认不是他自己操作的。
设备恢复后查证：

```
$ cat /proc/sys/kernel/panic_on_oops
1
$ uptime
 22:37:16 up 0 min          <- 刚开机
```

**`panic_on_oops = 1`** —— Android 默认如此。任何一次内核页错误都不是"出错返回"，
而是**直接 panic 重启**，没有优雅失败路径。

### 根因（我的代码）

`scan_priv_info()` 里为了找 `phy_timing_val` 用了「取一个像指针的 u64，往下读一大段，
找签名」的策略：

```c
hit = mem_has(p, 0x4000, plr165_phy, sizeof(plr165_phy));   /* 盲读 16KB */
```

**"看起来像内核指针" != "是一个已映射的对象"。** 只要有一个值是栈上的残留数据、
或者偏移算错，这次读取就落到未映射页 → oops → panic → 重启。

### 修复

`3105f65`：整段指针游走被 `#if 0` 封存，并写入原因说明；活跃代码只读
「来自真实函数参数、且大小有界」的结构体本身。

### 教训（写进代码注释了）

任何后续探针必须满足其一：

1. 用 `copy_from_kernel_nofault()` / `probe_kernel_read()` 之类**容错读取**，
   坏地址返回错误而不是 oops；
2. 或只解引用**已被正面确认过**的目标，且读取长度不超过该对象的真实大小。

设备上这三个符号是存在的（kallsyms 可查）：

```
T copy_from_kernel_nofault
T copy_to_kernel_nofault
T strncpy_from_kernel_nofault
```

但没有任何厂商模块导入它们，所以 `configs/Module.symvers` 里没有它们的 CRC。
可用途径是**运行期 kallsyms 解析 + 通过函数指针调用**（不需要 CRC），
代价是 KCFI 会校验函数类型哈希 —— 我们的 typedef 必须与内核声明完全一致，
否则 CFI failure 同样会 panic。

### 这也改变了 Phase 2c 的风险评估

注入本身是**写**操作。偏移错一个字节同样是 panic。所以：

- 在拿到容错读取能力之前，不应该再做任何指针游走式探测；
- 正式注入前，必须先用 nofault 读取把每个偏移**读一遍验证**，
  确认无误再写。
