# WinuxCmd 项目开发规范

## 1. 核心定位与目标

### 1.1 项目核心

提供 Windows 原生、轻量级、AI 无关的 Linux 命令集，适配 Windows (PowerShell/CMD) 环境，初期以 "Windows-only" 为主，预留跨平台扩展空间。

### 1.2 量化目标

- 支持上百个高频 Linux 命令（核心参数 1:1 兼容 Linux 行为）
- 合并主程序 winuxcmd.exe 体积 ≤ 1MB
- 单个命令可执行文件（如 ls.exe）体积 ≤ 2KB/文件
- 纯 Windows API 实现，无第三方库依赖，C++23 开发，零额外性能开销
- 编译器：Microsoft Visual Studio 2022+（MSVC 19.34+，完整支持 C++23 稳定特性）

## 2. 开发语言与编码标准

### 2.1 语言标准

- 主开发语言：C++23（仅使用 MSVC 支持的稳定特性，禁用实验性特性）
- 构建工具：CMake（主构建工具，适配 MSVC 编译链）
- 编译器：MSVC 19.34+（VS 2022+ 内置，需安装 "C++ 23 标准库支持" 组件）

### 2.1.1 C++23 模块系统

- 所有核心功能使用 C++23 模块实现
- 分离接口文件（.cppm）和实现文件（.cpp）
- 接口文件使用 `export module`，实现文件使用 `module`
- 构建配置使用 CMake 的 CXX_MODULES 支持

### 2.2 基础编码规则

#### 2.2.1 命名规范

- 文件：snake_case（如 path_resolver.cpp、ls_command.cppm）
- 函数：camelCase（如 parseCommandLine、getFileAttributes）
- 变量：camelCase（如 filePath、optionVerbose）
- 常量：kCamelCase（如 kMaxPathLength、kBufferSize）
- 命名空间：snake_case（如 filesystem_utils、console_output）

#### 2.2.2 代码结构

##### 注释

- 函数：Doxygen 风格注释（仅英文），示例：

```cpp
/**
 * @brief 列出目录内容
 * @param args 命令行参数
 * @return 退出码（成功为 0，失败为非零）
 */
int ls(std::span<std::string_view> args);
```

- 关键逻辑：使用单行注释 // 解释意图，避免冗余注释

##### 函数设计指南

- 使用 `std::span<std::string_view>` 作为命令参数
- 函数保持小而专注，只做一件事
- 返回整数退出码（0 = 成功，非零 = 错误）
- 尽量使用返回值，避免输出参数
- 避免全局变量，使用函数参数传递状态

##### 基于函数的架构示例

```cpp
// 按功能分组命令
namespace filesystem_commands {
    int ls(std::span<std::string_view> args);
    int cp(std::span<std::string_view> args);
    int mv(std::span<std::string_view> args);
    int rm(std::span<std::string_view> args);
}

namespace text_commands {
    int cat(std::span<std::string_view> args);
    int grep(std::span<std::string_view> args);
    int sed(std::span<std::string_view> args);
}
```

##### 模块结构示例

```cpp
// 接口文件：ls_command.cppm
export module ls_command;

import std;

export {
    int execute_ls(std::span<std::string_view> args) noexcept;
}

// 实现文件：ls_command.cpp
module ls_command;

int execute_ls(std::span<std::string_view> args) noexcept {
    // 实现...
    return 0;
}
```

#### 2.2.3 体积与性能约束（MSVC 专属）

##### 禁用冗余 C++ 特性

- 关闭 RTTI：/GR-（MSVC 编译参数）
- 关闭异常：/EHs- /EHc-（禁用 C++ 异常处理）

##### 优先使用 Windows 原生 API

- 目录遍历：使用 FindFirstFileW/FindNextFileW 而非 std::filesystem
- 控制台输出：使用 WriteConsoleA 而非 std::cout
- 内存操作：使用 LocalAlloc/LocalFree（可选）而非 new/delete 以减少 CRT 依赖
- 内存管理：优先使用栈内存（如 char buf[256]），减少堆内存分配，避免不必要的内存拷贝
- 代码复用：将公共逻辑（路径解析、参数解析）抽取到静态库 lxcore.lib，禁止重复代码

##### MSVC 极端体积优化

- 编译优化：/O1 /Os（优先优化体积而非速度）
- 链接优化：/GL /Gy /LTCG /OPT:REF /OPT:ICF（全程序优化，消除冗余符号，函数级链接）
- 移除调试信息：/DEBUG:NONE，禁用 PDB 文件

## 3. 功能开发标准

### 3.1 命令实现规则

- 每个命令仅实现 Linux 核心参数（如 ls 支持 -l/-a/-h/-r，禁用 --color=auto 等稀有参数）
- 命令输出格式：与 Linux 原生输出 1:1 对齐（如 ls -l 输出的权限、大小、时间格式与 Ubuntu 22.04 一致）
- 路径兼容：自动将 Linux 路径（/c/Users）转换为 Windows 路径（C:\Users），无需用户手动适配
- 错误处理：错误信息格式与 Linux 一致（如 ls: cannot access 'test': No such file or directory），仅英文输出
- CRT 依赖：尽可能使用 "CRT-free 模式"（/MT 静态链接 CRT 以避免携带 vcruntime140.dll）

### 3.2 双模式编译规则

- 独立命令模式：每个命令编译为单独的 .exe（如 ls.exe），仅链接核心库 lxcore.lib 和必要的 Windows API
- 合并模式：主程序 winuxcmd.exe 内置所有命令，通过第一个参数分发（如 winuxcmd ls -l）
- 两种模式共享核心逻辑，禁止为不同模式编写重复代码
- 输出格式：所有可执行文件编译为 64 位（x64），禁用 32 位（x86）以减少兼容冗余

## 4. 构建与发布标准

### 4.1 CMake 构建规则（MSVC 适配）

编译必须通过 CMake 执行；CMakeLists.txt 必须强制指定 MSVC 编译链，核心配置示例：

```cmake
cmake_minimum_required(VERSION 3.30)

project(WinuxCmd LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MODULES_DIR "${CMAKE_CURRENT_BINARY_DIR}/modules")
file(MAKE_DIRECTORY "${MODULES_DIR}")

# 核心模块库
add_library(lxcore STATIC)
target_sources(lxcore
    PUBLIC
    FILE_SET CXX_MODULES
    TYPE CXX_MODULES
    BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES
    src/core/path_resolver.cppm
    src/core/cmd_parser.cppm
    src/core/console_output.cppm
)
target_sources(lxcore PRIVATE
    src/core/path_resolver.cpp
    src/core/cmd_parser.cpp
    src/core/console_output.cpp
)

if(MSVC)
    target_compile_options(lxcore PRIVATE
        /O1 /Os              # 优化体积
        /GR-                # 禁用 RTTI
        /EHs- /EHc-         # 禁用异常
        /GL /Gy             # 全程序优化，函数级链接
    )
    target_link_options(lxcore PRIVATE
        /LTCG /OPT:REF /OPT:ICF  # 链接时优化
        /DEBUG:NONE              # 移除调试信息
    )
endif()

# 独立命令编译（示例）
set(COMMANDS ls grep sed)
foreach(CMD ${COMMANDS})
    add_executable(${CMD} src/cmd/${CMD}.cpp)
    target_link_libraries(${CMD} PRIVATE lxcore)
    
    if(MSVC)
        target_compile_options(${CMD} PRIVATE
            /O1 /Os
            /GR-
            /EHs- /EHc-
            /GL /Gy
        )
        target_link_options(${CMD} PRIVATE
            /LTCG /OPT:REF /OPT:ICF
            /DEBUG:NONE
        )
    endif()
    
    set_target_properties(${CMD} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin/cmd"
    )
endforeach()

# 合并主程序
add_executable(winuxcmd src/main.cpp)
target_link_libraries(winuxcmd PRIVATE lxcore)

if(MSVC)
    target_compile_options(winuxcmd PRIVATE
        /O1 /Os
        /GR-
        /EHs- /EHc-
    )
    target_link_options(winuxcmd PRIVATE
        /LTCG /OPT:REF /OPT:ICF
        /DEBUG:NONE
    )
endif()

set_target_properties(winuxcmd PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin"
)
```

- 编译输出目录：
  - 独立命令：bin/cmd/
  - 合并主程序：bin/

### 4.2 manager.py 构建工具使用

项目包含自定义构建工具 `manager.py`（类似 Cargo），简化构建和执行流程。日常开发推荐使用此工具。

#### 4.2.1 功能

- **构建管理**：支持 Debug 和 Release 配置
- **目标列出**：自动检测并列出所有可执行目标
- **目标执行**：直接运行已构建的可执行文件并传递参数
- **环境设置**：自动初始化 MSVC 环境变量
- **编码支持**：处理各种输出编码（UTF-8、GBK、Latin-1）

#### 4.2.2 基本用法

```bash
# 以 Release 模式构建项目
python scripts/manager.py build --release

# 以 Debug 模式构建项目
python scripts/manager.py build --debug

# 列出所有可执行目标
python scripts/manager.py list

# 列出特定配置的目标
python scripts/manager.py list --release
python scripts/manager.py list --debug

# 运行可执行文件（优先级：位置参数 > --target）
python scripts/manager.py run <target_name> -- <args>
python scripts/manager.py run --target <target_name> -- <args>
```

#### 4.2.3 配置

工具配置定义在 `scripts/manager.py` 顶部：

```python
CMAKE_PATH = r"C:\Program Files\CMake\bin\cmake.EXE"
PROJECT_ROOT = r"D:/codespace/WinuxCmd"

BUILD_DIR_DEBUG = os.path.join(PROJECT_ROOT, "cmake-build-debug")
BUILD_DIR_RELEASE = os.path.join(PROJECT_ROOT, "cmake-build-release")

VS_INSTALL_PATH = r"C:\Program Files\Microsoft Visual Studio\18\Community"
ARCH = "x64"
DEFAULT_BUILD_TYPE = "debug"
```

#### 4.2.4 最佳实践

- 日常开发任务始终使用 `manager.py`，而非原始 CMake 命令
- 性能测试和分发使用 Release 模式
- 开发和调试使用 Debug 模式
- 构建后运行 `list` 命令验证编译成功

#### 4.2.5 替代 CMake 命令

如果 `manager.py` 不可用，可使用以下 CMake 命令：

```bash
# 创建构建目录
mkdir build
cd build

# 配置 CMake 使用 MSVC
cmake .. -G "Visual Studio 17 2022" -A x64

# 构建所有目标
cmake --build . --config Release -v
```

### 4.3 发布标准

- 发布包仅包含：64 位可执行文件 + 英文帮助文档（help.txt），无冗余文件
- 版本号：语义化版本（如 v1.0.0），`<major>.<minor>.<patch>`
- 发布说明：仅英文，包含新增命令、修复的兼容问题、体积/性能数据（如 winuxcmd.exe v1.0.0: 890KB）

## 5. 扩展与兼容规则

### 5.1 跨平台预留

- 核心逻辑分层设计：接口层（纯抽象）+ Windows 实现层；后续 Linux/macOS 扩展只需添加实现层
- 禁止在核心逻辑中写入 Windows 独占硬编码（如路径分隔符 \），通过接口封装（如 get_path_separator()）

### 5.2 兼容性约束

- 支持 Windows 10/11（64 位），无需 32 位系统兼容
- 命令行为必须与常见发行版（Ubuntu 22.04、CentOS 9）的 Linux 命令兼容，禁止自定义扩展参数
- 可执行文件必须通过 "Windows Defender 兼容性验证"，避免误报病毒

## 6. 仓库与协作标准

### 6.1 仓库结构

```
winuxcmd/
├── include/          # 公共头文件
├── src/
│   ├── core/         # 核心静态库代码（路径解析、参数解析）
│   ├── cmd/          # 独立命令实现（ls.cpp、grep.cpp 等）
│   └── main.cpp      # 合并主程序入口
├── xmake.lua         # XMake 构建脚本（MSVC 适配）
├── Project_Rules.md  # 项目开发规范
└── README.md         # 项目文档（仅英文）
```

### 6.2 提交标准

提交格式：`<type>: <message>`（仅英文），type 包括：

- `feat`：添加新命令/功能
- `fix`：修复兼容/体积/性能问题
- `docs`：更新文档
- `build`：修改构建脚本/编译器配置

示例：`feat: add ls command with -l/-a/-h params (MSVC optimized)`
