# Testing Framework

## Overview

WinuxCmd includes a custom end-to-end (E2E) testing framework designed specifically for testing command-line tools. This framework provides a GTEST-like syntax while integrating with CTEST for seamless test execution.

## Key Features

- **GTEST-like syntax**: Familiar TEST() and EXPECT_* macros
- **CTEST integration**: Seamless integration with CMake's test runner
- **End-to-end testing**: Tests execute actual command-line tools
- **Cross-platform compatibility**: Works on Windows and Linux
- **Easy test creation**: Intuitive APIs for common test scenarios
- **Temporary directory management**: Automatic cleanup of test files

## Test Framework Components

### 1. TempDir

The `TempDir` class provides a temporary directory for test operations. It automatically creates a unique directory and cleans it up after the test completes.

```cpp
// Example usage
TEST(cp, cp_basic) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");
  
  // Test code that uses tmp.path or tmp.wpath()
}
```

### 2. Pipeline

The `Pipeline` class allows you to execute commands and capture their output.

```cpp
// Example usage
Pipeline p;
p.set_cwd(tmp.wpath());
p.add(L"cp.exe", {L"file1.txt", L"file2.txt"});
auto r = p.run();

// Check results
EXPECT_EQ(r.exit_code, 0);
EXPECT_TRUE(r.stdout_text.empty());
EXPECT_TRUE(r.stderr_text.empty());
```

### 3. Test Macros

#### TEST Macro

Defines a test case with a test suite name and test name.

```cpp
TEST(suite_name, test_name) {
  // Test code
}
```

#### EXPECT_* Macros

- `EXPECT_TRUE(condition)`: Checks if condition is true
- `EXPECT_EQ(a, b)`: Checks if values are equal (supports strings with detailed comparison)
- `EXPECT_EQ_TEXT(a, b)`: Checks if text content is equal (normalizes line endings)
- `EXPECT_BYTES(a, b)`: Checks if binary data is equal
- `EXPECT_EXIT_CODE(r, code)`: Checks if process exit code matches

## Creating a Test

### 1. Create Test File

Create a test file in the `tests/unit/{command}/` directory, e.g., `ls_unit_test.cpp`.

### 2. Include Framework Headers

```cpp
#include "tests/framework/pipeline.h"
#include "tests/framework/temp_dir.h"
```

### 3. Write Test Cases

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
  // Check for color escape codes
  EXPECT_TRUE(r.stdout_text.find("\x1b[") != std::string::npos);
}
```

### 4. Build and Run Tests

```bash
# Build tests
cmake --build . --config Release

# Run tests
ctest -C Release

# Run specific test
ctest -C Release -R "ls.ls_basic"
```

## Best Practices

1. **Isolation**: Each test should be independent and not affect other tests
2. **Cleanup**: Use `TempDir` to ensure test files are cleaned up
3. **Assertions**: Use appropriate EXPECT_* macros for clear error messages
4. **Coverage**: Test both success and failure scenarios
5. **Documentation**: Add comments to explain test intent

## Test Framework Architecture

### Directory Structure

```
tests/
├── framework/          # Test framework implementation
├── unit/               # Unit tests for each command
│   ├── cat/            # Tests for cat command
│   ├── cp/             # Tests for cp command
│   ├── ls/             # Tests for ls command
│   └── ...
└── CMakeLists.txt      # CTest configuration
```

### Framework Files

- `wctest.h`: Main test framework header
- `pipeline.h/cpp`: Command execution pipeline
- `temp_dir.h`: Temporary directory management
- `paths.h`: Cross-platform path utilities
- `process_win32.h/cpp`: Windows process management

## Troubleshooting

### Test Failures

1. **Check test output**: Look at the test log for detailed error messages
2. **Run specific test**: Use `ctest -R "test_name"` to run a single test
3. **Enable verbose output**: Use `ctest -V` for more detailed output
4. **Check temporary files**: Temporarily disable cleanup to inspect test files

### Common Issues

- **Path issues**: Use `tmp.wpath()` for Windows paths, `tmp.path` for UTF-8 paths
- **Command not found**: Ensure the command under test is built and in the PATH
- **Permission issues**: Run tests with appropriate permissions
- **Output capturing**: Some commands may bypass stdout/stderr capture

## Extending the Framework

To add new functionality to the test framework:

1. **Modify framework files**: Update files in `tests/framework/`
2. **Add new utilities**: Create helper functions in `tests_utils.h`
3. **Extend Pipeline**: Add new methods to the `Pipeline` class
4. **Add new macros**: Define new test macros in `wctest.h`

## Example Test Cases

### Basic Command Test

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

### Test with Options

```cpp
TEST(cp, cp_recursive) {
  TempDir tmp;
  tmp.write("src/file.txt", "content");
  
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"-r", L"src", L"dest"});
  
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  
  // Verify the file was copied
  std::ifstream dest_file(tmp.path + "/dest/file.txt");
  std::string content((std::istreambuf_iterator<char>(dest_file)),
                     std::istreambuf_iterator<char>());
  EXPECT_EQ(content, "content");
}
```

## Conclusion

The WinuxCmd test framework provides a powerful and intuitive way to test command-line tools. Its GTEST-like syntax and CTEST integration make it easy to write and run tests, while its end-to-end approach ensures that commands behave as expected in real-world scenarios.

By following the best practices outlined in this document, you can create comprehensive test suites that ensure the quality and reliability of WinuxCmd commands.
