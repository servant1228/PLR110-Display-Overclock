# 设备内核配置（实测，来自 /proc/config.gz）

`device.config.gz` 直接从设备导出：

```sh
adb exec-out "su -c 'cat /proc/config.gz'" > configs/device.config.gz
```

CI (`scripts/build_ci.sh`) 会优先使用它来配置内核树，保证
`CONFIG_MODVERSIONS` / `CONFIG_CFI_CLANG` / `CONFIG_CFI_ICALL_NORMALIZE_INTEGERS` /
`CONFIG_RANDSTRUCT_NONE` / `CONFIG_LTO_NONE` 等与设备一致，从而让
`-fsanitize=kcfi` 的类型哈希与结构体布局对齐。

换固件后请重新导出。

## kernel.release

`kernel.release` 写的是设备 `uname -r`。

⚠️ 实测结论（见 `docs/device-info.md` §3）：带 CRC 的模块，vermagic 的
**内核版本号部分会被内核忽略**（`same_magic()`），只有尾部
` SMP preempt mod_unload modversions aarch64` 参与比较。
所以这个文件只是留档，不影响加载；真正决定能否加载的是
`Module.symvers` 里的符号 CRC。
