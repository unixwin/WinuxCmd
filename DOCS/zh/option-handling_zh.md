# 选项处理指南

## 概述

本指南说明了如何在 WinuxCmd 项目中使用选项处理系统。使用新的基于管道的架构，选项处理更加精简和类型安全，为所有命令提供一致的命令行选项处理方式。

## 核心概念

### CommandContext

`CommandContext` 类提供类型安全的命令选项和参数访问。它根据提供的选项定义自动解析选项，并允许您通过适当的类型检查访问它们：

```cpp
// 示例：使用 CommandContext 访问选项
template<size_t N>
auto process_command(const CommandContext<N>& ctx) -> cp::Result<std::vector<std::string>> {
  // 类型安全访问布尔选项
  bool all = ctx.get<bool>("--all", false);
  bool list = ctx.get<bool>("--list", false);
  
  // 类型安全访问数值选项
  int tabsize = ctx.get<int>("--tabsize", 8);
  int width = ctx.get<int>("--width", 0);
  
  // 访问位置参数
  std::vector<std::string> paths;
  for (const auto& arg : ctx.positionals) {
    paths.push_back(std::string(arg));
  }
  
  return paths;
}
```

### OptionMeta

`OptionMeta` 结构定义单个命令选项，具有以下字段：

```cpp
struct OptionMeta {
  const char* short_name; // 短选项（例如，"-l"）
  const char* long_name;  // 长选项（例如，"--list"）
  const char* desc;       // 选项描述
};
```

### OPTION 宏

`OPTION` 宏简化了创建 `OptionMeta` 对象的过程：

```cpp
#define OPTION(short_opt, long_opt, description) \
    cmd::meta::OptionMeta{short_opt, long_opt, description}
```

## 使用示例（ls 命令）

### 1. 定义选项

首先，使用 `OPTION` 宏在 constexpr 数组中定义命令选项：

```cpp
using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

export auto constexpr LS_OPTIONS =
    std::array{
        OPTION("-a", "--all", "不忽略以 . 开头的条目"),
        OPTION("-A", "--almost-all", "不列出隐含的 . 和 .."),
        OPTION("-l", "--list", "使用长列表格式"),
        OPTION("-h", "--human-readable", "与 -l 一起使用，以人类可读格式打印大小"),
        OPTION("-T", "--tabsize", "假设制表位在每个 COLS 处而不是 8"),
        OPTION("-w", "--width", "将输出宽度设置为 COLS。0 表示无限制"),
        // ... 其他选项
    };
```

### 2. 创建管道组件

创建管道组件来处理命令，使用 `CommandContext` 进行选项访问：

```cpp
namespace ls_pipeline {
  namespace cp=core::pipeline;
  
  // 验证参数
  auto validate_arguments(std::span<const std::string_view> args) -> cp::Result<std::vector<std::string>> {
    std::vector<std::string> paths;
    for (auto arg : args) {
      paths.push_back(std::string(arg));
    }
    return paths;
  }

  // 主管道
  template<size_t N>
  auto process_command(const CommandContext<N>& ctx)
      -> cp::Result<std::vector<std::string>>
  {
    return validate_arguments(ctx.positionals);
  }

}
```

### 3. 注册命令

使用 `REGISTER_COMMAND` 宏注册命令，传递选项定义：

```cpp
REGISTER_COMMAND(ls,
                 /* name */
                 "ls",

                 /* synopsis */
                 "ls [选项]... [文件]...",

                 /* description */
                 "列出有关文件的信息（默认为当前目录）。\n"
                 "如果未指定 -cftuvSUX 或 --sort，则按字母顺序排序条目。",

                 /* examples */
                 "  ls -la                  以长格式列出所有文件\n"
                 "  ls -lh                 以人类可读格式列出文件大小\n"
                 "  ls -la /path/to/dir    列出指定目录中的所有文件",

                 /* see_also */
                 "cp, mv, rm, mkdir, rmdir",

                 /* author */
                 "WinuxCmd Team",

                 /* copyright */
                 "Copyright © 2026 WinuxCmd",

                 /* options */
                 LS_OPTIONS
) {
  using namespace ls_pipeline;

  auto result = process_command(ctx);
  if (!result) {
    cp::report_error(result, L"ls");
    return 1;
  }

  auto paths = *result;
  
  // 访问选项
  bool all = ctx.get<bool>("--all", false);
  bool almost_all = ctx.get<bool>("--almost-all", false);
  bool list = ctx.get<bool>("--list", false);
  bool human_readable = ctx.get<bool>("--human-readable", false);
  int tabsize = ctx.get<int>("--tabsize", 8);
  int width = ctx.get<int>("--width", 0);
  
  // 命令逻辑
  // ...

  return 0;
}
```

## 迁移指南

要将现有命令迁移到新的选项处理系统：

1. **添加必要的导入**：
   ```cpp
   import core;
   import utils;
   using cmd::meta::OptionMeta;
   using cmd::meta::OptionType;
   ```

2. **定义选项**：
   - 用使用 `OPTION` 宏的 constexpr 数组替换手动选项定义
   - 确保所有选项在适当的情况下都有短形式和长形式

3. **更新命令结构**：
   - 为命令创建管道命名空间
   - 实现 `validate_arguments` 和 `process_command` 函数
   - 使用 `CommandContext` 进行选项访问，而不是手动解析

4. **注册命令**：
   - 使用新的 `REGISTER_COMMAND` 宏格式
   - 传递选项定义数组

5. **更新错误处理**：
   - 使用 `core::pipeline::Result` 进行错误报告
   - 使用 `cp::report_error` 显示错误

## 最佳实践

- **类型安全**：始终使用 `ctx.get<T>()` 并为选项访问使用适当的类型
- **默认值**：访问选项时始终提供默认值
- **一致性**：尽可能在命令之间使用相同的选项名称
- **文档**：为所有选项提供清晰的描述
- **错误处理**：使用管道组件和 `Result` 进行一致的错误处理
- **模块化**：将验证与处理逻辑分离

## 故障排除

### 常见问题

1. **选项未找到**：确保选项在选项数组中定义
2. **类型不匹配**：确保 `ctx.get<T>()` 中使用的类型与预期类型匹配
3. **默认值**：确保为所有选项提供适当的默认值
4. **模块导入**：确保已导入所有必要的模块

### 调试技巧

- 打印 `CommandContext` 以验证选项是否被正确解析
- 检查位置参数是否被正确捕获
- 确保选项数组中的选项名称与 `ctx.get<T>()` 中使用的名称匹配

## 示例：完整实现

`echo.cppm` 文件作为新选项处理系统的参考实现。主要功能包括：

1. **选项定义**：使用带有 `OPTION` 宏的 constexpr 数组
2. **管道组件**：实现验证和处理函数
3. **CommandContext 使用**：使用类型安全的选项访问
4. **错误处理**：使用 `core::pipeline::Result` 进行错误报告
5. **命令注册**：使用新的 `REGISTER_COMMAND` 宏格式

通过遵循此模式，所有命令都可以受益于一致、类型安全和可维护的选项处理。
