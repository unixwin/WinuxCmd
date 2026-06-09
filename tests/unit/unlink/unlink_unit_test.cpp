/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software", to
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
 *  - File: unlink_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

namespace {

bool create_directory_symlink_or_skip(const std::filesystem::path& link,
                                      const std::filesystem::path& target) {
  DWORD flags = SYMBOLIC_LINK_FLAG_DIRECTORY;
#ifdef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
  flags |= SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
#endif

  if (CreateSymbolicLinkW(link.wstring().c_str(), target.wstring().c_str(),
                          flags)) {
    return true;
  }

  std::cout << "  SKIPPED (CreateSymbolicLinkW failed with error "
            << GetLastError() << ")\n";
  return false;
}

}  // namespace

TEST(unlink, unlink_basic) {
  TempDir tmp;
  tmp.write("test.txt", "hello world");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"unlink.exe", {L"test.txt"});

  TEST_LOG_CMD_LIST("unlink.exe", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(unlink, unlink_missing_operand) {
  Pipeline p;
  p.add(L"unlink.exe", {});

  TEST_LOG_CMD_LIST("unlink.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("unlink stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "unlink: missing operand\nTry 'unlink --help' for more information.\n");
}

TEST(unlink, unlink_unknown_option_reports_gnu_style_parse_error) {
  Pipeline p;
  p.add(L"unlink.exe", {L"--bogus"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "unlink: unrecognized option '--bogus'\n"
      "Try 'unlink --help' for more information.\n");
}

TEST(unlink, unlink_version_succeeds) {
  Pipeline p;
  p.add(L"unlink.exe", {L"--version"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("unlink (WinuxCmd)"), std::string::npos);
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(unlink, unlink_rejects_multiple_operands) {
  TempDir tmp;
  tmp.write("one.txt", "one");
  tmp.write("two.txt", "two");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"unlink.exe", {L"one.txt", L"two.txt"});

  TEST_LOG_CMD_LIST("unlink.exe", L"one.txt", L"two.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("unlink stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "unlink: extra operand 'two.txt'\n"
      "Try 'unlink --help' for more information.\n");
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "one.txt"));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "two.txt"));
}

TEST(unlink, unlink_nonexistent_reports_gnu_style_error) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"unlink.exe", {L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "unlink: cannot unlink 'missing.txt': No such file or "
                 "directory\n");
}

TEST(unlink, unlink_directory_reports_directory_error) {
  TempDir tmp;
  tmp.mkdir("dir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"unlink.exe", {L"dir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text, "unlink: cannot unlink 'dir': Is a directory\n");
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "dir"));
}

TEST(unlink, unlink_directory_symlink_removes_link_only_if_available) {
  TempDir tmp;
  tmp.mkdir("target-dir");
  tmp.write("target-dir\\payload.txt", "payload");

  auto link = tmp.path / "dir-link";
  if (!create_directory_symlink_or_skip(link, tmp.path / "target-dir")) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"unlink.exe", {L"dir-link"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_FALSE(std::filesystem::exists(link));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "target-dir"));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "target-dir" / "payload.txt"));
}

TEST(unlink, unlink_wildcard_multiple_matches_reports_extra_operand_help_hint) {
  TempDir tmp;
  tmp.write("a.txt", "a");
  tmp.write("b.txt", "b");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"unlink.exe", {L"*.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "unlink: extra operand 'b.txt'\n"
      "Try 'unlink --help' for more information.\n");
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "a.txt"));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "b.txt"));
}

TEST(unlink, unlink_unmatched_wildcard_falls_back_to_literal_path) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"unlink.exe", {L"*.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "unlink: cannot unlink '*.txt': No such file or directory\n");
}
