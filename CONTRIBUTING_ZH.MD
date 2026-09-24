# WinuxCmd 贡献指南

感谢您考虑为 WinuxCmd 项目做贡献！本文档提供了向项目贡献的指南和说明。

## 目录

- [入门指南](#入门指南)
- [开发工作流](#开发工作流)
- [代码风格](#代码风格)
- [高性能 C++23 编码规范](#高性能-c23-编码规范)
- [测试](#测试)
- [提交更改](#提交更改)
- [报告问题](#报告问题)
- [功能请求](#功能请求)
- [文档](#文档)
- [行为准则](#行为准则)

## 入门指南

### 先决条件

- Visual Studio 2026 或更高版本 (MSVC)
- CMake 3.21 或更高版本
- Windows 10 或更高版本
- Git

### 设置开发环境

1. **克隆仓库**
   ```bash
   git clone https://github.com/caomengxuan666/WinuxCmd.git
   cd WinuxCmd
   ```

2. **配置 CMake**
   ```bash
   mkdir build
   cd build
   cmake .. -G "Visual Studio 17 2026" -A x64
   ```

3. **构建项目**
   ```bash
   cmake --build . --config Release
   ```

4. **运行测试**
   ```bash
   ctest -C Release
   ```

## 开发工作流

### 分支策略

- `main`: 主开发分支
- 功能分支: 用于新功能和错误修复
- 发布分支: 用于版本发布

### 创建新命令

1. **使用脚手架工具**
   ```bash
   # 构建脚手架工具
   cmake --build . --target scaffold
   
   # 创建新命令
   ./scaffold.exe mycommand
   ```

2. **实现命令**
   - 编辑 `src/commands/` 中生成的文件
   - 遵循现有的命令模式

3. **添加测试**
   - 在 `tests/unit/mycommand/` 中创建测试文件
   - 遵循现有的测试模式

4. **更新文档**
   - 将命令添加到 `README.md` 和 `README-zh.md`
   - 更新命令实现文档

## 代码风格

### C++ 风格指南

WinuxCmd 遵循 Google C++ 风格指南并进行了一些修改：

- **缩进**: 2 个空格（无制表符）
- **行长度**: 80 个字符
- **命名约定**:
  - 类: `CamelCase` (例如 `MyClass`)
  - 函数: `camelCase` (例如 `myFunction`)
  - 变量: `camelCase` (例如 `myVariable`)
  - 常量: `kCamelCase` (例如 `kMyConstant`)
  - 宏: `UPPER_SNAKE_CASE` (例如 `MY_MACRO`)
- **大括号**: K&R 风格（左大括号在同一行，右大括号在新行）
- **包含顺序**: 标准库优先，然后是第三方库，最后是项目头文件

### 格式化

使用提供的格式化脚本来确保一致的格式：

```bash
winuxsh scripts/format.sh
```

### 代码检查

项目使用 clang-tidy 进行静态分析：

```bash
# 运行 clang-tidy
clang-tidy src/commands/*.cpp
```

## 高性能 C++23 编码规范

为了保持 WinuxCmd 的轻量级和高性能特性，所有代码必须遵循这些严格的字符串处理和 I/O 操作指南。

### 🎯 核心原则

| 原则 | 说明 |
|------|------|
| ✅ 内部用 UTF-8 | 所有业务逻辑、计算、格式化使用 `std::string` / `char` / `snprintf` |
| ⚠️ 边界用宽字符 | 仅在 Windows API 调用时使用 `std::wstring`，用完立即转回 |
| ✅ 输出用 safePrint | 库负责控制台/管道适配，业务代码零成本 |
| ❌ 禁止临时 wstring | 绝不要写 `std::wstring(1, c)` 或 `L"x" + wstring` |

### 📊 字符串类型选择指南

#### ✅ 推荐使用（零开销）

| 类型 | 使用场景 | 示例 |
|------|----------|------|
| `const char*` | 字符串字面量、常量 | `"Hello"`, `COLOR_DIR_A` |
| `std::string_view` | 只读字符串参数、切片 | `void print(std::string_view sv)` |
| `char[]` | 栈上格式化缓冲区 | `char buf[32]; snprintf(...)` |
| `std::string`（SSO） | 短字符串、需要修改 | `std::string s = "abc";` |

#### ⚠️ 谨慎使用（有成本）

| 类型 | 成本 | 替代方案 |
|------|------|----------|
| `std::wstring` | 每个字符 2 字节 + 堆分配 | 只在 Windows API 使用 |
| `std::ostringstream` | 8.6KB 代码 + 堆分配 | `snprintf` |
| `std::wostringstream` | 8.6KB 代码 + 堆分配 ❌ | 绝对禁止 |

#### ❌ 禁止使用（高成本）

| 写法 | 问题 | 替代方案 |
|------|------|----------|
| `std::wstring(1, c)` | 1.2KB 代码/次 | `safePrint(char)` |
| `L"^" + std::wstring(...)` | 2.4KB 代码/次 | `safePrint('^') + safePrint(c)` |
| `std::wstring(path.begin(), path.end())` | 拷贝整个字符串 | `utf8_to_wstring(path)` |
| `std::wostringstream` | 8.6KB 代码/次 | `snprintf + std::string` |

### 🛠️ 字符串操作规范

#### ✅ 1. 字符串拼接

```cpp
// ❌ 禁止 - 每个 operator+ 实例化 2.4KB 代码
std::string s = a + b + c;

// ✅ 推荐 - 使用 operator+= 或 append
std::string s = a;
s += b;
s += c;

// ✅ 或预分配容量
std::string s;
s.reserve(a.size() + b.size() + c.size());
s.append(a);
s.append(b);
s.append(c);
```

#### ✅ 2. 数字格式化

```cpp
// ❌ 禁止 - wostringstream/ostringstream 8.6KB
std::wostringstream oss;
oss << std::setw(6) << n << L" ";
safePrint(oss.str());

// ✅ 推荐 - snprintf + string_view (0.1KB)
char buf[32];
int len = snprintf(buf, sizeof(buf), "%6d ", n);
safePrint(std::string_view(buf, len));
```

#### ✅ 3. 字符输出

```cpp
// ❌ 禁止 - 构造临时 wstring
safePrint(std::wstring(1, c));
safePrint(L"^" + std::wstring(1, c + 0x40));

// ✅ 推荐 - 直接输出 char
safePrint(c);
safePrint('^');
safePrint(c + 0x40);
```

#### ✅ 4. 路径操作

```cpp
// ❌ 禁止 - 多次临时对象
std::wstring path = base + L"\\" + file;

// ✅ 推荐 - 单次构造，两次追加
std::wstring path = base;
path += L'\\';
path += file;
```

#### ✅ 5. 颜色输出

```cpp
// ✅ 推荐 - 使用 const char* 版本
export constexpr auto COLOR_DIR_A = "\033[01;34m";

// 直接输出，零构造
safePrint(COLOR_DIR_A);
safePrint(filename);
safePrint(COLOR_RESET_A);
```

### 📋 safePrint 重载选择指南

| 输入类型 | 选择的重载 | 成本 |
|----------|------------|------|
| `"string literal"` | `safePrint(const char*)` | 零构造 ✅ |
| `std::string` | `safePrint(std::string_view)` | 隐式转换 |
| `std::string_view` | `safePrint(std::string_view)` | 零拷贝 ✅ |
| `char c` | `safePrint(char)` | 零构造 ✅ |
| `int n` | `safePrint(int)` | 栈格式化 |
| `L"wide literal"` | `safePrint(const wchar_t*)` | 零构造 ✅ |
| `std::wstring` | `safePrint(std::wstring_view)` | 隐式转换 |
| `COLOR_DIR` (`wchar_t*`) | `safePrint(const wchar_t*)` | 零构造 ✅ |

**重要**：必须添加 `const wchar_t*` 重载，否则 `safePrint(COLOR_DIR)` 会构造临时 `std::wstring`！

### 🚫 绝对不要写的代码

#### ❌ 禁止清单

```cpp
// 1. 任何 std::wostringstream
std::wostringstream oss;  // ❌ 8.6KB

// 2. 任何 wstring 拼接
L"a" + std::wstring(b);   // ❌ 2.4KB
std::wstring(1, c);       // ❌ 1.2KB

// 3. 不必要的 string -> wstring 转换
std::wstring(path.begin(), path.end());  // ❌ O(n)

// 4. 非成员 operator+ 拼接
std::string s = a + b + c;  // ❌ 每个 + 实例化 2.4KB

// 5. iostream 格式化
std::ostringstream oss;     // ❌ 8.6KB
std::cout << x;            // ❌ 15KB iostream 代码
```

### ✅ 推荐代码模板

#### 📁 文件大小格式化

```cpp
auto format_size(uint64_t size, bool human_readable) -> std::string {
    char buf[32];
    if (human_readable) {
        const char* units[] = {"B", "K", "M", "G", "T"};
        int unit = 0;
        double s = static_cast<double>(size);
        while (s >= 1024.0 && unit < 4) { s /= 1024.0; unit++; }
        snprintf(buf, sizeof(buf), s < 10.0 ? "%.1f%s" : "%.0f%s", s, units[unit]);
    } else {
        snprintf(buf, sizeof(buf), "%llu", size);
    }
    return std::string(buf);
}
```

#### 📁 时间格式化

```cpp
auto format_time(const FILETIME& ft) -> std::string {
    SYSTEMTIME st;
    FILETIME local_ft;
    FileTimeToLocalFileTime(&ft, &local_ft);
    FileTimeToSystemTime(&local_ft, &st);
    
    const char* months[] = {"", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char buf[64];
    snprintf(buf, sizeof(buf), "%s %2d %02d:%02d",
             months[st.wMonth], st.wDay, st.wHour, st.wMinute);
    return std::string(buf);
}
```

#### 📁 路径拼接

```cpp
// Windows API 需要宽字符路径
std::wstring build_path(const std::wstring& base, const std::wstring& file) {
    std::wstring path = base;
    path += L'\\';
    path += file;
    return path;
}

// 内部使用 UTF-8
std::string build_path_utf8(const std::string& base, const std::string& file) {
    std::string path = base;
    path += '\\';
    path += file;
    return path;
}
```

### 📊 成本对照表

| 操作 | 代码大小 | 堆分配 | 推荐度 |
|------|----------|--------|--------|
| `safePrint('c')` | 0.1KB | 无 | ✅✅✅ |
| `safePrint("str")` | 0.1KB | 无 | ✅✅✅ |
| `snprintf + string_view` | 0.3KB | 无 | ✅✅✅ |
| `std::string::operator+=` | 0.5KB | 可能 | ✅✅ |
| `std::string::append` | 0.5KB | 可能 | ✅✅ |
| `std::wstring::operator+=` | 0.8KB | 可能 | ⚠️ |
| `std::string a + b` | 2.4KB | 是 | ❌ |
| `std::wstring(1, c)` | 1.2KB | 是 | ❌❌ |
| `std::ostringstream` | 8.6KB | 是 | ❌❌ |
| `std::wostringstream` | 8.6KB | 是 | ❌❌❌ |
| `std::cout` | 15KB+ | 是 | ❌❌❌ |

### 🎯 总结

#### ✅ Do

- 内部用 UTF-8：`std::string`、`char`、`snprintf`
- 输出用 `safePrint` 重载集
- 字符串拼接用 `+=` 或 `append`
- 数字格式化用 `snprintf`
- Windows API 边界才用 `std::wstring`

#### ❌ Don't

- 不要用 `std::wostringstream`
- 不要写 `std::wstring(1, c)`
- 不要用 `L"x" + wstring`
- 不要用 `std::string(a + b + c)`
- 不要用 `std::cout`/`std::wcout`

遵循本规范，您的代码将：

- 🚀 编译更快 - 减少模板实例化
- 💾 体积更小 - 省 30-50% 代码大小
- ⚡ 运行更快 - 零堆分配、零临时对象
- 🔧 维护更容易 - 统一、可预测的模式

任何问题，请参考 `cat.cppm`（已优化）和 `ls.cppm`（优化中）的实现。

## 测试

### 编写测试

1. **创建测试文件** 在 `tests/unit/{command}/`
2. **使用测试框架** 如 `DOCS/zh/testing_framework.md` 中所述
3. **遵循最佳实践**:
   - 隔离测试
   - 使用 `TempDir` 处理临时文件
   - 测试成功和失败场景
   - 添加清晰的断言

### 运行测试

```bash
# 运行所有测试
ctest -C Release

# 运行特定测试
ctest -C Release -R "ls.ls_basic"

# 详细输出运行测试
ctest -C Release -V
```

### 测试覆盖率

虽然目前没有正式收集测试覆盖率指标，但鼓励贡献者：

- 测试所有命令选项和标志
- 测试边缘情况和错误条件
- 确保测试在 32 位和 64 位系统上都能通过

## 提交更改

### 拉取请求流程

1. **Fork 仓库**
2. **创建功能分支**
   ```bash
   git checkout -b feature/my-feature
   ```
3. **进行更改**
4. **提交更改** 并使用清晰的提交信息
   ```bash
   git commit -m "[Feature] Add my-feature command"
   ```
5. **推送更改** 到您的 fork
   ```bash
   git push origin feature/my-feature
   ```
6. **创建拉取请求**
   - 详细描述更改
   - 引用相关问题
   - 确保所有测试通过

### 提交信息格式

提交信息请遵循以下格式：

```
[区域] 更改的简要描述

详细解释更改内容（如果需要）。

Fixes #issue_number（如果适用）
```

其中 `区域` 可以是：
- `Command`: 命令实现
- `Framework`: 测试框架更改
- `Core`: 核心功能更改
- `Docs`: 文档更改
- `Build`: 构建系统更改

## 报告问题

### 错误报告

报告错误时，请包含：

- **重现步骤**
- **预期行为**
- **实际行为**
- **环境**（Windows 版本，Visual Studio 版本）
- **错误消息**（如果有）
- **截图**（如果适用）

### 安全问题

对于安全问题，请直接联系维护者 <2507560089@qq.com>，而不是创建公开问题。

## 功能请求

请求新功能时，请包含：

- **功能描述**
- **功能用例**
- **功能使用示例**
- **类似工具中的参考实现**（如果适用）

## 文档

### 更新文档

- **README.md** 和 **README-zh.md**: 为新命令和功能更新
- **DOCS/en/**: 英文文档
- **DOCS/zh/**: 中文文档

### 文档指南

- 使用清晰、简洁的语言
- 在适当的地方提供示例
- 遵循现有的文档结构
- 保持英文和中文文档同步

## 行为准则

### 我们的承诺

我们作为成员、贡献者和领导者，承诺使我们的社区成为一个对所有人（无论年龄、体型、可见或不可见的残疾、种族、性别特征、性别认同和表达、经验水平、教育程度、社会经济地位、国籍、个人外貌、种族、宗教或性取向和身份）都免受骚扰的参与体验。

### 我们的标准

有助于创造积极环境的行为包括：

- 使用欢迎和包容的语言
- 尊重不同的观点和经验
- 优雅地接受建设性批评
- 专注于社区的最佳利益
- 对其他社区成员表现出同理心

参与者不可接受的行为包括：

- 使用性暗示语言或图像以及不受欢迎的性关注或挑逗
- 人身攻击、侮辱/贬低评论和政治攻击
- 公开或私下骚扰
- 未经明确许可发布他人的私人信息（如物理或电子地址）
- 其他在专业环境中可能被视为不适当的行为

### 执行责任

社区领导者负责澄清和执行我们可接受行为的标准，并将对任何他们认为不适当、威胁、冒犯或有害的行为采取适当和公平的纠正措施。

### 范围

本行为准则适用于所有社区空间，也适用于个人在公共空间中正式代表社区时。

### 执行

滥用、骚扰或其他不可接受行为的实例可以通过联系维护者 <2507560089@qq.com> 进行报告。所有投诉都将被审查和调查，并将根据情况做出必要和适当的回应。

## 致谢

感谢您对 WinuxCmd 的贡献！您的帮助使这个项目对每个人都更好。

## 联系方式

- 维护者: <2507560089@qq.com>
- GitHub: [@caomengxuan666](https://github.com/caomengxuan666)
- 网站: [blog.caomengxuan666.com](https://dl.caomengxuan666.com)
