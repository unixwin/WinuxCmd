/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 *
 *  - File: argv_encoding_unit_test.cpp
 *  - CopyrightYear: 2026
 *
 * Regression coverage for niubash issue #88: non-ASCII operands must survive
 * the process argv boundary as UTF-8 regardless of the system ANSI code page.
 * Pipeline always spawns real child processes via CreateProcessW with a pure
 * UTF-16 command line, so these tests exercise exactly the entry-point
 * boundary described in the issue.
 *
 * Every fixture uses \\u escape sequences instead of raw UTF-8 literals so
 * the test does not depend on the compiler's source-charset interpretation
 * (CI runners compile under ACP 1252, not UTF-8).
 */
#include <filesystem>
#include <string>

#include "framework/winuxtest.h"

namespace {

// "中文目录" as wide chars (U+4E2D U+6587 U+76EE U+5F55).
constexpr wchar_t kChineseDir[] = L"\x4E2D\x6587\x76EE\x5F55";

// "中文目录" as UTF-8 bytes.
constexpr const char *kChineseDirUtf8 =
    "\xE4\xB8\xAD\xE6\x96\x87\xE7\x9B\xAE\xE5\xBD\x95";

constexpr const char *kFileBody = "hello\n";

}  // namespace

TEST(argv, non_ascii_operand_reaches_winuxcmd_as_utf8) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / kChineseDir);
  temp_dir_detail::write_all(tmp.path / kChineseDir / L"a.txt", kFileBody,
                             std::string(kFileBody).size());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  // Mode 1: winuxcmd ls <中文目录> — the operand crosses the argv boundary.
  p.add(L"winuxcmd.exe", {L"ls", kChineseDir});

  TEST_LOG_CMD_LIST("winuxcmd.exe", L"ls", kChineseDir);

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ls operand output", r.stdout_text);
  TEST_LOG("ls operand stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_TRUE(r.stdout_text.find("a.txt") != std::string::npos);
}

TEST(argv, missing_non_ascii_ls_operand_preserves_utf8_diagnostic) {
  TempDir tmp;
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {kChineseDir});
  const auto r = p.run();
  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_NE(r.stderr_text.find(kChineseDirUtf8), std::string::npos);
}

TEST(argv, non_ascii_recursive_ls_header_preserves_utf8) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / kChineseDir);
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-R", kChineseDir});
  const auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find(std::string(kChineseDirUtf8) + ":"),
            std::string::npos);
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(argv, non_ascii_find_root_prints_utf8_paths) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / kChineseDir);
  temp_dir_detail::write_all(tmp.path / kChineseDir / L"a.txt", kFileBody,
                             std::string(kFileBody).size());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  // The Chinese root must be decoded as UTF-8 at the argv boundary AND
  // printed back as UTF-8 (not the system ACP) by path_display.
  p.add(L"winuxcmd.exe", {L"find", kChineseDir, L"-name", L"*.txt"});

  TEST_LOG_CMD_LIST("winuxcmd.exe", L"find", kChineseDir, L"-name", L"*.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("find utf8 output", r.stdout_text);
  TEST_LOG("find utf8 stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  const std::string expected = std::string(kChineseDirUtf8) + "/a.txt";
  EXPECT_TRUE(r.stdout_text.find(expected) != std::string::npos);
}

TEST(argv, non_ascii_grep_recursive_lists_utf8_paths) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / kChineseDir);
  temp_dir_detail::write_all(tmp.path / kChineseDir / L"a.txt", kFileBody,
                             std::string(kFileBody).size());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"winuxcmd.exe", {L"grep", L"-r", L"hello", L"."});

  TEST_LOG_CMD_LIST("winuxcmd.exe", L"grep", L"-r", L"hello", L".");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("grep recursive output", r.stdout_text);
  TEST_LOG("grep recursive stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  const std::string expected =
      std::string("./") + kChineseDirUtf8 + "/a.txt:hello";
  EXPECT_TRUE(r.stdout_text.find(expected) != std::string::npos);
}
