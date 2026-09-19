---
name: openvela-contest-repo
description: "openvela 大赛仓工程化：manifest linkfile 软链工作流、竞赛仓/公共仓改动归属、LVGL demo 应用四件套骨架、提交与 PR 流程。Use when: 在竞赛仓新增应用、修改 contest xml manifest、判断 defconfig 改动归属、linkfile 软链失效排查、准备提交/发 PR。触发词：contest、linkfile、manifest、竞赛仓、参赛、提交、软链。"
---

# openvela 大赛仓工程化

竞赛仓（`contest2026_<编号>_<队名>`）既是代码仓又内置 repo manifest。
**核心纪律：作品代码只写在竞赛仓，经 `<linkfile>` 软链进编译树；生产仓库零改动。**

## 新增应用的标准流程（5 步，缺一不可）

```bash
# 1. 建目录 + 四件套（Kconfig/CMakeLists.txt/Make.defs/Makefile）
mkdir -p contest2026_xxx/app/<name> && cd contest2026_xxx/app/<name>

# 2. manifest 登记（contest2026_xxx.xml 的 <project> 内）
#    <linkfile src="app/<name>" dest="packages/demos/contest2026_xxx_<name>"/>

# 3. 手动建软链（等效 repo sync 的 linkfile，立即生效）
ln -sfn ../../contest2026_xxx/app/<name> packages/demos/contest2026_xxx_<name>

# 4. defconfig 启用 CONFIG_LVX_USE_DEMO_..._<NAME>=y   （公共仓，见归属表）

# 5. rcS 改开机启动（可选）：一行 <app_name> &
```

**验收**：`packages/demos/` 下软链存在且可 `ls` 穿透；`build completed successfully`；
`strings nuttx_ap.elf | grep <app>` 有输出；镜像内 init 脚本含新命令名。

## 应用四件套骨架要点

| 文件 | 关键内容 |
|---|---|
| Kconfig | `config LVX_USE_DEMO_CONTEST2026_<编号>_<NAME>`，`depends on GRAPHICS_LVGL`，help 写清用途 |
| CMakeLists.txt | `nuttx_add_application(NAME <cmd> SRCS <file>.c STACKSIZE 8192)` 包在 if(CONFIG_...) |
| Make.defs | `CONFIGURED_APPS += $(APPDIR)/packages/demos/contest2026_<编号>_<name>` |
| Makefile | PROGNAME/PRIORITY/STACKSIZE/MODULE/MAINSRC + `include $(APPDIR)/Application.mk` |

四者 CONFIG 宏名必须一致；顶层 `packages/demos` 自动扫描子目录（nuttx_add_subdirectory），无需登记。

## 改动归属决策表

| 改动 | 归属仓 | 说明 |
|---|---|---|
| 应用源码 / 文档 / 插画 / manifest | **竞赛仓** | 评委评估主体 |
| defconfig 启用应用 CONFIG | 公共仓 vendor_bes | 单独 commit，PR 走上游流程 |
| rcS 开机启动应用 | 公共仓 vendor_bes | 同上 |
| 桥接/烧录等 PC 工具 | 竞赛仓（应用 tools/ 下） | |
| 公共仓无关实验 | 不提交 | 还原原状再提交 |

## 提交流程

```bash
# 竞赛仓（GitHub，非 Gerrit）
git checkout -b dev-ai-contest-2026        # 与上游分支同名，便于发 PR
git add <明确路径> && git commit -s -m "<conventional message>"
git push <fork-remote> dev-ai-contest-2026
```

- fork remote 用 SSH：`git@github.com:<login>/contest2026_xxx.git`；
  仓库级固定密钥：`git config core.sshCommand "ssh -i ~/.ssh/id_ed25519_github -o IdentitiesOnly=yes"`
- GitHub PAT（fine-grained）push 403 常见原因：Repository access 未覆盖该仓 / Contents 未给
  Read-write——**改用 SSH key 更省事**（账号 Settings → SSH keys）
- 提交前 `git status` 必须干净：实验残留（旧 demo 目录、失效软链、无关 CONFIG）全部清理

## 常见故障

| 症状 | 原因 |
|---|---|
| Kconfig 找不到新应用 | 软链缺失或 linkfile dest 与实际目录名不一致 |
| 编译过但镜像无该命令 | CONFIG 未启用（defconfig 没加或依赖未满足）——查 `cmake_out/.../.config` 实际生效值 |
| 开机不进新应用 | rcS 未改，或改的是 rc.sysinit（职责不同：sysinit 挂载文件系统，rcS 起服务） |

## Where to Find

| 内容 | 位置 |
|---|---|
| manifest | `contest2026_xxx.xml`（仓根） |
| 已有应用范例 | `app/apollia_hub/`（完整 LVGL + MIDI + tools 集成） |
| defconfig | `vendor/bes/boards/best1700_ep/aos_evb/configs/ap/defconfig` |
| 开机脚本 | `.../src/etc/init.d/rcS.ap` |
