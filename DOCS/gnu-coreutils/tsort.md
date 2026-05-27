# tsort 命令

## 命令概述
`tsort` 命令用于执行拓扑排序。它读取输入中的依赖关系（每行两个元素，表示第一个元素依赖于第二个元素），并输出满足所有依赖关系的线性顺序。常用于解决构建依赖、任务调度等问题。

## 完整参数列表

| 短选项 | 长选项 | 参数 | 说明 |
|--------|--------|------|------|
| | `--help` | 无 | 显示帮助信息 |
| | `--version` | 无 | 显示版本信息 |

## 参数详细说明

### 输入格式
- 每行包含两个元素（用空格或制表符分隔）
- 第一个元素依赖于第二个元素
- 例如："A B" 表示 A 依赖于 B（B 必须在 A 之前）
- 空行被忽略

### 输出格式
- 输出满足所有依赖关系的线性顺序
- 每行一个元素
- 如果存在循环依赖，会输出警告并打破循环

### 特殊行为
- 只出现一次的元素被视为无依赖
- 循环依赖会导致警告，但命令仍会输出一个有效的顺序
- 重复的依赖关系会被忽略

## 使用示例

```bash
# 基本使用
tsort filename.txt

# 从管道读取
echo -e "A B\nB C\nC D" | tsort

# 解决构建依赖
cat dependencies.txt | tsort

# 处理任务调度
tsort tasks.txt

# 从标准输入读取
tsort << EOF
A B
B C
C D
EOF

# 查找循环依赖（通过警告信息）
tsort cyclic_deps.txt

# 处理 Makefile 依赖
make -p 2>/dev/null | grep -E '^[a-zA-Z_]+:' | tsort

# 生成依赖图
echo -e "compile link\nlink assemble\nassemble package" | tsort

# 处理包依赖
cat package_deps.txt | tsort

# 验证依赖顺序
tsort deps.txt | head -1  # 应该是无依赖的元素

# 处理复杂依赖关系
cat << EOF | tsort
main utils
utils lib1
utils lib2
lib1 base
lib2 base
EOF

# 输出：base lib1 lib2 utils main

# 处理多个依赖文件
cat deps1.txt deps2.txt | sort -u | tsort

# 检查是否有循环依赖
if ! tsort deps.txt 2>&1 | grep -q "cycle"; then
    echo "No circular dependencies"
fi

# 生成构建顺序
tsort build_deps.txt | xargs -I {} echo "Build: {}"
```

## WinuxCmd 实现状态
**待实现**

## 相关命令
- `sort`：排序命令
- `make`：构建工具，使用依赖关系
- `ar`：归档工具，有时与 tsort 配合使用
- `ld`：链接器，使用符号依赖

## 注意事项
1. 输入必须是有效的依赖关系（不能有未定义的元素）
2. 循环依赖会导致警告，但命令仍会完成
3. 对于大型依赖图，可能需要较长时间
4. 输出顺序可能因实现而异（只要满足依赖关系）