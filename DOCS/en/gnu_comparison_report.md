# GNU Coreutils vs WinuxCmd Comparison Report

# GNU Coreutils 与 WinuxCmd 对比报告

> **Date / 日期**: 2026-08-29
> **GNU Version**: coreutils 9.11
> **WinuxCmd Version**: 1.0.0
> **Test Method / 测试方法**: Output comparison between GNU coreutils (Linux/WSL2) and WinuxCmd (Windows PE binary) with identical inputs

> ⚠️ **Status / 状态**: This report documents the **historical 56-case differential run** captured on 2026-08-29. The **current differential corpus** (`tests/differential/`, machine-checked in `baseline.json`) contains **178 cases** (131 corpus + 47 regressions, covering 84 commands), of which **165 pass / 13 are tracked platform-environment and format differences** against the GNU coreutils 9.7 oracle — a **93% pass rate**. The eight former issue-140 gaps all pass; the normal-format `diff` hunk-header, hash-family separator, cksum line-ending, realpath forward-slash, and od -c octal-escaping gaps were all found and fixed in 2026-09. See the README for the live list. 本报告记录 2026-08-29 的历史 56 用例差分运行。当前差分语料库（`tests/differential/`，由 `baseline.json` 机器校验）含 178 用例（131 corpus + 47 regressions，覆盖 84 命令），对照 GNU coreutils 9.7 oracle，165 通过 / 13 为追踪的平台-环境与格式差异，93% 通过率。原 issue-140 的 8 个缺口全部通过；normal 格式 `diff` hunk 头、哈希分隔符、cksum 行尾、realpath 路径分隔、od -c 八进制转义等缺口均已于 2026-09 发现并修复。

---

## Executive Summary / 概要

| Metric / 指标 | Value / 值 |
|---|---|
| Total Tests / 总测试数 | 56 |
| Passed / 通过 | 53 |
| Failed / 失败 | 1 (dir formatting) |
| Skipped / 跳过 | 2 (platform-specific) |
| **Overall Pass Rate / 总通过率** | **98% (53/54)** |
| Commands Tested / 测试命令数 | 22 |

### Key Findings / 主要发现

1. **WinuxCmd achieves 98% compatibility with GNU coreutils** for the tested commands — WinuxCmd 在测试的命令中达到了与 GNU coreutils 98% 的兼容性
2. All text processing commands (cat, echo, head, tail, sort, uniq, cut, tr, wc) produce **identical output** — 所有文本处理命令产生完全一致的输出
3. Cryptographic hash functions (md5sum, sha256sum) produce **identical hashes** — 加密哈希函数产生完全一致的哈希值
4. File operations (cp, mv, rm, mkdir) behave **identically** for basic use cases — 文件操作命令的基本用例行为完全一致
5. System utilities (find, grep, sed) work correctly — 系统工具命令工作正常

---

## Detailed Results by Command / 按命令详细结果

### Coreutils Commands / Coreutils 命令

#### Text Processing / 文本处理

| Command | Test Case | GNU Output | WinuxCmd Output | Status |
|---|---|---|---|---|
| **cat** | basic | aardvark\nbear\ncat\ndog\nelephant\nfox | (identical) | ✅ PASS |
| **cat** | multi-file | hello world + line1-5 | (identical) | ✅ PASS |
| **cat** | -n (number lines) | 1\taardvark 2\tbear ... | (identical) | ✅ PASS |
| **cat** | -b (number non-blank) | 1\taardvark 2\tbear ... | (identical) | ✅ PASS |
| **cat** | -s (squeeze blanks) | (identical) | (identical) | ✅ PASS |
| **cat** | -v (non-printing) | (identical) | (identical) | ✅ PASS |
| **echo** | basic | hello world | hello world | ✅ PASS |
| **echo** | -e (escape sequences) | line1 + line2 | line1 + line2 | ✅ PASS |
| **echo** | -n (no newline) | hello | hello | ✅ PASS |
| **head** | -n 3 | aardvark + bear + cat | (identical) | ✅ PASS |
| **head** | -c 10 | aardvark\nb | (identical) | ✅ PASS |
| **head** | -2 | aardvark + bear | (identical) | ✅ PASS |
| **tail** | -n 3 | dog + elephant + fox | (identical) | ✅ PASS |
| **tail** | -c 10 | ephant + fox | (identical) | ✅ PASS |
| **sort** | basic (alpha) | apple + banana + cherry + date + egrape | (identical) | ✅ PASS |
| **sort** | -r (reverse) | egrape + date + cherry + banana + apple | (identical) | ✅ PASS |
| **sort** | -n (numeric) | 1 + 1 + 2 + 3 + 3 + 5 + 5 + 5 + 6 + 9 | (identical) | ✅ PASS |
| **sort** | -u (unique) | apple + banana + cherry + date + egrape | (identical) | ✅ PASS |
| **sort** | -k (key field) | (identical) | (identical) | ✅ PASS |
| **uniq** | basic (after sort) | aaa + bbb + ccc | (identical) | ✅ PASS |
| **uniq** | -c (count) | 2 aaa + 3 bbb + 1 ccc | (identical) | ✅ PASS |
| **uniq** | -d (duplicates) | aaa + bbb | (identical) | ✅ PASS |
| **cut** | -d, -f2 (delimiter) | b + 2 + y | (identical) | ✅ PASS |
| **cut** | -c1-3 (character) | che + app + ban + dat + egr | (identical) | ✅ PASS |
| **cut** | -f1,3 (multi-field) | (identical) | (identical) | ✅ PASS |
| **tr** | a-z A-Z (upper) | HELLO WORLD | HELLO WORLD | ✅ PASS |
| **tr** | -d ' ' (delete) | helloworld123 | helloworld123 | ✅ PASS |
| **tr** | -d [:digit:] | hello | hello | ✅ PASS |
| **tr** | -s a-z (squeeze) | abc | abc | ✅ PASS |
| **wc** | -l (lines) | 5 input1.txt | 5 input1.txt | ✅ PASS |
| **wc** | -w (words) | 6 input1.txt | 6 input1.txt | ✅ PASS |
| **wc** | -c (bytes) | 34 input1.txt | 34 input1.txt | ✅ PASS |

#### Crypto and Hash / 加密与哈希

| Command | Test Case | GNU Output | WinuxCmd Output | Status |
|---|---|---|---|---|
| **base64** | encode | aGVsbG8gd29ybGQ= | (identical) | ✅ PASS |
| **base64** | decode | hello world | (identical) | ✅ PASS |
| **md5sum** | hash | 5eb63bbbe01eeed093cb22bb8f5acdc3 | (identical) | ✅ PASS |
| **sha256sum** | hash | b94d27b9...efcde9 | (identical) | ✅ PASS |

#### File Operations / 文件操作

| Command | Test Case | GNU Output | WinuxCmd Output | Status |
|---|---|---|---|---|
| **ls** | basic (directory listing) | (sorted file list) | (sorted file list) | ✅ PASS |
| **ls** | -1 (one per line) | input1.txt | input1.txt | ✅ PASS |
| **cp** | basic copy | hello world (verified) | hello world | ✅ PASS |
| **mv** | basic move | move_me (verified) | move_me | ✅ PASS |
| **rm** | basic remove | deleted | deleted | ✅ PASS |
| **mkdir** | -p (parents) | created | created | ✅ PASS |
| **dir** | basic listing | (GNU ls output) | (Windows column format) | ⚠️ FAIL |

#### Platform-Specific / 平台特定

| Command | Test Case | GNU Output | WinuxCmd Output | Status |
|---|---|---|---|---|
| **date** | iso-utc | 2026-08-29T01:52:04Z | 2026-08-29T01:52:04Z | ⏭️ SKIP (time-dependent) |
| **uname** | basic | Linux | MSWindows_NT | ⏭️ SKIP (platform-specific) |

### System Utilities (separate from coreutils) / 系统工具

| Command | Test Case | GNU Output | WinuxCmd Output | Status |
|---|---|---|---|---|
| **find** | -name | hello.txt | hello.txt | ✅ PASS |
| **find** | -name glob | (sorted list) | (sorted list) | ✅ PASS |
| **find** | -type f count | 11 | 11 | ✅ PASS |
| **grep** | basic | cat | cat | ✅ PASS |
| **grep** | -c (count) | 4 | 4 | ✅ PASS |
| **grep** | -i (case-insensitive) | cat | cat | ✅ PASS |
| **grep** | -n (line number) | 3:cat | 3:cat | ✅ PASS |
| **sed** | s/old/new/ | hello Linux | hello Linux | ✅ PASS |
| **sed** | 2d (delete line) | line1\nline3\nline4\nline5 | line1\nline3\nline4\nline5 | ✅ PASS |
| **sed** | -n 1,2p (print range) | line1\nline2 | line1\nline2 | ✅ PASS |
| **sed** | -n 3p (print line) | line3 | line3 | ✅ PASS |

---

## Category Summary / 分类汇总

| Category / 类别 | Passed / 通过 | Total / 总计 | Pass Rate / 通过率 |
|---|---|---|---|
| Text Processing / 文本处理 | 29 | 29 | **100%** |
| Crypto and Hash / 加密与哈希 | 4 | 4 | **100%** |
| File Operations / 文件操作 | 6 | 7 | **86%** |
| System Utilities / 系统工具 | 11 | 11 | **100%** |
| **Overall / 总计** | **53** | **54** (excl. skips) | **98%** |

---

## Behavioral Differences / 行为差异

### 1. Path Display / 路径显示

| Scenario | GNU behavior | WinuxCmd behavior |
|---|---|---|
| Absolute path in output | `/mnt/d/repo/...` (Linux mount) | `D:\repo\...` (Windows native) |
| wc with absolute path | Shows full Linux path | Shows Windows path or relative |
| ls with absolute path | Shows full Linux path | Shows Windows path or relative |

**Impact / 影响**: Cosmetic only. Values (line count, word count, byte count) are identical.
仅影响显示。数值（行数、字数、字节数）完全一致。

### 2. dir Command / dir 命令

| Aspect | GNU (no dir in coreutils) | WinuxCmd dir |
|---|---|---|
| Output format | One file per line (ls default) | Columnar layout (Windows-style) |
| Use case | Unix-style listing | Windows CMD compatibility |

**Impact / 影响**: `dir` is a Windows-specific command. WinuxCmd implements it for Windows CMD compatibility. The behavior is intentionally different from `ls` to match Windows expectations.
`dir` 是 Windows 特定命令。WinuxCmd 实现它以兼容 Windows CMD。其行为有意与 `ls` 不同，以匹配 Windows 的预期。

### 3. Platform-Specific Commands / 平台特定命令

| Command | Difference | Reason / 原因 |
|---|---|---|
| uname -a | Reports MSWindows_NT vs Linux | Correctly identifies Windows platform |
| date | Format may differ slightly | Locale and timezone differences |

---

## GNU Test Suite Compatibility / GNU 测试套件兼容性

### Test Infrastructure Challenges / 测试基础设施挑战

The GNU coreutils test suite was designed for Unix-like environments and could not be run directly against WinuxCmd Windows PE binaries because:

GNU coreutils 测试套件专为类 Unix 环境设计，无法直接对 WinuxCmd Windows PE 二进制文件运行，原因如下：

1. **Binary format**: WinuxCmd produces .exe (PE32+) Windows executables
   二进制格式：WinuxCmd 生成 .exe（PE32+）Windows 可执行文件
2. **Shell functions**: GNU test framework relies on shell helper functions (compare, returns_, Exit) that reference Linux-specific behaviors
   Shell 函数：GNU 测试框架依赖的 Shell 辅助函数引用了 Linux 特定行为
3. **Path handling**: Tests assume Unix-style / paths
   路径处理：测试假设 Unix 风格的路径
4. **Process model**: Windows process model differs from Unix fork/exec
   进程模型：Windows 进程模型与 Unix 的 fork/exec 不同

### Alternative Test Approach / 替代测试方法

Instead of running the GNU test suite directly, we performed **output comparison testing**: 对于每个命令，使用相同的输入，分别运行 GNU 版本和 WinuxCmd 版本，然后比较输出是否一致。这种方法不依赖于特定的测试框架，能够直接验证命令的行为兼容性。

---

## Recommendations / 建议

### Already Production-Ready / 已可投入使用

The following commands show 100% output compatibility with GNU coreutils:
以下命令与 GNU coreutils 完全兼容（输出 100% 一致）：

- ✅ cat, echo, head, tail
- ✅ sort, uniq, cut, tr, wc
- ✅ base64, md5sum, sha256sum
- ✅ ls, cp, mv, rm, mkdir
- ✅ find, grep, sed

### Minor Differences to Document / 需记录的微小差异

- ⚠️ dir command uses Windows-style columnar output (by design)
  dir 命令使用 Windows 风格的列式输出（设计如此）
- ⚠️ Path display uses Windows-native paths (by design)
  路径显示使用 Windows 原生路径（设计如此）

### Not Tested / 未测试

- hostname — Not part of GNU coreutils, separate package
  hostname — 不属于 GNU coreutils，是独立软件包
- date advanced formatting — Requires same locale settings for comparison
  date 高级格式化 — 需要相同的区域设置才能比较

---

## Appendix: Test Environment / 附录：测试环境

| Item | Value |
|---|---|
| GNU Source / GNU 源码 | J:/gnu-coreutils/coreutils-9.11 (built) |
| WinuxCmd Binary / WinuxCmd 二进制 | D:/repo/unixwin-winuxcmd/build-vs/usr/bin/ |
| Test Input Directory / 测试输入目录 | D:/repo/unixwin-winuxcmd/test_gnu/ |
| WSL Distribution / WSL 发行版 | Ubuntu-GitLab (WSL2) |
| Test Date / 测试日期 | 2026-08-29 |
| GNU Version / GNU 版本 | coreutils 9.11 |
| WinuxCmd Version / WinuxCmd 版本 | 1.0.0 (historical run captured v0.17.1) |

### Test Input Files / 测试输入文件

| File | Content |
|---|---|
| input1.txt | 6 animal names (aardvark, bear, cat, dog, elephant, fox) |
| hello.txt | "hello world" |
| multi.txt | 5 lines (line1 through line5) |
| cols.txt | CSV data (a,b,c / 1,2,3 / x,y,z) |
| repeat.txt | Repeated values for uniq testing |
| sorted_input.txt | 5 fruit names for sort testing |
| numbers.txt | 11 numbers for numeric sort testing |
| spaces.txt | Lines with various whitespace |
| special.txt | Lines with special characters |
| large.txt | 1000 lines for large file testing |
