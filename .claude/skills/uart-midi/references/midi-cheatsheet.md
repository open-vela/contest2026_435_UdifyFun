# MIDI 1.0 消息速查（面向嵌入式发送端）

## 通道消息（status 高 4 位 = 类型，低 4 位 = 通道 0–15）

| 类型 | status | 数据字节 | 说明 |
|---|---|---|---|
| NoteOff | 0x80\|ch | note, vel | vel 通常 0 |
| NoteOn | 0x90\|ch | note, vel | vel=0 等价 NoteOff（running status 常用） |
| PolyAftertouch | 0xA0\|ch | note, pressure | |
| ControlChange | 0xB0\|ch | cc, value | 见常用 CC 表 |
| ProgramChange | 0xC0\|ch | program | **仅 2 字节！** |
| ChannelAftertouch | 0xD0\|ch | pressure | 2 字节 |
| PitchBend | 0xE0\|ch | lsb, msb | 14 位，中立值 8192 (0x2000) |

系统实时：0xF8 Clock / 0xFA Start / 0xFC Stop / 0xFE ActiveSensing —— 可插在任意字节间。

## 常用 CC

| CC | 名称 | 典型用途 |
|---|---|---|
| 1 | Modulation | 效果强度（本项目 IMU 握拳上下映射） |
| 7 | Channel Volume | |
| 10 | Pan | |
| 64 | Sustain Pedal | >64 = on |
| 91 | Reverb Depth | **效果湿度 wet/dry（本项目 IMU 上下位移映射）** |
| 93 | Chorus Depth | |

## GM Program（0-indexed；界面显示号 = +1）

| PC | 音色 | PC | 音色 |
|---|---|---|---|
| 0 | Acoustic Grand Piano | 24 | Nylon Guitar |
| 33 | Fingered Bass | 40 | Violin |
| 48 | String Ensemble 1 | 73 | Flute |
| 80 | Square Lead | 88 | Pad 1 (new age) |
| 10 | **Music Box** | 11 | Vibraphone |

打击乐：通道 10（ch 索引 9）。GM 鼓映射：36 Kick、38 Snare、42 Closed HH、46 Open HH、49 Crash。

## 音符号 ↔ 音名

`note = (octave + 1) * 12 + semitone`；C4 = 60（中央 C）。
半音：C=0 C#=1 D=2 D#=3 E=4 F=5 F#=6 G=7 G#=8 A=9 A#=10 B=11。

## 发送端实现要点

- 数据字节必须 & 0x7f（高位留给 status）
- PC 只发 2 字节（`0xC0|ch, prog`）——按 3 字节发送会把下一个消息首字节吃掉
- 和弦 = 多条 NoteOn 连发 + 对应 NoteOff 连发；**记录每个挂起音**，停止时逐一 off
- 单音乐器序列器：新音前先 off 上一音（mono legato），或用 running status 省带宽
- 波特率 921600 下 3 字节消息 ≈ 31µs，无速率顾虑
