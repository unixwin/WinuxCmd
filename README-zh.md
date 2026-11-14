<!-- 2.0.0 文档草稿：数据核对于 origin/main f0cf6b17（2026-11-14）。
     WP-S 在发布 tag 时需刷新计数、产物名与链接。 -->

<a id="top"></a>

<div align="center">

<img src=".github/assets/banner.svg" alt="WinuxCmd — Unix 命令，Windows 原生。176 个命令，一个原生可执行文件，GNU 行为差分门。" width="100%">

**真正的 Unix 命令，原生的 Windows 路径，一个原生可执行文件。**
无需 WSL · 无需 Cygwin · 无需 MSYS2 · 没有路径转换的坑
**v2.0.0 —— Rust 命令核心。** 🎉

**[unixwin.dev](https://unixwin.dev) — 文档与下载 (Docs & downloads)**

[![GitHub release](https://img.shields.io/github/v/release/unixwin/WinuxCmd)](https://github.com/unixwin/WinuxCmd/releases)
[![GitHub downloads](https://img.shields.io/github/downloads/unixwin/WinuxCmd/total)](https://github.com/unixwin/WinuxCmd/releases)
[![Stars](https://img.shields.io/github/stars/unixwin/WinuxCmd/stargazers)](https://github.com/unixwin/WinuxCmd/stargazers)
[![License](https://img.shields.io/github/license/unixwin/WinuxCmd)](LICENSE)
![Platform](https://img.shields.io/badge/platform-Windows%20x64%20%7C%20ARM64-blue)

[💾 安装](#安装) · [🚀 2.0 新在哪](#-20-新在哪) · [⚡ 快速开始](#-快速开始) · [🔁 从-1x-升级](#-从-1x-升级) · [📚 文档](#-文档) · [English](README.md)

</div>

---

## 一句话说清痛点

你在 Windows 上需要 `grep -rn`、`sed -i`、`find -exec`、`xargs -0`——但目前为止每条路都是妥协：

- **WSL** —— 1 GB 起步，文件还隔着一层虚拟机文件系统
- **Cygwin** —— 路径转换的地狱，启动还要等 2–5 秒
- **GnuWin32** —— 2012 年起停更，兼容性停在 60%
- **uutils** —— 优秀的 Rust 项目，但只有约 100 个命令、约 600 个选项

**WinuxCmd 直接跳过妥协。** 一个原生 Win32 可执行文件，在 Windows 路径上讲地道的
GNU 语法：**176 个命令**，无运行时、无虚拟机。2.0 把命令核心换成 Rust，行为仍以
GNU oracle 逐字节对照。

## 🚀 2.0 新在哪

2.0 是**实现层**换代，不是接口换代：命令、命令名、选项、`--help` 面、multicall
都是同一套。变的是底下跑的东西。

```
winuxcmd.exe（一个镜像、一套 CRT）
├── C++ 前端          注册元数据、调度器、选项策略、
│                     help/version 拦截、wpm、保留面
├── rust2_bridge      唯一接触 ABI 的 C++
├── winuxcmd_ffi      C ABI v2：winuxcmd_run / winuxcmd_run_stream
└── winux-cmds        库形态命令
    └── winux-core    Io / Ctx / HostServices / path / regex 接缝
```

- **库形态命令**——每个已接线 applet 就是一个函数：
  `run(args, &mut Io, &Ctx) -> CmdResult`。IO 显式注入，cwd 与环境显式传入，
  没有进程级可变全局状态，返回状态码而不是 `exit()`。命令 panic 在 ABI 边界
  转成状态 125，不会展开进调度器。
- **一个镜像**——Rust 侧是**静态库**，和 `winuxcmd.exe` 在同一次 MSVC 链接里
  合成，无 DLL、无加载期 CRT 边界、无额外安装项。这正是第一次 Rust 尝试被
  FFI 阻断（#1487）的修复方式。
- **流式 stdin**——`winuxcmd_run_stream` 用拉取回调按需喂入，命令增量读取，
  ABI 层不再整块复制。宿主停止喂入返回 GNU 的 124；读失败映射为命令自己的状态。
- **`winux-regex`**——std-only 的 BRE/ERE 引擎（GNU 扩展、反引用、POSIX
  leftmost-longest、glibc 捕获规则），供 `grep`/`sed`/`find`/`tac` 等共享。
  `grep -P` 保留既有的钉死 PCRE2 面，不重写 Perl 语法。
- **无全局翻译**——运行时消息带稳定 key 与英文 fallback；译文按调用从
  `WINUX_LANG` 选中的 catalog 注入。默认英文构建。
- **更严的门**——每个接线命令过三重门：GNU oracle 逐字节、ctest 失败集不变、
  新旧 exe 逐字节矩阵且每条差异有解释。

2.0 冻结时，**50 个 applet 已跑 Rust 核心**，**32 个库已就绪、差分门全绿**，
排在 2.0.x 接线波次；其余 applet 在轮到之前保持 1.x 核心。逐命令权威状态见
[`DOCS/2.0-inventory.tsv`](DOCS/2.0-inventory.tsv)，架构与接线契约见
[`rust2/REPLACEMENT-RECIPE.md`](rust2/REPLACEMENT-RECIPE.md)。

## ⚡ 快速开始

```bash
# 任选一种方式安装，然后直接使用命令：
ls -la
grep -rn "TODO" src/
sed -i 's/http:/https:/g' config.ini
find . -name "*.tmp" -exec rm {} \;

# 完整管道，零配置——命令链接直接注入 PATH，
# 不带前缀、不用配置
find . -name "*.cpp" -print0 | xargs -0 wc -l

# 每个 applet 也可经 multicall 二进制调用：
winuxcmd grep -c ERROR app.log
winuxcmd --list-all

# 不该重复造轮子的工具，走 WPM：
wpm install jq
```

确认你在跑什么：

```bash
winuxcmd --version
winuxcmd <command> --help
```

### 安装

| | |
|---|---|
| **winget** | `winget install WinuxCmd`（ID `caomengxuan666.WinuxCmd`） |
| **安装器（推荐）** | 从 [GitHub Releases](https://github.com/unixwin/WinuxCmd/releases/latest) 取 `WinuxCmd-<version>-x64-setup.exe`（或 ARM64 安装器） |
| **便携版** | 解压 `WinuxCmd-<version>-win-x64.zip` 到任意目录，加入 `PATH` |
| **源码构建** | VS 2022 + CMake 3.30 + Ninja + Rust，见[源码构建](#️-源码构建) |

## 🔁 从 1.x 升级

命令集不变。1.x 自己发明过的地方，2.0 回归 GNU。升级脚本前请读完整
**[迁移指南](DOCS/2.0-MIGRATION.md)**；速览：

| 方面 | 1.x | 2.0 |
|---|---|---|
| 实现 | C++ 主体在 `winuxcmd.exe` | Rust 库函数，同一个镜像 |
| 命令集 / 命令名 / 选项 | 176 applet | **不变**（退役的 1.x 私有拼写除外） |
| `--help` / `--version` 文本 | WinuxCmd 品牌 | **不变**（同一调度器面） |
| GNU 行为 | 实测，带跟踪偏差 | 设门面 GNU 逐字节；剩余偏差全部分类 |
| `sort` 速度 | 参照 | 64 MiB NTFS 基准纯排序快 12 倍 |
| 流式 stdin | 整块缓冲 ABI | 增量拉取回调（v2） |
| 翻译 | C++ catalog | 按调用宿主注入 + 独立 catalog 仓库 |
| Edition / multicall / WPM | default + full | **不变** |
| Windows 子集 | NTFS 权限位、属主、符号链接、SELinux、FIFO | **不变，有文档** |

**退役的 1.x 扩展**（GNU 没有该选项，或把它当数据）：
`echo -u/--upper`、`echo -r/--repeat`、`fold -c`、
`fmt -m/-P/-x/-X/-T`、`numfmt --unit-separator/-M/-l` 以及 1.x 接受的
`Z/Y/R/Q` 后缀。每条 GNU 依据见迁移指南 §3。

**尚未变化**：仍在 1.x 核心上的 applet 行为与 1.2.1 完全一致；它们的 GNU
差异按 `compat-gap` issue（#1639–#1657）跟踪，由对应的 Rust 重写修复，
不是 2.0.0 的回归。

## 🧰 命令覆盖

176 个 applet，覆盖 GNU coreutils、findutils、grep/sed、util-linux、BSD
工具、进程管理、Cygwin/MSYS 桥接和 WinuxCmd 扩展。命令面与 1.x 相同，
变化的是每个 applet 背后的引擎。

| 家族 | 示例 |
|---|---|
| 文件操作 | `ls` `cp` `mv` `rm` `ln` `mkdir` `install` `stat` `du` `df` |
| 文本处理 | `cat` `head` `tail` `cut` `tr` `sort` `uniq` `sed` `grep` |
| 查找与遍历 | `find` `xargs` `locate` `updatedb` `whereis` `which` |
| 摘要与编码 | `md5sum` `sha*sum` `b2sum` `cksum` `base64` `base32` `basenc` |
| 路径与链接 | `readlink` `realpath` `basename` `dirname` `pathchk` `cygpath` |
| 进程与系统 | `ps` `top` `kill` `timeout` `nice` `nohup` `free` `uptime` |
| 权限 | `chmod` `chown` `chgrp` `getfacl` `chcon` `runcon` |
| 终端与文档 | `tty` `stty` `tput` `clear` `reset` `man` `less` `more` |
| WinuxCmd 扩展 | `wpm` `regtool` `mkgroup` `mkpasswd` `d2u` `u2d` |

逐命令状态（Rust 已接线 / 延后 / 保留面）与 issue 锚点见
[`DOCS/2.0-inventory.tsv`](DOCS/2.0-inventory.tsv)，摘要见
[`DOCS/2.0-inventory-summary.md`](DOCS/2.0-inventory-summary.md)。

## 🧪 测试与 GNU 验证

2.0 对每个接线命令设三道门：

1. **GNU oracle**——接线后的产品 vs WSL GNU（coreutils 9.4、findutils
   4.9.0、util-linux 2.39.3、Debian hostname 3.23），`strict` 行 stdout
   与退出码逐字节。
2. **既有测试**——全量 `ctest` 失败集必须等于接线前基线。
3. **新旧 exe**——参数矩阵比较 rc/stdout/stderr；每条差异必须解释为
   替代版修正的 1.x 偏差。

用例表由 `rust2/gate/gen-cases.py` 生成；每行分类为
`strict`/`exit`/`win`/`note`，`note` 无登记即为 finding。实测叙事见
[`rust2/GATE-REPORT.md`](rust2/GATE-REPORT.md)；wave5 全量跑（2026-11-13）
覆盖 82 命令 / 2,444 例，xargs 另加 141 例。发布跑会替换这些数字。

Windows 值模型行（`id`、`uname`、`hostname`、`getconf`、`df`、`whoami`、
`nproc`）对 GNU 设 rc 门、对 1.x 值模型设 stdout 门；记录在案，不跳过。

## 📚 文档

| 文档 | 说明 |
|---|---|
| [迁移指南 1.x → 2.0](DOCS/2.0-MIGRATION.md) | 逐命令差异、退役扩展、平台子集 |
| [2.0.0 发布说明](DOCS/2.0-RELEASE-NOTES.md) | 中英双语（tag 前为草稿） |
| [命令清点表](DOCS/2.0-inventory.tsv) | 176 applet、三态、豁免锚点 |
| [清点摘要](DOCS/2.0-inventory-summary.md) | 方法、计数、终版状态 |
| [正则引擎决策](DOCS/2.0-regex-engine-decision.md) | 为什么移植 `winux-regex`，以及 `grep -P` 豁免 |
| [替换配方](rust2/REPLACEMENT-RECIPE.md) | 单个 applet 如何接线（三重门、分引擎、流式、I18N） |
| [Gate 报告](rust2/GATE-REPORT.md) | 逐波差分结果 |
| [兼容矩阵](DOCS/en/command_compatibility_matrix.md) | 1.x 时代逐命令支持说明 |
| [WPM 指南](DOCS/en/wpm_guide.md) | 包管理器用户指南 |
| [Windows 特性](DOCS/en/windows_features.md) | Windows 专有行为 |

## 📦 Edition

WinuxCmd 用同一份源码构建两个 edition：

| | **Default edition** | **Full edition** |
|---|---|---|
| 资产 | `WinuxCmd-<version>-win-<arch>.*` | `WinuxCmd-<version>-win-<arch>-full.*` |
| 入口链接 | 除下列 full-only 名字外的全部命令 | 全部入口 |
| wpm 索引 | 是 | **不索引**——仅独立下载 |

**Full-only 命令：** default 版不为
**`[`** · **`env`** · **`false`** · **`kill`** · **`test`** · **`time`** ·
**`true`** 物化入口链接。它们的用户面语义由宿主 shell 内建决定，
`kill.exe` 这类入口会宣告一个其实由内建执行的调度。命令本体在
multicall 二进制里完整存在——`winuxcmd kill -9 <pid>` 两版都可用。
`echo`、`printf`、`pwd` 刻意保留入口。定义只在一处
（[`src/core/dispatcher.cppm`](src/core/dispatcher.cppm)
`is_full_only_command`）。

## 📦 WPM 包管理器

```bash
wpm install jq          # JSON 处理器
wpm install goawk       # awk 实现
wpm install bsdtar      # BSD tar
wpm install openssh     # SSH 客户端
wpm install make        # GNU make
wpm install neovim      # 文本编辑器

wpm search json         # 搜索包
wpm list --all          # 查看已安装
```

详见 [WPM 用户指南](DOCS/en/wpm_guide.md)。

## 🐂 更好的搭档：niubash shell

WinuxCmd 给 Windows 真正的 Unix **命令**，
[niubash](https://github.com/unixwin/niubash) 给它们真正的 **bash 语言**：

```bash
for f in *.log; do
  grep -c ERROR "$f" | xargs -I{} echo "$f: {} errors"
done | sort -t: -k2 -rn | head -5
```

niubash 启动时把 WinuxCmd 的命令链接注入 `PATH`，上面的 `grep`、`sed`、
`find` 就是真实二进制。

## 🛠️ 源码构建

**前置：** Visual Studio 2022+ · CMake 3.30+ · Ninja · Rust（stable，两个
MSVC target）。

```powershell
# 构建（一律用脚本，它会准备 VS 环境）
./scripts/build-with-vs.ps1

# 构建 full edition（全部入口链接，"-full" 资产名）
cmake -B build-full -G Ninja -DCMAKE_BUILD_TYPE=Release -DWINUXCMD_PACKAGE_EDITION=full

# 测试
./scripts/build-with-vs.ps1 -Target winuxcmd-tests
build-vs/tests/winuxcmd-tests.exe
```

### Rust 工具链（rust2 命令库，#1488）

- **安装 [rustup](https://rustup.rs)（stable）与两个 MSVC target：**
  `rustup target add x86_64-pc-windows-msvc aarch64-pc-windows-msvc`。
  即使在 x64 宿主上交叉构建 arm64，也必须装 `aarch64` target——cargo 按
  C++ 目标架构用 `--target` 构建静态库。CI 钉死工具链版本
  （`.github/workflows/build-winuxcmd.yml` 的 `RUST2_TOOLCHAIN`）。
- **`WINUXCMD_ENABLE_RUST2`** 在找到 cargo 时默认 **ON**，否则 **OFF**；
  无 cargo 却显式 `-DWINUXCMD_ENABLE_RUST2=ON` 是配置期错误。
- **没有 C++ 回退体。** 无 Rust 的构建会省略
  `WINUXCMD_RUST2_COMMANDS`（当前 50 个）里的全部命令；同一命令永不双轨
  （#1550）。官方构建带 Rust。
- **跨架构一致：** CI 的 `rust2-surface-check` 要求 x64 与 arm64 包的
  rust2 面相同。
- **跑 Rust 测试：**

  ```bash
  cd rust2
  cargo test -p winux-core -p winux-regex -p winux-cmds
  ```

**Profile-Guided Optimization（x64）：** 发布 CI 用 PGO 构建 x64 二进制。
本地复现：

```powershell
./scripts/build-pgo.ps1          # -> build-pgo/usr/bin/winuxcmd.exe
```

训练负载在 `scripts/pgo-train.sh`；ARM64 不做 PGO（profile 必须在目标架构
上采集）。

## 🤝 参与贡献

欢迎贡献——见 [CONTRIBUTING.md](CONTRIBUTING.md)。改 Rust 核心前先读
[`rust2/REPLACEMENT-RECIPE.md`](rust2/REPLACEMENT-RECIPE.md)：替换是机械的，
同一次改动删掉旧体，并过三重门。

## License

MIT —— 见 [LICENSE](LICENSE)。PCRE2 是钉死版本的静态依赖
（pcre2-10.47），许可证文本随源码树分发。

---

<div align="center">

**WinuxCmd** —— 因为 `ls` 不该要求一个 Linux 内核。
搭配 [**niubash**](https://github.com/unixwin/niubash) —— 在 Windows 上
流利说 GNU 的原生 bash shell。

[⬆ 回到顶部](#top)

</div>
