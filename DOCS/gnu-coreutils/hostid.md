# hostid 命令

## 命令概述

`hostid` 命令用于打印当前主机的数字标识符。它输出一个 32 位的十六进制标识符。

**命令格式：**
```
hostid [选项]...
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 输出版本信息并退出 |

## 参数详细说明

`hostid` 命令没有特定的操作参数。它总是输出当前主机的 32 位标识符（十六进制格式）。

### 输出格式

输出为 8 个十六进制字符（32 位），例如：
```
007f0101
```

### 标识符来源

在不同的系统上，hostid 的来源不同：

| 系统 | 标识符来源 |
|------|-----------|
| Linux | 基于 IP 地址计算 |
| macOS | 从 IOPlatformExpertInfo 获取 |
| Windows (WinuxCmd) | 基于 Machine GUID 计算 |

## 使用示例

1. **显示主机标识符**
   ```bash
   hostid
   ```
   输出示例：`007f0101`

2. **将 hostid 用作系统唯一标识**
   ```bash
   SYSTEM_ID=$(hostid)
   echo "System identifier: $SYSTEM_ID"
   ```

3. **在脚本中验证主机身份**
   ```bash
   EXPECTED_ID="007f0101"
   CURRENT_ID=$(hostid)
   if [ "$CURRENT_ID" = "$EXPECTED_ID" ]; then
       echo "Running on expected host"
   else
       echo "Unexpected host!"
       exit 1
   fi
   ```

4. **显示帮助信息**
   ```bash
   hostid --help
   ```

5. **显示版本信息**
   ```bash
   hostid --version
   ```

6. **将 hostid 写入文件**
   ```bash
   hostid > /tmp/hostid.txt
   ```

7. **使用 xxd 解析 hostid**
   ```bash
   hostid | xxd -r -p
   ```

## 技术说明

### GNU/Linux 实现
在 GNU/Linux 上，`hostid` 通常返回基于系统 IP 地址计算的值。可以通过直接写入 `/etc/hostid` 文件来设置。

### WinuxCmd 实现
在 WinuxCmd 中，`hostid` 基于 Windows 注册表中的 Machine GUID 计算：
- 注册表位置：`HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Cryptography\MachineGuid`
- 使用哈希算法将 GUID 转换为 32 位标识符
- 输出为 8 位十六进制字符串

## WinuxCmd 实现状态

**已实现**

实现说明：
- 从 Windows 注册表读取 Machine GUID
- 使用 `std::hash` 计算 GUID 的哈希值
- 取哈希值的低 32 位作为主机标识符
- 输出为 8 位小写十六进制字符串

注意：WinuxCmd 的实现与 GNU/Linux 的实现不同，输出值不能互换使用。
