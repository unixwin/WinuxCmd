# nice 命令

## 命令概述

`nice` 命令用于以修改后的调度优先级（niceness）运行程序，或打印当前进程的 niceness 值。Niceness 值范围从 -20（最高优先级）到 19（最低优先级），它影响操作系统调度器对进程的资源分配策略。

**命令格式：**
```
nice [选项]... [命令 [参数]...]
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| `-n` | `--adjustment=N` | 将 niceness 调整 N 值（而非默认的 10） |
| `-N` | | 废弃语法，等价于 `-n N`（不推荐使用） |

## 参数详细说明

### `-n, --adjustment=N`
将命令的 niceness 增加 N。默认增加 10。如果 N 为负数且当前用户没有相应权限，`nice` 会发出警告并按 0 处理。

### `-N`（废弃语法）
为了兼容性，`nice` 也支持旧式语法 `-N`（如 `nice -5 command`）。新脚本应使用 `-n N` 代替。

### 无参数运行
不带参数运行 `nice` 时，会打印当前进程的 niceness 值。

### Niceness 值说明
- **-20**：最高优先级，进程获得更多 CPU 时间
- **0**：默认优先级
- **19**：最低优先级，对其他进程影响最小
- 超出支持范围的值会被截断到最小或最大支持值
- 只有特权用户（root）才能设置负的 niceness 值

## 使用示例

1. **以默认较低优先级运行程序**
   ```bash
   nice factor 4611686018427387903
   ```

2. **打印当前 niceness 值**
   ```bash
   nice
   ```

3. **以指定优先级增量运行程序**
   ```bash
   nice -n 15 mycommand
   ```

4. **以较高优先级运行程序（需要 root 权限）**
   ```bash
   sudo nice -n -5 mycommand
   ```

5. **嵌套使用 nice**
   ```bash
   nice nice -n 3 nice
   # 第一个 nice 将 niceness 设为 10
   # 第二个 nice 再增加 3，变为 13
   ```

6. **设置超出范围的 niceness 值（会被截断）**
   ```bash
   nice -n 10000000000 nice
   # 输出 19（最大支持值）
   ```

7. **与 nohup 配合使用**
   ```bash
   nohup nice -n 15 long_running_command &
   ```

## WinuxCmd 实现状态

**已实现**

当前实现将 niceness 映射到 Windows 进程优先级类。GNU `nice` 还支持无命令时打印当前 niceness，且数值语义与 Windows 调度优先级不完全相同。
