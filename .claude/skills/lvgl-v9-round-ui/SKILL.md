---
name: lvgl-v9-round-ui
description: "LVGL v9 圆屏 UI 开发规范：动画回调签名、坐标缓存时序、圆形安全区布局、粒子与序列器模式。Use when: 在圆屏/手表设备上写 LVGL 界面、使用 lv_anim 动画、set_size 后读取坐标、极坐标布局、触摸事件处理、排查 UI crash 或布局错位。触发词：LVGL、圆屏、lv_anim、exec_cb、安全区、粒子、布局不对称。"
---

# LVGL v9 圆屏 UI 开发

三个本项目实测踩过的坑构成核心规则；违反任何一条都会产生难定位的运行时故障。

## Hard Rules

### 1. 动画回调签名不可混用（违反 = 堆损坏 crash）

```c
typedef void (*lv_anim_exec_xcb_t)(void *, lv_anim_value_t);
/* 定时器调用: f(a->var, v) —— 给 lv_obj_set_x/y、lv_arc_set_value 这类用 */

typedef void (*lv_anim_custom_exec_cb_t)(lv_anim_t *, lv_anim_value_t);
/* 定时器调用: f(a, v) —— 第一参是 anim 本体，仅当你需要 anim 内部字段时用 */
```

给 `(lv_obj_t*, int32_t)` 签名的 setter 做动画必须用：

```c
lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_x);   /* ✓ */
/* lv_anim_set_custom_exec_cb(..., (lv_anim_custom_exec_cb_t)lv_obj_set_x)
   会把 anim 堆节点当 obj 写坐标 → 首个 tick 即 crash，强转让编译器闭嘴 */
```

**强转任何回调前先读 typedef 原型**（`lvgl/src/misc/lv_anim.h`）。

### 2. set_size 后坐标不刷新（违反 = 布局整体偏移）

```c
/* lv_obj_pos.c: set_width 只写样式；get_width 读 coords 缓存 */
lv_obj_set_size(o, w, h);
lv_obj_set_pos(o, x - lv_obj_get_width(o) / 2, y);   /* ✗ 拿到旧值(≈0) */
```

修复三选一：显式传尺寸（推荐）／先 `lv_obj_update_layout(obj)`／用 `lv_obj_align*`（内部自带 update）。
症状实例：左右传感器弧镜像偏移半个身位——左侧多空白、右侧被裁切。

### 3. 圆屏布局必须做角点校验（违反 = 四角元素被裁）

矩形对齐 API（`LV_ALIGN_BOTTOM_LEFT` 等）落在圆外。规则：**元素外接矩形最远角点到屏幕中心距离 ≤ R−4**：

```c
/* 高度简化的安全放置：极坐标 + 显式宽高 */
static void place_polar(lv_obj_t * o, int32_t cx, int32_t cy,
                        int16_t deg, int32_t r, int32_t w, int32_t h)
{
  int32_t x = cx + (lv_trigo_cos(deg) * r) / 32768;
  int32_t y = cy - (lv_trigo_sin(deg) * r) / 32768;
  lv_obj_set_pos(o, x - w / 2, y - h / 2);
}
```

中心列（TOP_MID 水平居中）永远安全；角落文字一律沿弧摆放。`lv_trigo_sin/cos` 以度为单位、返回 ±32768，支持负角。

### 4. LVGL 仅主线程操作

工作线程（HCI/MIDI 采集等）结果写 `volatile` 变量，由 `lv_timer`（200–500ms）在主线程轮询刷新 UI。

## 常用模式

| 模式 | 要点 |
|---|---|
| 粒子池 | 预建 N 个 hidden 圆 + busy 标记；`completed_cb` 中 hide+回收；池满直接丢弃（MIDI 已发，视觉可丢） |
| 呼吸 halo | 低透明大圆 + 无限 playback 动画调 `lv_obj_set_style_opa`，各元素 delay 错相 |
| 传感器点按 | user_data 塞索引（或位编码旗标）；PRESSED 高亮+发事件，RELEASED/PRESS_LOST 都要恢复 |
| 自动演奏序列器 | `lv_timer` 步进音符表；与手动输入共存时熄灯前检查 `held` 标志 |
| 中心拖动改参数 | `lv_event_get_indev` + `lv_indev_get_point`（NULL 安全），PRESSING 事件算 delta |

## Anti-Patterns

| 反模式 | 后果 |
|---|---|
| 对同一文件并行发多个 fs.edit | 编辑互相覆盖产生残缺文件——同文件编辑必须串行 |
| completed_cb 里删除/替换**仍在飞的兄弟动画** | 同 var 不同 exec 的三个并行动画，回调中 start 新动画会 delete 未处理的兄弟；本版 LVGL 靠 anim_list_changed 从头重扫保护，但设计上应避免 |
| LV_EVENT_ALL 回调里做重活 | draw/style 事件每帧回灌；只按需响应 PRESSED/RELEASED/CLICKED |
| 中文进 UI 文本 | montserrat 字体无 CJK 字形——UI 文案用英文，中文只进文档 |

## Where to Find Current APIs

| 内容 | 位置 |
|---|---|
| 动画 typedef / 定时器语义 | `apps/graphics/lvgl/lvgl/src/misc/lv_anim.h` / `lv_anim.c` |
| 坐标与 layout | `apps/graphics/lvgl/lvgl/src/core/lv_obj_pos.c` |
| 三角函数 | `apps/graphics/lvgl/lvgl/src/misc/lv_math.h` |
| 输入设备 | `apps/graphics/lvgl/lvgl/src/indev/lv_indev.h` |

圆屏安全区计算详解与校验清单：读 [references/round-safe-area.md](references/round-safe-area.md)。
