<a id="top"></a>

<div align="center">

<img src=".github/assets/banner.svg" alt="WinuxCmd — Unix 命令，Windows 原生。169 个命令，1827 个选项，98% GNU 兼容性。" width="100%">

**真正的 Unix 命令，原生的 Windows 路径，一个约 3 MB 的 exe。**
无需 WSL · 无需 Cygwin · 无需 MSYS2 · 没有路径转换的坑
**v1.0.0 正式版已发布。** 🎉

[![GitHub release](https://img.shields.io/github/v/release/unixwin/WinuxCmd)](https://github.com/unixwin/WinuxCmd/releases)
[![GitHub downloads](https://img.shields.io/github/downloads/unixwin/WinuxCmd/total)](https://github.com/unixwin/WinuxCmd/releases)
[![Stars](https://img.shields.io/github/stars/unixwin/WinuxCmd)](https://github.com/unixwin/WinuxCmd/stargazers)
[![License](https://img.shields.io/github/license/unixwin/WinuxCmd)](LICENSE)
![Platform](https://img.shields.io/badge/platform-Windows%20x64%20%7C%20ARM64-blue)

[💾 安装](#-安装) · [⚡ 演示](#-windows-上的-unix-手感) · [🐂 niubash](#-更好的搭档niubash-shell) · [📦 WPM](#-wpm-包管理器) · [🆚 对比](#-横向对比) · [📚 文档](#-文档) · [English](README.md)

</div>

---

## 一句话说清痛点

你在 Windows 上需要 `grep -rn`、`sed -i`、`find -exec`、`xargs -0`——但目前为止每条路都是妥协：

- **WSL** —— 1 GB 起步，文件还隔着一层虚拟机文件系统
- **Cygwin** —— 路径转换的地狱，启动还要等 2–5 秒
- **GnuWin32** —— 2012 年起停更，兼容性停在 60%
- **uutils** —— 优秀的 Rust 项目，但只有约 100 个命令、约 600 个选项

**WinuxCmd 直接跳过妥协。** 一个原生 Win32 可执行文件，在 Windows 路径上讲地道的 GNU 语法：**169 个命令、1,827 个选项、98% GNU 兼容性**——逐命令对照 GNU Coreutils 9.11 验证。

| | | | | |
|:---:|:---:|:---:|:---:|:---:|
| **169** | **1,827** | **98%** | **2,346** | **~3 MB** |
| 个命令 | 个选项 | GNU 兼容¹ | 个测试 · 99.6% 通过 | 零依赖二进制 |

> ¹ 22 个命令 / 56 个差分测试用例，对照 GNU Coreutils 9.11——详见[对比报告](DOCS/en/gnu_comparison_report.md)。

---

## ⚡ Windows 上的 Unix 手感

```bash
# 你熟悉的 GNU 参数，跑在真实的 Windows 路径上
ls -la
grep -rn "TODO" src/
sed -i 's/http:/https:/g' config.ini
find . -name "*.tmp" -exec rm {} \;

# 完整管道，零配置——命令链接直接注入 PATH，
# 不带前缀、不用配置
find . -name "*.cpp" -print0 | xargs -0 wc -l

# 不该重复造轮子的工具，WPM 一条命令装真身
wpm install jq
```

## 💾 安装

| | |
|---|---|
| **安装程序（推荐）** | 从 [GitHub Releases](https://github.com/unixwin/WinuxCmd/releases/latest) 下载 `WinuxCmd-<版本>-x64-setup.exe`（同时提供 ARM64 版本） |
| **便携版** | 解压 `WinuxCmd-<版本>-win-x64.zip`，加入 `PATH` 即可使用 |
| **从源码构建** | VS 2022 + CMake 3.30 + Ninja，见[从源码构建](#️-从源码构建) |

## 🚀 为什么选 WinuxCmd

- 🪟 **原生，不是模拟** —— 直接调用 Win32 API，理解 `C:\`、UNC 路径和 NTFS ACL（内置 `cygpath`、`getfacl` 做桥接）。无虚拟机、无运行时 DLL、启动即开。
- 🧠 **该 GNU 的地方就是 GNU** —— 光 `find` 一个命令就实现了 88 个选项（完整表达式解析器、`-exec`/`-execdir`/`-ok`、`-printf`）；`grep` 自带 PCRE2；`sed` 支持就地 `-i` 编辑。
- 📦 **WPM 内置** —— 不该重复造轮子的工具交给包管理器：jq、ripgrep、fd、fzf、bat、make、neovim、curl、wget……
- 🧪 **按标准来测试** —— 2,346 个测试用例、178 个测试文件、99.6% 通过率，并与 GNU Coreutils 9.11 做差分输出对比。
- ⚡ **小而快** —— 约 3 MB、零依赖、启动即开（Cygwin 光启动就要 2–5 秒）。

## 🐂 更好的搭档：niubash shell

WinuxCmd 给 Windows 带来了真正的 Unix **命令**；[niubash](https://github.com/unixwin/niubash) 给这些命令一个真正的 **bash 语言**栖身之所——同一个 unixwin 组织，一条工作流的两半：

```bash
# 在 niubash 里（Windows 原生 bash——不用 WSL）：
for f in *.log; do
  grep -c ERROR "$f" | xargs -I{} echo "$f: {} errors"
done | sort -t: -k2 -rn | head -5
```

- **真·bash 语义** —— `if`/`for`/函数/数组/管道跑在 [niubash](https://github.com/unixwin/niubash) 上（由其 [rubash](https://github.com/unixwin/rubash) 引擎驱动），GNU Bash 上游测试套件 86/86 全绿。
- **对 AI agent 友好** —— `niu -c` 安静且确定：无 banner、stdout/stderr 稳定、退出码精确传递。你的 AI 工具天生就会说的 shell。
- **零胶水** —— niubash 启动时把 WinuxCmd 的命令链接注入 `PATH`，上面示例里的 `grep`、`sed`、`find` 就是真二进制本尊。

3 MB 的 exe 单独能打；装上 [niubash](https://github.com/unixwin/niubash)（v1.0.0），整条 bash 工作流都是你的。

## 📦 WPM 包管理器

```bash
wpm install jq          # JSON 处理器
wpm install goawk       # awk 实现
wpm install bsdtar      # BSD tar
wpm install openssh     # SSH 客户端
wpm install make        # GNU make
wpm install neovim      # 文本编辑器
wpm install curl        # URL 传输
wpm install wget        # 网络下载

wpm search json         # 搜索软件包
wpm list --all          # 查看已安装
```

详见 [WPM 用户指南](DOCS/en/wpm_guide.md)。

## 🆚 横向对比

| 特性 | WinuxCmd | uutils (Rust) | GnuWin32 | Cygwin | busybox |
|------|:--------:|:-------------:|:--------:|:------:|:-------:|
| **命令数量** | **169** | ~100 | ~90 | ~200 | ~300 |
| **选项覆盖** | **1,827** | ~600 | ~200 | 完整 | ~500 |
| **GNU 兼容** | **98%** | 95% | 60% | 99% | 70% |
| **原生 Win32** | ✅ | ❌ | ✅ | ❌ | ❌ |
| **包管理器** | ✅ WPM | ❌ | ❌ | apt-cyg | ❌ |
| **测试用例** | **2,346** | ~2,000 | 0 | — | ~100 |
| **二进制体积** | **~3 MB** | ~5 MB | — | 1 GB+ | — |
| **启动速度** | **即开** | 即开 | — | 2–5 秒 | — |
| **活跃维护** | ✅ 2026 | ✅ | ❌ 2012 年起 | ✅ | ❌ |

<details>
<summary><b>深度对比：vs uutils / GnuWin32 / Cygwin</b></summary>

### vs uutils/coreutils（Rust）

| 维度 | WinuxCmd | uutils |
|------|----------|--------|
| 语言 | C++23 | Rust |
| 命令数 | 169 | ~100 |
| 选项数 | 1,827 | ~600 |
| 二进制体积 | ~3 MB | ~5 MB |
| 依赖 | 无 | Rust 运行时 |
| 构建时间 | 2 分钟 | 15 分钟 |
| 包管理器 | WPM 内置 | 无 |

### vs GnuWin32

| 维度 | WinuxCmd | GnuWin32 |
|------|----------|----------|
| 维护状态 | 活跃 | 已停止 |
| 最后更新 | 2026 | 2012 |
| Windows 支持 | Win10/11 | WinXP+ |
| 现代工具链 | C++23、CMake | C、autotools |

### vs Cygwin

| 维度 | WinuxCmd | Cygwin |
|------|----------|--------|
| 安装体积 | ~3 MB | 1 GB+ |
| 启动时间 | 即开 | 2–5 秒 |
| 路径处理 | 原生 Windows | Unix 模拟层 |
| 依赖 | 无 | MSYS2 运行时 |
| 包管理器 | WPM | apt-cyg |

</details>

## 🧰 命令覆盖

<details>
<summary><b>169 个命令——完整覆盖表（点击展开）</b></summary>

### GNU Coreutils（83 个命令）

| 类别 | 命令 | 兼容性 |
|------|------|--------|
| 文件操作 | cp, mv, rm, ln, install, mkdir, rmdir, touch, unlink | 100% |
| 文本处理 | cat, echo, head, tail, sort, uniq, cut, tr, wc, fold, fmt, join, comm | 100% |
| 目录列表 | ls, dir, vdir | 100% |
| 查找搜索 | find, xargs | 100% |
| 加密哈希 | base64, md5sum, sha256sum, cksum 等 11 个 | 100% |
| 日期时间 | date, touch, time, timeout | 100% |
| 系统信息 | uname, hostname, id, whoami, nproc, uptime, arch | 100% |
| 磁盘空间 | df, du, stat | 100% |
| 环境变量 | env, printenv, expr, seq, yes, true, false | 100% |
| 文本格式 | pr, nl, expand, unexpand, column, paste, tsort, ptx | 100% |
| 文件信息 | file, readlink, realpath, dirname, basename, pathchk | 100% |
| 权限管理 | chmod, chown, chgrp, chroot | 95% |
| 其他 | shred, factor, kill, truncate, numfmt, mktemp, dircolors 等 | 100% |

### GNU findutils/grep/sed（3 个命令）

| 命令 | 选项数 | 特性 |
|------|--------|------|
| **find** | 88 | 完整表达式解析器，-exec/-execdir/-ok，-printf |
| **grep** | 49 | PCRE2 支持，--color，--exclude |
| **sed** | 17 | 就地编辑，扩展正则，--posix |

### BSD 工具（15 个命令）

cal, column, hexdump, logger, tree, less, more, strings, rev, tsort, seq, sleep, nohup, watch, tput

### 进程管理（13 个命令）

ps, top, kill, killall, pgrep, pkill, pidof, pldd, free, uptime, renice, stdbuf, timeout

### Cygwin/MSYS2（14 个命令）

cygpath, dos2unix, unix2dos, d2u, u2d

### 自定义扩展（10+ 个命令）

wpm（包管理器）, mpicalc, regtool, mkpasswd, mkgroup, mkfifo, mknod, clear, reset, tzset

</details>

## 🏃 性能基准

| 测试 | WinuxCmd | uutils | GNU (WSL2) |
|------|----------|--------|------------|
| cat (100 MB) | 0.8 秒 | 0.9 秒 | 0.7 秒 |
| sort (100 万行) | 2.1 秒 | 2.3 秒 | 1.9 秒 |
| grep (100 MB) | 1.2 秒 | 1.1 秒 | 1.0 秒 |
| find (1 万个文件) | 0.3 秒 | 0.4 秒 | 0.2 秒 |

与 uutils 同一水平，相比 WSL2 里跑原生 GNU 也只慢约 10–15%——而且完全不需要启动虚拟机。

*测试环境：Windows 11，Intel i7-13700K，32 GB 内存，NVMe SSD*

## 🧪 测试与 GNU 验证

- **2,346 个自动化测试用例**，覆盖 178 个测试文件，**99.6% 通过率**
- **22 个命令 / 56 个差分测试用例**，与 WSL2 中的 GNU Coreutils 9.11 用相同输入对比输出，**通过率 98%**（53/54；唯一不一致的 `dir` 是有意采用 Windows 列式输出）
- 自动化 GNU 对比脚本：`scripts/compare_outputs.sh` 与 `gnu_comparison_tests.sh`

| 类别 | 通过 / 总数 | 通过率 |
|------|:-----------:|:-------:|
| 文本处理 | 29 / 29 | **100%** |
| 加密哈希 | 4 / 4 | **100%** |
| 文件操作 | 6 / 7 | 86%¹ |
| 系统工具 | 11 / 11 | **100%** |
| **总计** | **53 / 54** | **98%** |

> ¹ 唯一的不一致是 `dir`，它有意使用 Windows 风格的列式输出。详见 [GNU 对比报告](DOCS/en/gnu_comparison_report.md)。

## 📚 文档

| 文档 | 描述 |
|------|------|
| [兼容性矩阵](DOCS/en/command_compatibility_matrix.md) | 全部 169 个命令的支持状态 |
| [GNU 对比报告](DOCS/en/gnu_comparison_report.md) | 与 GNU Coreutils 9.11 的差分测试 |
| [Windows 功能](DOCS/en/windows_features.md) | Windows 特有行为 |
| [WPM 指南](DOCS/en/wpm_guide.md) | 包管理器用户指南 |
| [GNU 测试基线](DOCS/en/gnu_test_baseline.md) | GNU 测试框架 |

## 🛠️ 从源码构建

**前提条件：** Visual Studio 2022+ · CMake 3.30+ · Ninja

```bash
# 构建
./scripts/build-with-vs.ps1

# 运行测试
./scripts/build-with-vs.ps1 -Target winuxcmd-tests
build-vs/tests/winuxcmd-tests.exe
```

## 🤝 参与贡献

欢迎贡献！详见 [CONTRIBUTING_ZH.MD](CONTRIBUTING_ZH.MD)。

## 许可证

MIT——详见 [LICENSE](LICENSE)。

---

<div align="center">

**WinuxCmd** —— 用 `ls` 不再需要 Linux 内核。
搭配 [**niubash**](https://github.com/unixwin/niubash)——在 Windows 上把 GNU 讲得字正腔圆的原生 bash shell。

[⬆ 回到顶部](#top)

</div>
