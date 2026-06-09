/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *  sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 *
 *  - File: chmod_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

namespace {
auto is_readonly(const std::filesystem::path& path) -> bool {
  DWORD attrs = GetFileAttributesW(path.wstring().c_str());
  return attrs != INVALID_FILE_ATTRIBUTES &&
         (attrs & FILE_ATTRIBUTE_READONLY) != 0;
}

auto set_readonly(const std::filesystem::path& path, bool readonly) -> bool {
  DWORD attrs = GetFileAttributesW(path.wstring().c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return false;
  }

  if (readonly) {
    attrs |= FILE_ATTRIBUTE_READONLY;
  } else {
    attrs &= ~FILE_ATTRIBUTE_READONLY;
  }

  return SetFileAttributesW(path.wstring().c_str(), attrs) != 0;
}
}  // namespace

TEST(chmod, chmod_numeric_644) {
  TempDir tmp;
  tmp.write("test.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chmod.exe", {L"644", L"test.txt"});

  TEST_LOG_CMD_LIST("chmod.exe", L"644", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chmod output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(chmod, chmod_numeric_755) {
  TempDir tmp;
  tmp.write("test.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chmod.exe", {L"755", L"test.txt"});

  TEST_LOG_CMD_LIST("chmod.exe", L"755", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chmod output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(chmod, chmod_symbolic_add) {
  TempDir tmp;
  tmp.write("test.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chmod.exe", {L"u+w", L"test.txt"});

  TEST_LOG_CMD_LIST("chmod.exe", L"u+w", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chmod output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(chmod, chmod_symbolic_remove) {
  TempDir tmp;
  tmp.write("test.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chmod.exe", {L"go-w", L"test.txt"});

  TEST_LOG_CMD_LIST("chmod.exe", L"go-w", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chmod output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(chmod, chmod_verbose) {
  TempDir tmp;
  tmp.write("test.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chmod.exe", {L"-v", L"644", L"test.txt"});

  TEST_LOG_CMD_LIST("chmod.exe", L"-v", L"644", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chmod output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(chmod, chmod_recursive) {
  TempDir tmp;
  tmp.write("test.txt", "hello\n");
  std::filesystem::create_directory(tmp.path / "subdir");
  tmp.write("subdir/file.txt", "world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chmod.exe", {L"-R", L"755", L"subdir"});

  TEST_LOG_CMD_LIST("chmod.exe", L"-R", L"755", L"subdir");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chmod output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(chmod, chmod_multiple_files) {
  TempDir tmp;
  tmp.write("file1.txt", "hello\n");
  tmp.write("file2.txt", "world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chmod.exe", {L"644", L"file1.txt", L"file2.txt"});

  TEST_LOG_CMD_LIST("chmod.exe", L"644", L"file1.txt", L"file2.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chmod output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(chmod, chmod_changes) {
  TempDir tmp;
  tmp.write("test.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chmod.exe", {L"-c", L"755", L"test.txt"});

  TEST_LOG_CMD_LIST("chmod.exe", L"-c", L"755", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chmod output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(chmod, chmod_wildcard_multiple_files) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\n");
  tmp.write("b.txt", "beta\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chmod.exe", {L"644", L"*.txt"});

  TEST_LOG_CMD_LIST("chmod.exe", L"644", L"*.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chmod wildcard output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(chmod, chmod_preserve_root_recursive_root_failsafe) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chmod.exe", {L"-R", L"--preserve-root", L"755", L"/"});

  TEST_LOG_CMD_LIST("chmod.exe", L"-R", L"--preserve-root", L"755", L"/");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chmod preserve-root stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ(
      r.stderr_text,
      "chmod: it is dangerous to operate recursively on '/'\n"
      "chmod: use --no-preserve-root to override this failsafe\n");
}

TEST(chmod, chmod_reference_copies_reference_mode_approximation) {
  TempDir tmp;
  tmp.write("reference.txt", "ref\n");
  tmp.write("target.txt", "target\n");

  EXPECT_TRUE(set_readonly(tmp.path / "reference.txt", true));
  EXPECT_TRUE(set_readonly(tmp.path / "target.txt", false));

  EXPECT_TRUE(is_readonly(tmp.path / "reference.txt"));
  EXPECT_FALSE(is_readonly(tmp.path / "target.txt"));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chmod.exe", {L"--reference=reference.txt", L"target.txt"});

  TEST_LOG_CMD_LIST("chmod.exe", L"--reference=reference.txt", L"target.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chmod reference stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(is_readonly(tmp.path / "target.txt"));
}

TEST(chmod, chmod_reference_missing_reference_reports_gnu_style_error) {
  TempDir tmp;
  tmp.write("target.txt", "target\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chmod.exe", {L"--reference=missing.txt", L"target.txt"});

  TEST_LOG_CMD_LIST("chmod.exe", L"--reference=missing.txt", L"target.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chmod missing reference stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ(
      r.stderr_text,
      "chmod: failed to get attributes of 'missing.txt': No such file or "
      "directory\n");
}

TEST(chmod, chmod_invalid_mode_reports_gnu_style_error_before_file_access) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chmod.exe", {L"u+gr", L"missing.txt"});

  TEST_LOG_CMD_LIST("chmod.exe", L"u+gr", L"missing.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chmod invalid mode stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ(
      r.stderr_text,
      "chmod: invalid mode: 'u+gr'\n"
      "Try 'chmod --help' for more information.\n");
}

TEST(chmod, chmod_gnu_negative_mode_first_operand_is_accepted) {
  TempDir tmp;
  tmp.write("test.txt", "hello\n");
  EXPECT_TRUE(set_readonly(tmp.path / "test.txt", false));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chmod.exe", {L"-w", L"test.txt"});

  TEST_LOG_CMD_LIST("chmod.exe", L"-w", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chmod -w stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(is_readonly(tmp.path / "test.txt"));
}

TEST(chmod, chmod_gnu_negative_mode_after_file_is_accepted) {
  TempDir tmp;
  tmp.write("test.txt", "hello\n");
  EXPECT_TRUE(set_readonly(tmp.path / "test.txt", false));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chmod.exe", {L"test.txt", L"-w"});

  TEST_LOG_CMD_LIST("chmod.exe", L"test.txt", L"-w");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chmod file -w stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(is_readonly(tmp.path / "test.txt"));
}

TEST(chmod, chmod_gnu_repeated_negative_modes_are_accepted) {
  TempDir tmp;
  tmp.write("test.txt", "hello\n");
  EXPECT_TRUE(set_readonly(tmp.path / "test.txt", false));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chmod.exe", {L"-w", L"-w", L"test.txt"});

  TEST_LOG_CMD_LIST("chmod.exe", L"-w", L"-w", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chmod -w -w stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(is_readonly(tmp.path / "test.txt"));
}

TEST(chmod, chmod_gnu_negative_mode_before_double_dash_is_accepted) {
  TempDir tmp;
  tmp.write("test.txt", "hello\n");
  EXPECT_TRUE(set_readonly(tmp.path / "test.txt", false));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chmod.exe", {L"-w", L"--", L"test.txt"});

  TEST_LOG_CMD_LIST("chmod.exe", L"-w", L"--", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chmod -w -- file stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(is_readonly(tmp.path / "test.txt"));
}
