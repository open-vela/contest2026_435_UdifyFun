---
name: bes1700-platform
description: "BES2800BP / best1700_ep AOS EVB 库模式 BSP 的编译、烧录与能力边界诊断。Use when: 编译或烧录 best1700 固件、使用 dldtool、nuttx_ap.bin 产物问题、库模式功能缺失排查（bthci_register / AP2BTH rptun / USB DCD 孤儿符号）、出厂测试固件（彩带画面）问题、erase-chip 后恢复。触发词：best1700、BES2800BP、dldtool、烧录、库模式、孤儿符号、彩带。"
---

# BES2800BP (best1700_ep) 平台开发

库模式发布的 BSP：芯片与板级实现以预编译 `.a` 提供，源码仅保留板级入口胶水。**判断一个功能是否可用，必须做符号取证（见 references/library-forensics.md），不能靠猜。**

## 能力边界决策表

| 需求 | 可行性 | 依据 |
|---|---|---|
| AP 应用（LVGL / 文件系统 / UART-MIDI） | ✅ | configs/ap 自由编译，app 层无限制 |
| APC1 协处理器 | ⚠️ | configs/apc1 存在；AP↔APC1 rptun 通道在库中且启动链自动注册 |
| BTH 蓝牙（HCI / BLE） | ❌ | AP↔BTH rptun 通道实现不在库中；`bthci_register` 全库零调用方 |
| USB 设备类枚举（USB-MIDI / HID） | ❌ | NuttX DCD 胶水层缺失，`hal_usb` 系列零调用方、中断向量未接 |
| HiFi4 DSP 音频 | ⚠️ | 仅 prebuilt bin（`nuttx_audio_*.bin`），可烧录不可改 |

## Hard Rules

1. **完整烧录是 7 镜像**：bl / ota / ap / apc1 / bth / bthcp / hifi（+tee）。bl/ota 等只随 release 包发布，本仓编不出来——只烧 AP 时引导链仍是旧的。
2. **defconfig 或 rc.sysinit.ap 变更触发全量重编**：`1700_ap.sh` 对这两个文件做 hash，变了自动 rm 构建目录（几分钟）；只改 C 代码则增量（~12 秒）。
3. **erase-chip 不会变砖**：片内 ROM bootloader 擦不掉，dldtool 照常 SYNC；等 `Wait for SYNC` 后按板上 RESET 即可恢复烧录。
4. **出厂固件是"vela测试bin"**：开机会画彩带+旋转（屏幕/触摸测试）。替换它需要全量烧录 release 镜像集，仅换 AP 无效——引导权在测试版 bl/ota 手里。
5. **release 镜像来源**：随板资料《BES2800BP EVB说明.docx》——rar 以 **OLE 对象内嵌在 docx 里**（`word/embeddings/oleObject1.bin`），Mac 打不开属正常，用 unzip+7z 提取。

## Workflows

### 编译

```bash
./build.sh vendor/bes/boards/best1700_ep/aos_evb/configs/ap --cmake -j8
# 产物: cmake_out/aos_evb_ap/nuttx_ap.bin（及 .elf/.map）
# 成功标志: "build completed successfully"
```

### 烧录（AP-only，日常迭代）

```bash
./dldtool /dev/ttyUSB0 --reboot programmer1700_dual.bin \
  --set-dual-chip 1 -M nuttx_ap.bin --pgm-rate 2000000
# Windows: dldtool.exe <COM口号> ...；SYNC 后按 RESET
```

全量烧录顺序（官方 updateall.bat）：`-M bl → --set-dual-chip 1 -M ota → -M ap → -M apc1 → -M bth → -M bthcp → --addr 0x30D90000 hifi`。一键脚本见 `scripts/flash_ap.sh`。

### 新功能可行性诊断（先于编码）

```bash
# 1. 库里有没有实现
cd /tmp && mkdir libx && cd libx && ar x <configs/ap/*.a>
nm *.o | grep -iE " T <keyword>"

# 2. 有没有调用方（孤儿符号 = 死代码）
for o in *.o; do nm $o | grep -q " U <symbol>" && echo "CALLER: $o"; done
# 3. 重定位兜底（同 .o 内部调用不走符号表）
for o in *.o; do readelf -r $o | grep -q "<symbol>" && echo "RELOC: $o"; done
```

零调用方 → 该功能在本发布版不可达，找 release 包或换路线。

## Anti-Patterns

| 反模式 | 后果 |
|---|---|
| 用 `prebuilts/gcc/linux-aarch64` 工具链在 x86 主机跑 nm/objdump | Exec format error 被 `2>/dev/null` 吞掉，得出"符号不存在"的错误结论 |
| 假设 BL 阶段画的图案来自 AP 固件 | 白改一天代码——先用 strings 验证目标行为在哪个镜像 |
| 靠 `strings .a \| grep` 判断功能存在 | 只能证明字符串存在；调用链必须用 nm/readelf 验证 |

## Where to Find Current Truth

| 内容 | 位置 |
|---|---|
| 板级入口 / init 脚本 | `vendor/bes/boards/best1700_ep/aos_evb/src/` |
| defconfig | `vendor/bes/boards/best1700_ep/aos_evb/configs/ap/defconfig` |
| 预编译库 | `configs/ap/*.a`（libbesboard / libbeschip / libnx_bestbsp） |
| 烧录工具 | `vendor/bes/prebuild/m1/dldtool`（Linux）`dldtool.exe`（Windows） |
| programmer | `vendor/bes/prebuild/programmer1700_dual.bin` |

符号取证详细流程（工具链选择、反汇编、依赖链追踪）：读 [references/library-forensics.md](references/library-forensics.md)。
