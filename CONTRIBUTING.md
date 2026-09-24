# Contributing to WinuxCmd

Thank you for considering contributing to WinuxCmd! This document provides guidelines and instructions for contributing to the project.

## Table of Contents

- [Getting Started](#getting-started)
- [Development Workflow](#development-workflow)
- [Code Style](#code-style)
- [High-Performance C++23 Coding Guidelines](#high-performance-c23-coding-guidelines)
- [Testing](#testing)
- [Submitting Changes](#submitting-changes)
- [Reporting Issues](#reporting-issues)
- [Feature Requests](#feature-requests)
- [Documentation](#documentation)
- [Code of Conduct](#code-of-conduct)

## Getting Started

### Prerequisites

- Visual Studio 2026 or later (MSVC)
- CMake 3.21 or later
- Windows 10 or later
- Git

### Setting Up the Development Environment

1. **Clone the repository**
   ```bash
   git clone https://github.com/caomengxuan666/WinuxCmd.git
   cd WinuxCmd
   ```

2. **Configure CMake**
   ```bash
   mkdir build
   cd build
   cmake .. -G "Visual Studio 17 2026" -A x64
   ```

3. **Build the project**
   ```bash
   cmake --build . --config Release
   ```

4. **Run tests**
   ```bash
   ctest -C Release
   ```

## Development Workflow

### Branching Strategy

- `main`: Main development branch
- Feature branches: For new features and bug fixes
- Release branches: For version releases

### Creating a New Command

1. **Use the scaffold tool**
   ```bash
   # Build the scaffold tool
   cmake --build . --target scaffold
   
   # Create a new command
   ./scaffold.exe mycommand
   ```

2. **Implement the command**
   - Edit the generated files in `src/commands/`
   - Follow the existing command patterns

3. **Add tests**
   - Create test files in `tests/unit/mycommand/`
   - Follow the existing test patterns

4. **Update documentation**
   - Add the command to `README.md` and `README-zh.md`
   - Update the command implementation documentation

## Code Style

### C++ Style Guide

WinuxCmd follows the Google C++ Style Guide with some modifications:

- **Indentation**: 2 spaces (no tabs)
- **Line Length**: 80 characters
- **Naming Conventions**:
  - Classes: `CamelCase` (e.g., `MyClass`)
  - Functions: `camelCase` (e.g., `myFunction`)
  - Variables: `camelCase` (e.g., `myVariable`)
  - Constants: `kCamelCase` (e.g., `kMyConstant`)
  - Macros: `UPPER_SNAKE_CASE` (e.g., `MY_MACRO`)
- **Braces**: K&R style (opening brace on same line, closing brace on new line)
- **Include Order**: Standard libraries first, then third-party libraries, then project headers

### Formatting

Use the provided format script to ensure consistent formatting:

```bash
winuxsh scripts/format.sh
```

### Linting

The project uses clang-tidy for static analysis:

```bash
# Run clang-tidy
clang-tidy src/commands/*.cpp
```

## High-Performance C++23 Coding Guidelines

To maintain WinuxCmd's lightweight and high-performance characteristics, all code must follow these strict guidelines for string handling and I/O operations.

### 🎯 Core Principles

| Principle | Description |
|-----------|-------------|
| ✅ Internal UTF-8 | All business logic, calculations, and formatting use `std::string` / `char` / `snprintf` |
| ⚠️ Boundary Wide Chars | Only use `std::wstring` when calling Windows API, convert back immediately |
| ✅ Output via safePrint | The library handles console/pipe adaptation; business code is zero-cost |
| ❌ No Temporary wstrings | Never write `std::wstring(1, c)` or `L"x" + wstring` |

### 📊 String Type Selection Guide

#### ✅ Recommended (Zero Overhead)

| Type | Usage Scenario | Example |
|------|----------------|---------|
| `const char*` | String literals, constants | `"Hello"`, `COLOR_DIR_A` |
| `std::string_view` | Read-only string parameters, slices | `void print(std::string_view sv)` |
| `char[]` | Stack formatting buffers | `char buf[32]; snprintf(...)` |
| `std::string` (SSO) | Short strings, need modification | `std::string s = "abc";` |

#### ⚠️ Use Carefully (Has Cost)

| Type | Cost | Alternative |
|------|------|-------------|
| `std::wstring` | 2 bytes per char + heap allocation | Only at Windows API boundaries |
| `std::ostringstream` | 8.6KB code + heap allocation | `snprintf` |
| `std::wostringstream` | 8.6KB code + heap allocation ❌ | Absolutely forbidden |

#### ❌ Forbidden (High Cost)

| Code | Problem | Alternative |
|------|---------|-------------|
| `std::wstring(1, c)` | 1.2KB code per instance | `safePrint(char)` |
| `L"^" + std::wstring(...)` | 2.4KB code per instance | `safePrint('^') + safePrint(c)` |
| `std::wstring(path.begin(), path.end())` | Copies entire string | `utf8_to_wstring(path)` |
| `std::wostringstream` | 8.6KB code per instance | `snprintf + std::string` |

### 🛠️ String Operation Guidelines

#### ✅ 1. String Concatenation

```cpp
// ❌ Forbidden - Each operator+ instantiates 2.4KB code
std::string s = a + b + c;

// ✅ Recommended - Use operator+= or append
std::string s = a;
s += b;
s += c;

// ✅ Or pre-allocate capacity
std::string s;
s.reserve(a.size() + b.size() + c.size());
s.append(a);
s.append(b);
s.append(c);
```

#### ✅ 2. Number Formatting

```cpp
// ❌ Forbidden - wostringstream/ostringstream 8.6KB
std::wostringstream oss;
oss << std::setw(6) << n << L" ";
safePrint(oss.str());

// ✅ Recommended - snprintf + string_view (0.1KB)
char buf[32];
int len = snprintf(buf, sizeof(buf), "%6d ", n);
safePrint(std::string_view(buf, len));
```

#### ✅ 3. Character Output

```cpp
// ❌ Forbidden - Construct temporary wstring
safePrint(std::wstring(1, c));
safePrint(L"^" + std::wstring(1, c + 0x40));

// ✅ Recommended - Direct char output
safePrint(c);
safePrint('^');
safePrint(c + 0x40);
```

#### ✅ 4. Path Operations

```cpp
// ❌ Forbidden - Multiple temporary objects
std::wstring path = base + L"\\" + file;

// ✅ Recommended - Single construction, two appends
std::wstring path = base;
path += L'\\';
path += file;
```

#### ✅ 5. Color Output

```cpp
// ✅ Recommended - Use const char* versions
export constexpr auto COLOR_DIR_A = "\033[01;34m";

// Direct output, zero construction
safePrint(COLOR_DIR_A);
safePrint(filename);
safePrint(COLOR_RESET_A);
```

### 📋 safePrint Overload Selection Guide

| Input Type | Selected Overload | Cost |
|------------|-------------------|------|
| `"string literal"` | `safePrint(const char*)` | Zero construction ✅ |
| `std::string` | `safePrint(std::string_view)` | Implicit conversion |
| `std::string_view` | `safePrint(std::string_view)` | Zero copy ✅ |
| `char c` | `safePrint(char)` | Zero construction ✅ |
| `int n` | `safePrint(int)` | Stack formatting |
| `L"wide literal"` | `safePrint(const wchar_t*)` | Zero construction ✅ |
| `std::wstring` | `safePrint(std::wstring_view)` | Implicit conversion |
| `COLOR_DIR` (`wchar_t*`) | `safePrint(const wchar_t*)` | Zero construction ✅ |

**Important**: Must add `const wchar_t*` overload, otherwise `safePrint(COLOR_DIR)` will construct temporary `std::wstring`!

### 🚫 Absolutely Forbidden Code

#### ❌ Forbidden List

```cpp
// 1. Any std::wostringstream
std::wostringstream oss;  // ❌ 8.6KB

// 2. Any wstring concatenation
L"a" + std::wstring(b);   // ❌ 2.4KB
std::wstring(1, c);       // ❌ 1.2KB

// 3. Unnecessary string -> wstring conversion
std::wstring(path.begin(), path.end());  // ❌ O(n)

// 4. Non-member operator+ concatenation
std::string s = a + b + c;  // ❌ Each + instantiates 2.4KB

// 5. iostream formatting
std::ostringstream oss;     // ❌ 8.6KB
std::cout << x;            // ❌ 15KB iostream code
```

### ✅ Recommended Code Templates

#### 📁 File Size Formatting

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

#### 📁 Time Formatting

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

#### 📁 Path Concatenation

```cpp
// Windows API needs wide character paths
std::wstring build_path(const std::wstring& base, const std::wstring& file) {
    std::wstring path = base;
    path += L'\\';
    path += file;
    return path;
}

// Internal use UTF-8
std::string build_path_utf8(const std::string& base, const std::string& file) {
    std::string path = base;
    path += '\\';
    path += file;
    return path;
}
```

### 📊 Cost Comparison Table

| Operation | Code Size | Heap Allocation | Recommendation |
|-----------|-----------|-----------------|----------------|
| `safePrint('c')` | 0.1KB | None | ✅✅✅ |
| `safePrint("str")` | 0.1KB | None | ✅✅✅ |
| `snprintf + string_view` | 0.3KB | None | ✅✅✅ |
| `std::string::operator+=` | 0.5KB | Possible | ✅✅ |
| `std::string::append` | 0.5KB | Possible | ✅✅ |
| `std::wstring::operator+=` | 0.8KB | Possible | ⚠️ |
| `std::string a + b` | 2.4KB | Yes | ❌ |
| `std::wstring(1, c)` | 1.2KB | Yes | ❌❌ |
| `std::ostringstream` | 8.6KB | Yes | ❌❌ |
| `std::wostringstream` | 8.6KB | Yes | ❌❌❌ |
| `std::cout` | 15KB+ | Yes | ❌❌❌ |

### 🎯 Summary

#### ✅ Do

- Internal UTF-8: `std::string`, `char`, `snprintf`
- Output via `safePrint` overload set
- String concatenation with `+=` or `append`
- Number formatting with `snprintf`
- Use `std::wstring` only at Windows API boundaries

#### ❌ Don't

- Don't use `std::wostringstream`
- Don't write `std::wstring(1, c)`
- Don't use `L"x" + wstring`
- Don't use `std::string(a + b + c)`
- Don't use `std::cout`/`std::wcout`

Following these guidelines ensures your code will:

- 🚀 Compile faster - Reduced template instantiation
- 💾 Smaller binary size - Save 30-50% code size
- ⚡ Run faster - Zero heap allocation, zero temporary objects
- 🔧 Easier maintenance - Consistent, predictable patterns

For reference implementations, see `cat.cppm` (optimized) and `ls.cppm` (optimization in progress).

## Testing

### Writing Tests

1. **Create test files** in `tests/unit/{command}/`
2. **Use the test framework** described in `DOCS/en/testing_framework_en.md`
3. **Follow best practices**:
   - Isolate tests
   - Use `TempDir` for temporary files
   - Test both success and failure scenarios
   - Add clear assertions

### Running Tests

```bash
# Run all tests
ctest -C Release

# Run specific test
ctest -C Release -R "ls.ls_basic"

# Run tests with verbose output
ctest -C Release -V
```

### Test Coverage

While formal test coverage metrics are not currently collected, contributors are encouraged to:

- Test all command options and flags
- Test edge cases and error conditions
- Ensure tests pass on both 32-bit and 64-bit systems

## Submitting Changes

### Pull Request Process

1. **Fork the repository**
2. **Create a feature branch**
   ```bash
   git checkout -b feature/my-feature
   ```
3. **Make changes**
4. **Commit changes** with clear commit messages
   ```bash
   git commit -m "[Feature] Add my-feature command"
   ```
5. **Push changes** to your fork
   ```bash
   git push origin feature/my-feature
   ```
6. **Create a pull request**
   - Describe the changes in detail
   - Reference any related issues
   - Ensure all tests pass

### Commit Message Format

Follow this format for commit messages:

```
[Area] Brief description of changes

Detailed explanation of the changes (if needed).

Fixes #issue_number (if applicable)
```

Where `Area` can be:
- `Command`: For command implementations
- `Framework`: For test framework changes
- `Core`: For core functionality changes
- `Docs`: For documentation changes
- `Build`: For build system changes

## Reporting Issues

### Bug Reports

When reporting bugs, please include:

- **Steps to reproduce** the issue
- **Expected behavior**
- **Actual behavior**
- **Environment** (Windows version, Visual Studio version)
- **Error messages** (if any)
- **Screenshots** (if applicable)

### Security Issues

For security issues, please contact the maintainers directly at <2507560089@qq.com> instead of creating a public issue.

## Feature Requests

When requesting new features, please include:

- **Description** of the feature
- **Use case** for the feature
- **Examples** of how the feature would be used
- **References** to similar features in other tools (if applicable)

## Documentation

### Updating Documentation

- **README.md** and **README-zh.md**: Update for new commands and features
- **DOCS/en/**: English documentation
- **DOCS/zh/**: Chinese documentation

### Documentation Guidelines

- Use clear, concise language
- Provide examples where appropriate
- Follow the existing documentation structure
- Keep English and Chinese documentation in sync

## Code of Conduct

### Our Pledge

We as members, contributors, and leaders pledge to make participation in our community a harassment-free experience for everyone, regardless of age, body size, visible or invisible disability, ethnicity, sex characteristics, gender identity and expression, level of experience, education, socio-economic status, nationality, personal appearance, race, religion, or sexual identity and orientation.

### Our Standards

Examples of behavior that contributes to creating a positive environment include:

- Using welcoming and inclusive language
- Being respectful of differing viewpoints and experiences
- Gracefully accepting constructive criticism
- Focusing on what is best for the community
- Showing empathy towards other community members

Examples of unacceptable behavior by participants include:

- The use of sexualized language or imagery and unwelcome sexual attention or advances
- Trolling, insulting/derogatory comments, and personal or political attacks
- Public or private harassment
- Publishing others' private information, such as a physical or electronic address, without explicit permission
- Other conduct which could reasonably be considered inappropriate in a professional setting

### Enforcement Responsibilities

Community leaders are responsible for clarifying and enforcing our standards of acceptable behavior and will take appropriate and fair corrective action in response to any behavior that they deem inappropriate, threatening, offensive, or harmful.

### Scope

This Code of Conduct applies within all community spaces, and also applies when an individual is officially representing the community in public spaces.

### Enforcement

Instances of abusive, harassing, or otherwise unacceptable behavior may be reported by contacting the maintainers at <2507560089@qq.com>. All complaints will be reviewed and investigated and will result in a response that is deemed necessary and appropriate to the circumstances.

## Acknowledgements

We appreciate your contributions to WinuxCmd! Your help makes this project better for everyone.

## Contact

- Maintainer: <2507560089@qq.com>
- GitHub: [@caomengxuan666](https://github.com/caomengxuan666)
- Website: [blog.caomengxuan666.com](https://dl.caomengxuan666.com)
