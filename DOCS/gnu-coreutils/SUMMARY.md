# GNU Coreutils 文档汇总

本文档汇总了 WinuxCmd 已实现的所有 GNU Coreutils 命令的参数文档。

## 文档统计

- **总文档数**: 102 个命令文档 + 1 个 README
- **文档格式**: Markdown
- **参考版本**: GNU Coreutils 9.11

## 命令分类统计

| 类别 | 命令数 | 已实现 | 待实现 |
|------|--------|--------|--------|
| 文件输出 | 7 | 7 | 0 |
| 格式化 | 3 | 3 | 0 |
| 部分输出 | 4 | 4 | 0 |
| 文件汇总 | 10 | 10 | 0 |
| 排序操作 | 6 | 6 | 0 |
| 字段操作 | 3 | 3 | 0 |
| 字符操作 | 3 | 3 | 0 |
| 目录列表 | 4 | 4 | 0 |
| 基本操作 | 7 | 7 | 0 |
| 特殊文件 | 6 | 6 | 0 |
| 属性修改 | 4 | 4 | 0 |
| 空间使用 | 5 | 5 | 0 |
| 文本打印 | 3 | 3 | 0 |
| 条件判断 | 4 | 4 | 0 |
| 重定向 | 1 | 1 | 0 |
| 文件名操作 | 5 | 5 | 0 |
| 工作上下文 | 4 | 4 | 0 |
| 用户信息 | 7 | 7 | 0 |
| 系统上下文 | 7 | 7 | 0 |
| 修改命令 | 5 | 5 | 0 |
| 进程控制 | 1 | 1 | 0 |
| 延迟操作 | 1 | 1 | 0 |
| 数值操作 | 3 | 3 | 0 |
| **总计** | **102** | **102** | **0** |

## 完整命令列表

### 文件输出 (Output of entire files)
| 命令 | 参数数 | 状态 | 文档 |
|------|--------|------|------|
| cat | 10 | 已实现 | [cat.md](cat.md) |
| tac | 3 | 已实现 | [tac.md](tac.md) |
| nl | 11 | 已实现 | [nl.md](nl.md) |
| od | 6 | 已实现 | [od.md](od.md) |
| base32 | 3 | 已实现 | [base32.md](base32.md) |
| base64 | 3 | 已实现 | [base64.md](base64.md) |
| basenc | 10 | 已实现 | [basenc.md](basenc.md) |

### 格式化 (Formatting file contents)
| 命令 | 参数数 | 状态 | 文档 |
|------|--------|------|------|
| fmt | 7 | 已实现 | [fmt.md](fmt.md) |
| pr | 18 | 已实现 | [pr.md](pr.md) |
| fold | 4 | 已实现 | [fold.md](fold.md) |

### 部分输出 (Output of parts of files)
| 命令 | 参数数 | 状态 | 文档 |
|------|--------|------|------|
| head | 5 | 已实现 | [head.md](head.md) |
| tail | 12 | 已实现 | [tail.md](tail.md) |
| split | 10 | 已实现 | [split.md](split.md) |
| csplit | 7 | 已实现 | [csplit.md](csplit.md) |

### 文件汇总 (Summarizing files)
| 命令 | 参数数 | 状态 | 文档 |
|------|--------|------|------|
| wc | 7 | 已实现 | [wc.md](wc.md) |
| sum | 2 | 已实现 | [sum.md](sum.md) |
| cksum | 6 | 已实现 | [cksum.md](cksum.md) |
| md5sum | 4 | 已实现 | [md5sum.md](md5sum.md) |
| b2sum | 4 | 已实现 | [b2sum.md](b2sum.md) |
| sha1sum | 4 | 已实现 | [sha1sum.md](sha1sum.md) |
| sha224sum | 4 | 已实现 | [sha224sum.md](sha224sum.md) |
| sha256sum | 4 | 已实现 | [sha256sum.md](sha256sum.md) |
| sha384sum | 4 | 已实现 | [sha384sum.md](sha384sum.md) |
| sha512sum | 4 | 已实现 | [sha512sum.md](sha512sum.md) |

### 排序操作 (Operating on sorted files)
| 命令 | 参数数 | 状态 | 文档 |
|------|--------|------|------|
| sort | 20 | 已实现 | [sort.md](sort.md) |
| shuf | 7 | 已实现 | [shuf.md](shuf.md) |
| uniq | 10 | 已实现 | [uniq.md](uniq.md) |
| comm | 6 | 已实现 | [comm.md](comm.md) |
| ptx | 8 | 已实现 | [ptx.md](ptx.md) |
| tsort | 0 | 已实现 | [tsort.md](tsort.md) |

### 字段操作 (Operating on fields)
| 命令 | 参数数 | 状态 | 文档 |
|------|--------|------|------|
| cut | 8 | 已实现 | [cut.md](cut.md) |
| paste | 3 | 已实现 | [paste.md](paste.md) |
| join | 12 | 已实现 | [join.md](join.md) |

### 字符操作 (Operating on characters)
| 命令 | 参数数 | 状态 | 文档 |
|------|--------|------|------|
| tr | 4 | 已实现 | [tr.md](tr.md) |
| expand | 2 | 已实现 | [expand.md](expand.md) |
| unexpand | 3 | 已实现 | [unexpand.md](unexpand.md) |

### 目录列表 (Directory listing)
| 命令 | 参数数 | 状态 | 文档 |
|------|--------|------|------|
| ls | 30+ | 已实现 | [ls.md](ls.md) |
| dir | 30+ | 已实现 | [dir.md](dir.md) |
| vdir | 30+ | 已实现 | [vdir.md](vdir.md) |
| dircolors | 4 | 已实现 | [dircolors.md](dircolors.md) |

### 基本操作 (Basic operations)
| 命令 | 参数数 | 状态 | 文档 |
|------|--------|------|------|
| cp | 15 | 已实现 | [cp.md](cp.md) |
| dd | 8 | 已实现 | [dd.md](dd.md) |
| install | 12 | 已实现 | [install.md](install.md) |
| mv | 10 | 已实现 | [mv.md](mv.md) |
| rm | 7 | 已实现 | [rm.md](rm.md) |
| shred | 5 | 已实现 | [shred.md](shred.md) |

### 特殊文件类型 (Special file types)
| 命令 | 参数数 | 状态 | 文档 |
|------|--------|------|------|
| link | 0 | 已实现 | [link.md](link.md) |
| ln | 12 | 已实现 | [ln.md](ln.md) |
| mkdir | 3 | 已实现 | [mkdir.md](mkdir.md) |
| readlink | 7 | 已实现 | [readlink.md](readlink.md) |
| rmdir | 3 | 已实现 | [rmdir.md](rmdir.md) |
| unlink | 0 | 已实现 | [unlink.md](unlink.md) |

### 更改文件属性 (Changing file attributes)
| 命令 | 参数数 | 状态 | 文档 |
|------|--------|------|------|
| chgrp | 12 | 已实现 | [chgrp.md](chgrp.md) |
| chown | 6 | 已实现 | [chown.md](chown.md) |
| chmod | 8 | 已实现 | [chmod.md](chmod.md) |
| touch | 8 | 已实现 | [touch.md](touch.md) |

### 文件空间使用 (File space usage)
| 命令 | 参数数 | 状态 | 文档 |
|------|--------|------|------|
| df | 12 | 已实现 | [df.md](df.md) |
| du | 10 | 已实现 | [du.md](du.md) |
| stat | 6 | 已实现 | [stat.md](stat.md) |
| sync | 0 | 已实现 | [sync.md](sync.md) |
| truncate | 4 | 已实现 | [truncate.md](truncate.md) |

### 打印文本 (Printing text)
| 命令 | 参数数 | 状态 | 文档 |
|------|--------|------|------|
| echo | 4 | 已实现 | [echo.md](echo.md) |
| printf | 0 | 已实现 | [printf.md](printf.md) |
| yes | 0 | 已实现 | [yes.md](yes.md) |

### 条件 (Conditions)
| 命令 | 参数数 | 状态 | 文档 |
|------|--------|------|------|
| false | 0 | 已实现 | [false.md](false.md) |
| true | 0 | 已实现 | [true.md](true.md) |
| test | 0 | 已实现 | [test.md](test.md) |
| expr | 0 | 已实现 | [expr.md](expr.md) |

### 重定向 (Redirection)
| 命令 | 参数数 | 状态 | 文档 |
|------|--------|------|------|
| tee | 3 | 已实现 | [tee.md](tee.md) |

### 文件名操作 (File name manipulation)
| 命令 | 参数数 | 状态 | 文档 |
|------|--------|------|------|
| basename | 3 | 已实现 | [basename.md](basename.md) |
| dirname | 1 | 已实现 | [dirname.md](dirname.md) |
| pathchk | 3 | 已实现 | [pathchk.md](pathchk.md) |
| mktemp | 4 | 已实现 | [mktemp.md](mktemp.md) |
| realpath | 8 | 已实现 | [realpath.md](realpath.md) |

### 工作上下文 (Working context)
| 命令 | 参数数 | 状态 | 文档 |
|------|--------|------|------|
| pwd | 2 | 已实现 | [pwd.md](pwd.md) |
| printenv | 1 | 已实现 | [printenv.md](printenv.md) |
| tty | 1 | 已实现 | [tty.md](tty.md) |
| stty | 13 | 已实现 | [stty.md](stty.md) |

### 用户信息 (User information)
| 命令 | 参数数 | 状态 | 文档 |
|------|--------|------|------|
| id | 4 | 已实现 | [id.md](id.md) |
| logname | 0 | 已实现 | [logname.md](logname.md) |
| whoami | 0 | 已实现 | [whoami.md](whoami.md) |
| groups | 0 | 已实现 | [groups.md](groups.md) |
| users | 0 | 已实现 | [users.md](users.md) |
| who | 4 | 已实现 | [who.md](who.md) |
| pinky | 6 | 已实现 | [pinky.md](pinky.md) |

### 系统上下文 (System context)
| 命令 | 参数数 | 状态 | 文档 |
|------|--------|------|------|
| date | 10 | 已实现 | [date.md](date.md) |
| arch | 0 | 已实现 | [arch.md](arch.md) |
| nproc | 2 | 已实现 | [nproc.md](nproc.md) |
| uname | 9 | 已实现 | [uname.md](uname.md) |
| hostname | 4 | 已实现 | [hostname.md](hostname.md) |
| hostid | 0 | 已实现 | [hostid.md](hostid.md) |
| uptime | 2 | 已实现 | [uptime.md](uptime.md) |

### 修改命令调用 (Modified command invocation)
| 命令 | 参数数 | 状态 | 文档 |
|------|--------|------|------|
| env | 6 | 已实现 | [env.md](env.md) |
| nice | 1 | 已实现 | [nice.md](nice.md) |
| nohup | 0 | 已实现 | [nohup.md](nohup.md) |
| stdbuf | 3 | 已实现 | [stdbuf.md](stdbuf.md) |
| timeout | 5 | 已实现 | [timeout.md](timeout.md) |

### 进程控制 (Process control)
| 命令 | 参数数 | 状态 | 文档 |
|------|--------|------|------|
| kill | 3 | 已实现 | [kill.md](kill.md) |

### 延迟 (Delaying)
| 命令 | 参数数 | 状态 | 文档 |
|------|--------|------|------|
| sleep | 0 | 已实现 | [sleep.md](sleep.md) |

### 数值操作 (Numeric operations)
| 命令 | 参数数 | 状态 | 文档 |
|------|--------|------|------|
| factor | 0 | 已实现 | [factor.md](factor.md) |
| numfmt | 8 | 已实现 | [numfmt.md](numfmt.md) |
| seq | 3 | 已实现 | [seq.md](seq.md) |

## 通用选项统计

所有命令都支持以下通用选项：
- `--help` - 显示帮助信息
- `--version` - 输出版本信息

## 完整兼容性矩阵

详细的逐选项兼容性对照请参阅 [COMPATIBILITY_MATRIX.md](COMPATIBILITY_MATRIX.md)。

兼容性统计：
- **完全对齐**: 68 个命令
- **主要差距**: 34 个命令（主要是平台差异和部分未实现选项）
- 详见 [COMPATIBILITY_MATRIX.md](COMPATIBILITY_MATRIX.md) 的统计表

## 下一步工作

1. **参数实现**: 根据文档逐一实现缺失的参数
2. **测试验证**: 为每个参数编写测试用例
3. **兼容性验证**: 与 GNU Coreutils 行为对比验证
4. **文档更新**: 根据实现进度更新文档状态

## 参考链接

- [GNU Coreutils 官方文档](https://www.gnu.org/software/coreutils/manual/)
- [WinuxCmd 项目主页](https://github.com/caomengxuan666/WinuxCmd)
- [完整兼容性矩阵](COMPATIBILITY_MATRIX.md)
- [命令兼容性矩阵](../en/commands_implementation_en.md)
- [GNU Coreutils 兼容性审计](../en/gnu_coreutils_parity.md)
