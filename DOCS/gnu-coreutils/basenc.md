# basenc 命令

## 命令概述

`basenc` 命令用于将数据转换为各种常见的编码形式或从这些编码形式转换回来。编码形式使用可打印的 ASCII 字符来表示二进制数据。

**命令格式：**
```
basenc 编码 [选项]... [文件]
basenc 编码 --decode [选项]... [文件]
```

编码参数是必需的。如果省略文件，`basenc` 从标准输入读取。`-w/--wrap`、`-i/--ignore-garbage`、`-d/--decode` 选项与 `base64` 命令完全相同。

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| | `--base64` | 使用 base64 编码 |
| | `--base64url` | 使用 URL 安全的 base64 编码 |
| | `--base58` | 使用 base58 编码 |
| | `--base32` | 使用 base32 编码 |
| | `--base32hex` | 使用扩展十六进制字母表的 base32 编码 |
| | `--base16` | 使用 base16（十六进制）编码 |
| | `--base2lsbf` | 使用二进制字符串形式（最低有效位优先） |
| | `--base2msbf` | 使用二进制字符串形式（最高有效位优先） |
| | `--z85` | 使用 Z85 编码 |
| `-w` | `--wrap=列数` | 编码时在指定列数后换行 |
| `-d` | `--decode` | 从编码数据解码 |
| `-i` | `--ignore-garbage` | 解码时忽略无法识别的字节 |

## 参数详细说明

### 编码选项

#### `--base64`
编码为（或使用 `-d/--decode` 从）base64 形式。该格式符合 RFC 4648#4。等价于 `base64` 命令。

#### `--base64url`
编码为（或使用 `-d/--decode` 从）文件和 URL 安全的 base64 形式（使用 `_` 和 `-` 代替 `+` 和 `/`）。该格式符合 RFC 4648#5。

#### `--base58`
编码为（或使用 `-d/--decode` 从）base58 形式。该格式符合 Base58 草案。此编码适用于转录，因为输出避免了视觉上相似的字符。它最适合较小的数据量。例如，以下命令生成 22 字节的唯一 128 位 ID：
```bash
uuidgen | basenc --base16 -di | basenc --base58
```

#### `--base32`
编码为（或使用 `-d/--decode` 从）base32 形式。编码数据使用 `ABCDEFGHIJKLMNOPQRSTUVWXYZ234567=` 字符。该格式符合 RFC 4648#6。等价于 `base32` 命令。

#### `--base32hex`
编码为（或使用 `-d/--decode` 从）扩展十六进制字母表的 base32 形式。编码数据使用 `0123456789ABCDEFGHIJKLMNOPQRSTUV=` 字符。该格式符合 RFC 4648#7。

#### `--base16`
编码为（或使用 `-d/--decode` 从）base16（十六进制）形式。编码数据使用 `0123456789ABCDEF` 字符。该格式符合 RFC 4648#8。

#### `--base2lsbf`
编码为（或使用 `-d/--decode` 从）二进制字符串形式（`0` 和 `1`），每个字节的最低有效位优先。

#### `--base2msbf`
编码为（或使用 `-d/--decode` 从）二进制字符串形式（`0` 和 `1`），每个字节的最高有效位优先。

#### `--z85`
编码为（或使用 `-d/--decode` 从）Z85 形式（修改的 Ascii85 形式）。编码数据使用 `0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?&<>()[]{}@%$#` 字符。该格式符合 ZeroMQ 规范:32/Z85。

使用 `--z85` 编码时，输入长度必须是 4 的倍数；使用 `--z85` 解码时，输入长度必须是 5 的倍数。

### 通用选项

#### `-w, --wrap=列数`
在编码时，在指定的列数后换行。这必须是一个正数。默认是在 76 个字符后换行。使用值 0 可以完全禁用换行。

#### `-d, --decode`
改变操作模式，从默认的编码数据变为解码数据。输入应为相应编码的数据，输出将是原始数据。

#### `-i, --ignore-garbage`
解码时，总是接受换行符。在解码过程中，忽略无法识别的字节，以允许解码扭曲的数据。

## 使用示例

1. **使用 base64 编码**
   ```bash
   basenc --base64 file.txt
   ```

2. **使用 base32 编码**
   ```bash
   basenc --base32 file.txt
   ```

3. **使用 base16（十六进制）编码**
   ```bash
   basenc --base16 file.txt
   ```

4. **使用 base64url 编码**
   ```bash
   basenc --base64url file.txt
   ```

5. **使用 base58 编码**
   ```bash
   basenc --base58 file.txt
   ```

6. **使用 Z85 编码**
   ```bash
   basenc --z85 file.txt
   ```

7. **解码 base64 数据**
   ```bash
   basenc --base64 -d encoded.txt
   ```

8. **使用二进制字符串形式（最高有效位优先）**
   ```bash
   basenc --base2msbf file.txt
   ```

9. **编码示例**
   ```bash
   printf '\376\117\202' | basenc --base64
   # 输出: /k+C
   
   printf '\376\117\202' | basenc --base64url
   # 输出: _k-C
   
   printf '\376\117\202' | basenc --base32
   # 输出: 7ZHYE===
   
   printf '\376\117\202' | basenc --base32hex
   # 输出: VP7O4===
   
   printf '\376\117\202' | basenc --base16
   # 输出: FE4F82
   
   printf '\376\117\202' | basenc --base2lsbf
   # 输出: 011111111111001001000001
   
   printf '\376\117\202' | basenc --base2msbf
   # 输出: 111111100100111110000010
   
   printf '\376\117\202\000' | basenc --z85
   # 输出: @.FaC
   ```

## WinuxCmd 实现状态

**待实现**