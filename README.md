# PLR110 Display Overclock — 185Hz

OnePlus Ace 6T (`PLR110`, Qualcomm **SM8845**) 显示刷新率超频项目。

在 stock 165Hz 面板上新增 **185Hz** 档位，以 **KernelSU 模块**（含 WebUI）形式分发，
底层是一个运行期注入的 **LKM (loadable kernel module)**，不修改 DTBO、不替换 panel 驱动。

> 参考实现：[Yunnijian/MTK-Display-Overclock-LKM](https://github.com/Yunnijian/MTK-Display-Overclock-LKM)（联发科 MT6993 版本）。
> 本项目是其在 **高通 MSM DSI / DRM** 栈上的重新实现 —— 两者驱动栈完全不同，代码不共享。

---

## 状态

| 阶段 | 状态 |
| --- | --- |
| 官方内核源码定位 / 版本比对 | ✅ 完成 |
| 高通 DSI 挂载点分析 | ✅ 完成 |
| 设备地面真相采集 | ✅ 完成（`docs/device-info.md`） |
| 符号 CRC 提取（1267 条） | ✅ 完成 |
| CI 云端编译链 | ✅ 打通（冷跑 ~15min，命中缓存 **3m41s**） |
| LKM 编译 + 真机加载 | ✅ **通过**（`insmod rc=0`，CRC + vermagic + KCFI 全部成立） |
| 结构体 ABI 校验 | ✅ **通过**（`dsi_mode_info` 头部偏移逐项吻合 DT 实测值） |
| 185Hz 档位注入 | ⏳ Phase 2 |
| 185Hz 物理生效验证 | ⏳ Phase 3（CMD 模式面板，风险点） |

### Phase 1 关键证据

```
$ insmod plr110_display_oc.ko enable=0 dump_modes=1   ->  rc=0
plr110_display_oc: resolved 15/21 symbols
plr110_display_oc: set_mode#1: 1272x2800 @120Hz clk=1107000000
                   hporch=26/26/2 vporch=56/24/2 dsc=1
```

kprobe 读出的 timing 与设备 live FDT 里的 DT 定义逐项一致。

---

## ⚠️ 已知兼容性风险

一加官方开源内核 **落后于设备固件**：

| | 版本 | 内核 |
| --- | --- | --- |
| OnePlusOSS 开源分支 `oneplus/sm8845_b_16.0.0_ace_6t` | `PLR110_16.0.10.500(CN01)` | `6.12.38` |
| 目标设备（本项目） | `17.0.0.100` | `6.12.69-android16-6-g242e1a07f878-ab15865446-4k` |

设备内核 commit `242e1a07f878` 在开源仓库中 **不存在（HTTP 404）**。

缓解策略（方案 C）：

1. **vermagic** 直接硬编码为目标内核字符串。
2. **Module.symvers / CRC** 由 CI 编译内核时生成；`android16-6.12` 属于同一 KMI 世代，
   导出符号 ABI 冻结，CRC 应当一致。
3. **vendor 显示驱动内部结构体**（`dsi_panel` / `dsi_display_mode` / `dsi_display`）
   **不在 KMI 保护范围**。模块加载时会做运行时自检（面板名、模式数、已知字段值），
   不匹配则拒绝 hook 并安全退出。
4. 所有内核/厂商符号通过 `kallsyms_lookup_name` **运行期解析**，不静态链接厂商符号。

---

## 目录结构

```
.
├── .github/workflows/build.yml   GitHub Actions：云端编译 .ko + 打包 KernelSU 模块
├── src/                          LKM 源码
│   ├── plr110_display_oc.c
│   └── Makefile
├── module/                       KernelSU 模块模板（打包进 zip）
│   ├── module.prop
│   ├── customize.sh
│   ├── post-fs-data.sh
│   ├── service.sh
│   ├── uninstall.sh
│   └── webroot/                  KernelSU WebUI
├── configs/                      设备内核配置 / 版本锚点
├── scripts/
│   ├── build_ci.sh               CI 构建脚本
│   └── package_ksu.sh            打包 KernelSU zip
└── docs/
```

---

## 构建

### 本地（WSL2 / Linux）

```sh
export PLR110_KERNEL_SRC=/path/to/android_kernel_oneplus_sm8845
export PLR110_DISPLAY_SRC=/path/to/android_kernel_modules_and_devicetree_oneplus_sm8845
export PLR110_CLANG=/path/to/clang-r536225/bin/clang
bash scripts/build_ci.sh
```

### 云端（推荐）

推送到 GitHub 后，`.github/workflows/build.yml` 会：
拉取 Android Clang r536225 → 拉取一加内核源码 → 用**设备实测的 `/proc/config.gz`** 配置内核 →
`modules_prepare` → 编译 `plr110_display_oc.ko` → 打包 KernelSU zip → 上传 Artifact。

Artifact 里可直接下载 `PLR110-Display-OC-<sha>.zip`，用 KernelSU 管理器刷入。

---

## 安全

- 模块默认 **只做只读诊断**（dump 当前 mode table），不修改时序。
- 超频功能需显式 `insmod plr110_display_oc.ko enable=1`（或 WebUI 开关）。
- 任何时序注入前都做 `panel_name` + `stock mode count` 校验，失败即 `-EINVAL` 退出。
- KernelSU 模块内含 `uninstall.sh`，卸载后可完全恢复。

## License

GPL-2.0-only（LKM 链接内核，必须 GPL）
