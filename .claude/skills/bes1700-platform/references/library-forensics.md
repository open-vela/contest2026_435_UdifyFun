# 库符号取证（Library Forensics）

判定预编译库中某功能"是否存在且可达"的标准流程。全部结论必须由符号表 + 重定位表支撑。

## 0. 工具链铁律

用**主机架构匹配的 binutils**：

```bash
which nm readelf objdump          # 主机版即可读任意 ELF 的符号表
arm-none-eabi-objdump -d x.o      # 反汇编用 /usr/bin/（发行版自带）
```

`prebuilts/gcc/linux-aarch64/...` 是 ARM64 主机版，x86 上执行报
`Exec format error`。若把它写进 `2>/dev/null` 管道，会静默产出空结果，
**制造"符号不存在"的假象**——这是最危险的取证错误。执行任何工具前先验证退出码。

## 1. 解包与枚举

```bash
mkdir -p /tmp/forensics && cd /tmp/forensics
ar x <workspace>/vendor/bes/boards/best1700_ep/aos_evb/configs/ap/libbeschip_ap.a
# 注意：多个 .a 解到同一目录时同名 .o 会互相覆盖，分目录解包
```

## 2. 三级证据链

| 级别 | 手段 | 能证明 | 不能证明 |
|---|---|---|---|
| L1 字符串 | `strings x.o \| grep kw` | 数据/日志存在 | 代码可达 |
| L2 符号表 | `nm x.o`（T/U/t） | 定义与跨 .o 引用 | 同 .o 内部静态调用 |
| L3 重定位 | `readelf -r x.o` | 全部调用边（含同 .o 内） | 运行时函数指针 |

结论规则：**L3 找不到任何引用 = 孤儿符号 = 运行时不可达**（库模式下调用方源码缺失则永远无法触发）。

```bash
SYM=bthci_register
# 定义者
for o in *.o; do nm $o | grep -qE "[tT] $SYM$" && echo "DEF: $o"; done
# 跨对象引用
for o in *.o; do nm $o | grep -qE " U $SYM$" && echo "CALLER: $o"; done
# 重定位兜底
for o in *.o; do readelf -r $o 2>/dev/null | grep -q "$SYM" && echo "RELOC: $o"; done
```

## 3. 最终镜像复核

.o 级结论还要在链接产物上复核（链接器可能裁掉未引用段）：

```bash
ELF=cmake_out/aos_evb_ap/nuttx_ap.elf
strings $ELF | grep -c "$SYM"        # debug info 残留的函数名
strings $ELF | grep -c "特征字符串"   # 业务字符串更可靠
```

nm 对最终 ELF 可能因 strip 而为空——**以 strings 结果为准**。

## 4. 依赖链追踪（评估"接线成本"）

若决定主动调用一个孤儿导出函数（如 `bthci_register()`），先查它的未定义依赖能否被链接器满足：

```bash
nm <定义者>.o | grep " U "           # 全部外部依赖
# 在最终镜像中确认关键依赖已被链接（如 hal_intersys_open、uart_register）
strings $ELF | grep -c "hal_intersys_open"
```

依赖的依赖（如 `hal_intersys.o`）通常随 `--whole-archive` 或按需链接自动带入；若缺失，链接期会报 undefined reference——编译即暴露。

## 5. 反汇编读签名（无源码时的最后一招）

```bash
arm-none-eabi-objdump -d x.o | grep -A40 "<func>:"
arm-none-eabi-objdump -r x.o | grep -B20 "<callee>"   # 重定位定位调用者函数
```

用途：确认函数参数（从 `movs r0, #N` 读立即数）、判断空函数（`movs r0,#0; pop {pc}`）、
找回调注册点（literal pool 加载函数指针）。

## 已验证案例速查

| 符号 | 结论 | 影响 |
|---|---|---|
| `bthci_register` | 孤儿（libbeschip 导出，零调用方）；主动调用可注册 /dev/ttyBT | ttyBT 节点可出现，但 BTH 核无 rptun 通道唤醒，HCI 无应答 |
| `up_rptun_init_ap2bth` | 完全不存在（库里只有 ap2apc1） | 蓝牙链路死路 |
| `hal_usb_*` 全系 | 孤儿，中断向量未接 | USB 设备类枚举死路 |
| `rotate_test` | 孤儿（bes_lcdc_rotate.o 内部） | 与开机画面无关 |
