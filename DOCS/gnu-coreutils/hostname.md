# hostname 命令

## 命令概述

`hostname` 命令用于打印或设置系统的主机名。当不带参数调用时，打印当前主机名。

**命令格式：**
```
hostname [选项]...
hostname [新主机名]
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| `-a` | `--alias` | 显示主机别名 |
| `-A` | `--all-fqdns` | 显示所有完全限定域名 |
| `-d` | `--domain` | 显示 DNS 域名 |
| `-f` | `--fqdn` | 显示完全限定域名 (FQDN) |
| `-i` | `--ip-address` | 显示主机的 IP 地址 |
| `-I` | `--all-ip-addresses` | 显示主机的所有 IP 地址 |
| `-s` | `--short` | 显示短主机名（截断到第一个点） |
| `-y` | `--yp` | 显示 NIS/YP 域名 |
| `-n` | `--node` | 显示网络节点主机名 |
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 输出版本信息并退出 |

## 参数详细说明

### `-a, --alias`
显示主机的别名。别名通常在 `/etc/hosts` 文件中定义。

### `-A, --all-fqdns`
显示主机的所有完全限定域名 (FQDN)。这会查询 DNS 获取所有关联的域名。

### `-d, --domain`
显示主机的 DNS 域名部分。等同于 `hostname -f | cut -d. -f2-`。

### `-f, --fqdn`
显示完全限定域名 (FQDN)。FQDN 包含主机名和完整的域名，例如 `server1.example.com`。

### `-i, --ip-address`
显示主机的 IP 地址。如果有多个网络接口，只显示第一个 IP 地址。

### `-I, --all-ip-addresses`
显示主机的所有 IP 地址，每个地址一行。包括所有网络接口的地址。

### `-s, --short`
显示短主机名，截断到第一个点之前的部分。例如，如果 FQDN 是 `server1.example.com`，短主机名是 `server1`。

### `-y, --yp`
显示 NIS (YP) 域名。

### `-n, --node`
显示网络节点主机名（UTS 名称）。

## 使用示例

1. **显示当前主机名**
   ```bash
   hostname
   ```
   输出示例：`DESKTOP-ABC123`

2. **显示完全限定域名**
   ```bash
   hostname -f
   ```

3. **显示短主机名**
   ```bash
   hostname -s
   ```

4. **显示 DNS 域名**
   ```bash
   hostname -d
   ```

5. **显示主机的 IP 地址**
   ```bash
   hostname -i
   ```
   输出示例：`192.168.1.100`

6. **显示所有 IP 地址**
   ```bash
   hostname -I
   ```
   输出示例：`192.168.1.100 10.0.0.1`

7. **在脚本中使用主机名**
   ```bash
   CURRENT_HOST=$(hostname)
   echo "Running on $CURRENT_HOST"
   ```

8. **设置新主机名（需要管理员权限）**
   ```bash
   sudo hostname new-hostname
   ```
   注意：WinuxCmd 不支持设置主机名，仅支持查看。

9. **显示帮助信息**
   ```bash
   hostname --help
   ```

10. **显示版本信息**
    ```bash
    hostname --version
    ```

## 与 DNS 相关的命令

- `hostname -f` - 完全限定域名
- `hostname -d` - DNS 域名
- `hostname -i` - IP 地址

这些命令通常需要正确的 DNS 配置才能正常工作。

## WinuxCmd 实现状态

**已实现**（部分选项）

支持的选项：
- `-i, --ip-address` - 显示 IP 地址
- `-I, --all-ip-addresses` - 显示所有 IP 地址
- `-s, --short` - 显示短主机名
- `-f, --fqdn` - 显示完全限定域名

暂未支持的选项：
- `-a, --alias` - 主机别名
- `-A, --all-fqdns` - 所有 FQDN
- `-d, --domain` - DNS 域名
- `-y, --yp` - NIS/YP 域名
- `-n, --node` - 网络节点主机名

实现说明：
- 主机名通过 Windows Sockets API (`gethostname`) 获取
- IP 地址通过 `gethostbyname` 和 `inet_ntoa` 获取
- 短主机名通过截断第一个点之前的部分实现
- 不支持设置主机名功能
