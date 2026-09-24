# 构建模式配置说明

## 概述

WinuxCmd 现在支持两种构建模式：**开发模式**(DEV) 和 **发布模式**(RELEASE)，每种模式针对不同的使用场景进行了优化。

## 构建模式选项

### BUILD_MODE
控制整体构建配置的主选项：
- `DEV` - 开发模式（默认）
- `RELEASE` - 发布模式

## 各模式详细配置

### 开发模式 (DEV)
适用于日常开发和调试，优化快速增量编译：

| 选项 | 默认值 | 说明 |
|------|--------|------|
| ENABLE_TESTS | ON | 启用单元测试 |
| GENERATE_MAP_INFO | ON | 生成映射文件用于调试 |
| ENABLE_UNITY_BUILD | OFF | 禁用Unity构建（加快增量编译） |
| ENABLE_UPX | OFF | 禁用UPX压缩 |

**优势：**
- 增量编译速度快
- 完整的调试信息
- 包含所有测试用例
- 便于开发调试

### 发布模式 (RELEASE)
适用于生产环境，优化性能和文件大小：

| 选项 | 默认值 | 说明              |
|------|--------|-----------------|
| ENABLE_TESTS | OFF | 禁用单元测试          |
| GENERATE_MAP_INFO | OFF | 不生成映射文件         |
| ENABLE_UNITY_BUILD | ON | 启用Unity构建（提高性能） |
| ENABLE_UPX | ON | 禁用UPX压缩         |

**优势：**
- 更小的可执行文件体积
- 更快的运行时性能
- 移除了调试和测试代码
- 适合分发给最终用户

## 使用方法

### 1. 命令行构建

#### 开发模式构建（默认）
```bash
cmake -B build
cmake --build build
```

#### 显式指定开发模式
```bash
cmake -B build -DBUILD_MODE=DEV
cmake --build build
```

#### 发布模式构建
```bash
cmake -B build -DBUILD_MODE=RELEASE
cmake --build build --config Release
```

### 2. 覆盖默认配置

即使在特定模式下，也可以覆盖单个选项：

```bash
# 在开发模式下启用Unity构建
cmake -B build -DBUILD_MODE=DEV -DENABLE_UNITY_BUILD=ON

# 在发布模式下保留测试
cmake -B build -DBUILD_MODE=RELEASE -DENABLE_TESTS=ON

# 在发布模式下禁用UPX压缩
cmake -B build -DBUILD_MODE=RELEASE -DENABLE_UPX=OFF
```

## 构建输出示例

### 开发模式输出
```
-- Configuring for DEVELOPMENT mode
--   - Fast incremental builds: ENABLED
--   - Tests: ENABLED
--   - Map generation: ENABLED
--   - Unity build: DISABLED (for faster incremental compilation)
--   - UPX compression: DISABLED
-- =================== BUILD CONFIGURATION SUMMARY ===================
-- Build Mode: DEV
-- Tests Enabled: ON
-- Map Info Generation: ON
-- Unity Build: OFF
-- UPX Compression: OFF
-- Static CRT: ON
-- Hardlinks: ON
-- =================================================================
```

### 发布模式输出
```
-- Configuring for RELEASE mode
--   - Optimized performance and size: ENABLED
--   - Tests: DISABLED
--   - Map generation: DISABLED
--   - Unity build: ENABLED (improves performance and reduces size)
--   - UPX compression: ENABLED
-- =================== BUILD CONFIGURATION SUMMARY ===================
-- Build Mode: RELEASE
-- Tests Enabled: OFF
-- Map Info Generation: OFF
-- Unity Build: ON
-- UPX Compression: ON
-- Static CRT: ON
-- Hardlinks: ON
-- =================================================================
```

## 最佳实践建议

### 日常开发
```bash
# 使用默认开发模式
cmake -B build-dev
cmake --build build-dev
```

### 性能测试
```bash
# 启用Unity构建进行性能测试
cmake -B build-perf -DBUILD_MODE=DEV -DENABLE_UNITY_BUILD=ON
cmake --build build-perf
```

### 发布准备
```bash
# 完整的发布构建
cmake -B build-release -DBUILD_MODE=RELEASE
cmake --build build-release --config Release
```

### CI/CD 流水线
```bash
# 开发环境构建（包含测试）
cmake -B build-ci-dev -DBUILD_MODE=DEV
cmake --build build-ci-dev
ctest --test-dir build-ci-dev

# 生产环境构建
cmake -B build-ci-prod -DBUILD_MODE=RELEASE
cmake --build build-ci-prod --config Release
```

## 注意事项

1. **增量编译**：开发模式禁用Unity构建是为了获得最快的增量编译速度
2. **首次构建**：无论哪种模式，首次构建都会比较慢
3. **磁盘空间**：开发模式会生成更多的中间文件和调试信息
4. **运行性能**：发布模式的可执行文件通常有20-30%的性能提升
5. **文件大小**：发布模式结合UPX压缩可以显著减小文件体积

## 故障排除

如果遇到配置问题：

1. 清理构建目录：
   ```bash
   rm -rf build/
   ```

2. 检查CMake版本：
   ```bash
   cmake --version
   ```
   确保使用 CMake 3.30.0 或更高版本

3. 查看详细的配置信息：
   ```bash
   cmake -B build -DBUILD_MODE=DEV --debug-output
   ```