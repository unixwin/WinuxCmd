# 测试框架

## 概述

WinuxCmd 包含一个自定义的端到端 (E2E) 测试框架，专门设计用于测试命令行工具。该框架提供了类似 GTEST 的语法，同时与 CTEST 集成以实现无缝的测试执行。

## 主要特性

- **类 GTEST 语法**：熟悉的 TEST() 和 EXPECT_* 宏
- **CTEST 集成**：与 CMake 的测试运行器无缝集成
- **端到端测试**：测试执行实际的命令行工具
- **跨平台兼容性**：适用于 Windows 和 Linux
- **易于创建测试**：用于常见测试场景的直观 API
- **临时目录管理**：自动清理测试文件

## 测试框架组件

### 1. TempDir

`TempDir` 类为测试操作提供临时目录。它会自动创建唯一的目录，并在测试完成后清理它。

```cpp
// 使用示例
TEST(cp, cp_basic) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");
  
  // 使用 tmp.path 或 tmp.wpath() 的测试代码
}
```

### 2. Pipeline

`Pipeline` 类允许您执行命令并捕获其输出。

```cpp
// 使用示例
Pipeline p;
p.set_cwd(tmp.wpath());
p.add(L"cp.exe", {L"file1.txt", L"file2.txt"});
auto r = p.run();

// 检查结果
EXPECT_EQ(r.exit_code, 0);
EXPECT_TRUE(r.stdout_text.empty());
EXPECT_TRUE(r.stderr_text.empty());
```

### 3. 测试宏

#### TEST 宏

定义带有测试套件名称和测试名称的测试用例。

```cpp
TEST(suite_name, test_name) {
  // 测试代码
}
```

#### EXPECT_* 宏

- `EXPECT_TRUE(condition)`: 检查条件是否为真
- `EXPECT_EQ(a, b)`: 检查值是否相等（支持字符串的详细比较）
- `EXPECT_EQ_TEXT(a, b)`: 检查文本内容是否相等（规范化行尾）
- `EXPECT_BYTES(a, b)`: 检查二进制数据是否相等
- `EXPECT_EXIT_CODE(r, code)`: 检查进程退出码是否匹配

## 创建测试

### 1. 创建测试文件

在 `tests/unit/{command}/` 目录中创建测试文件，例如 `ls_unit_test.cpp`。

### 2. 包含框架头文件

```cpp
#include "tests/framework/pipeline.h"
#include "tests/framework/temp_dir.h"
```

### 3. 编写测试用例

```cpp
TEST(ls, ls_basic) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");
  tmp.write("file2.txt", "content2");
  
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-la"});
  
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("file1.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file2.txt") != std::string::npos);
}

TEST(ls, ls_color) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");
  
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"--color=always"});
  
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  // 检查颜色转义码
  EXPECT_TRUE(r.stdout_text.find("\x1b[") != std::string::npos);
}
```

### 4. 构建和运行测试

```bash
# 构建测试
cmake --build . --config Release

# 运行测试
ctest -C Release

# 运行特定测试
ctest -C Release -R "ls.ls_basic"
```

## 最佳实践

1. **隔离**：每个测试应独立且不影响其他测试
2. **清理**：使用 `TempDir` 确保测试文件被清理
3. **断言**：使用适当的 EXPECT_* 宏获取清晰的错误消息
4. **覆盖**：测试成功和失败场景
5. **文档**：添加注释解释测试意图

## 测试框架架构

### 目录结构

```
tests/
├── framework/          # 测试框架实现
├── unit/               # 每个命令的单元测试
│   ├── cat/            # cat 命令的测试
│   ├── cp/             # cp 命令的测试
│   ├── ls/             # ls 命令的测试
│   └── ...
└── CMakeLists.txt      # CTest 配置
```

### 框架文件

- `wctest.h`: 主测试框架头文件
- `pipeline.h/cpp`: 命令执行管道
- `temp_dir.h`: 临时目录管理
- `paths.h`: 跨平台路径工具
- `process_win32.h/cpp`: Windows 进程管理

## 故障排除

### 测试失败

1. **检查测试输出**：查看测试日志以获取详细的错误消息
2. **运行特定测试**：使用 `ctest -R "test_name"` 运行单个测试
3. **启用详细输出**：使用 `ctest -V` 获取更详细的输出
4. **检查临时文件**：临时禁用清理以检查测试文件

### 常见问题

- **路径问题**：使用 `tmp.wpath()` 获取 Windows 路径，使用 `tmp.path` 获取 UTF-8 路径
- **命令未找到**：确保被测命令已构建并在 PATH 中
- **权限问题**：以适当的权限运行测试
- **输出捕获**：某些命令可能会绕过 stdout/stderr 捕获

## 扩展框架

要向测试框架添加新功能：

1. **修改框架文件**：更新 `tests/framework/` 中的文件
2. **添加新工具**：在 `tests_utils.h` 中创建辅助函数
3. **扩展 Pipeline**：向 `Pipeline` 类添加新方法
4. **添加新宏**：在 `wctest.h` 中定义新的测试宏

## 测试用例示例

### 基本命令测试

```cpp
TEST(cat, cat_basic) {
  TempDir tmp;
  tmp.write("file.txt", "Hello, World!");
  
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cat.exe", {L"file.txt"});
  
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "Hello, World!\n");
}
```

### 带选项的测试

```cpp
TEST(cp, cp_recursive) {
  TempDir tmp;
  tmp.write("src/file.txt", "content");
  
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"-r", L"src", L"dest"});
  
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  
  // 验证文件已复制
  std::ifstream dest_file(tmp.path + "/dest/file.txt");
  std::string content((std::istreambuf_iterator<char>(dest_file)),
                     std::istreambuf_iterator<char>());
  EXPECT_EQ(content, "content");
}
```

## 结论

WinuxCmd 测试框架提供了一种强大而直观的方式来测试命令行工具。其类似 GTEST 的语法和 CTEST 集成使其易于编写和运行测试，而其端到端方法确保命令在真实场景中按预期运行。

通过遵循本文档中概述的最佳实践，您可以创建全面的测试套件，确保 WinuxCmd 命令的质量和可靠性。
