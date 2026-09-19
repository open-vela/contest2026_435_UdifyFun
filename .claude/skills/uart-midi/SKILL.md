---
name: uart-midi
description: "在 USB 串口桥链路上输出标准 MIDI 控制 DAW：板端字节流 + PC 端虚拟 MIDI 口桥接，跨 macOS/Linux。Use when: 嵌入式设备需要 MIDI 输出、USB DCD 缺失时的替代方案、写 MIDI NoteOn/Off/CC/PC、ttymidi 类桥接脚本、DAW 排查无声。触发词：MIDI、DAW、rtmidi、桥接、NoteOn、CC91、Program Change、GarageBand。"
---

# UART-MIDI：串口上的标准 MIDI 输出

当平台缺少 USB 设备控制器（无法做 USB-MIDI class 枚举）时的等价方案：
板端把标准 MIDI 字节流写进控制台 UART，经板载 USB-串口桥到 PC，
PC 侧桥接脚本还原为系统虚拟 MIDI 口。**DAW 体验与真 USB-MIDI 完全一致**。

## 决策表

| 场景 | 方案 |
|---|---|
| 平台有 USB DCD + MIDI class 驱动 | 真 USB-MIDI（免 PC 侧软件） |
| 无 DCD（库模式 BSP 常见） | **UART-MIDI + 桥接脚本**（本方案） |
| 只需脱机发声 | 板端合成器（不经 MIDI） |

## 板端要点

```c
/* H4 无关，直接写字节流即可；O_NONBLOCK 防 console 阻塞 */
static void midi_send(uint8_t b0, uint8_t b1, uint8_t b2)
{
  uint8_t msg[3] = { b0, b1 & 0x7f, b2 & 0x7f };
  write(g_midifd, msg, 3);   /* fd = open("/dev/ttyS0", O_WRONLY|O_NONBLOCK) */
}
/* NoteOn  0x90|ch, note, vel ; NoteOff 0x80|ch, note, 0
   CC      0xB0|ch, cc, val  ; PC      0xC0|ch, prog  (PC 只有 2 字节!) */
```

- **单音纪律**：新 NoteOn 前先对上一个音发 NoteOff，防止挂音
- 停止路径必须 all-notes-off（含 CC/和弦的每个音）
- 波特率对齐：板端 console 速率（Vela 为 921600）= 桥接脚本速率

## PC 端桥接

可直接复用本仓成品：`.claude/skills/uart-midi/scripts/uart_midi_bridge.py`
（也是 `app/apollia_hub/tools/` 的运行版）。依赖 `pip3 install pyserial python-rtmidi`。

```bash
python3 uart_midi_bridge.py                # 端口自动探测
python3 uart_midi_bridge.py /dev/cu.usbserial-1410 921600   # 显式指定
```

脚本要点（改动时保持）：

- **端口探测顺序**：macOS `/dev/cu.usbserial*`、`/dev/cu.wchusbserial*`、`/dev/cu.usbmodem*`；
  Linux `/dev/ttyUSB*`、`/dev/ttyACM*`。多候选时列出让用户选（EVB 双 CH343：下载口+控制台口，
  **选能敲出 NSH 提示符的那个**）
- **macOS 必须用 `cu.`**：`tty.*` 等载波检测会挂死
- **解析器**：realtime(≥0xF8) 透传；status 字节重置 running status；
  按 status nibble 定长收满再转发；无法解析的字节静默丢弃（控制台与 MIDI 共线，
  NSH/syslog 噪声是常态——弹奏时提醒用户别在 NSH 敲命令）

## DAW 侧验证

| 平台 | 方法 |
|---|---|
| macOS | 系统自带 "Audio MIDI Setup" 应用应列出 "Apollia UART-MIDI"；GarageBand 建 Software Instrument 轨即收 |
| Linux | `aseqdump -p "Apollia"` 看消息流；qsynth/FluidSynth 选该端口 |
| Windows | loopMIDI 建口 + 桥接脚本改用 `COMx`（rtmidi 虚拟口需 loopMIDI） |

## Anti-Patterns

| 反模式 | 后果 |
|---|---|
| 忽略 PC 只读 1 字节即返回的短读 | 半包死锁——循环读满目标长度，每次带超时 |
| write 一次完事（O_NONBLOCK） | 短写丢字节——循环写满 + EAGAIN 重试 |
| 台架直连 `/dev/tty.*`（macOS） | 等 DCD 挂死 |
| 停止播放时不发 NoteOff | DAW 永久挂音 |

## Where to Find

| 内容 | 位置 |
|---|---|
| 桥接脚本（成品） | 本 skill `scripts/uart_midi_bridge.py` |
| MIDI 消息格式速查 | [references/midi-cheatsheet.md](references/midi-cheatsheet.md) |
| 板端示例实现 | `app/apollia_hub/apollia_hub_main.c`（MIDI 节 + 序列器） |
