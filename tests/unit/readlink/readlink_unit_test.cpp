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
 *  - File: readlink_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

namespace {

auto normalize_path_text(std::string text) -> std::string {
  std::replace(text.begin(), text.end(), '/', '\\');
  if (text.rfind("\\\\?\\UNC\\", 0) == 0) {
    text = "\\\\" + text.substr(8);
  } else if (text.rfind("\\\\?\\", 0) == 0 || text.rfind("\\??\\", 0) == 0) {
    text.erase(0, 4);
  }
  return text;
}

bool create_symlink_or_skip(const std::filesystem::path& link,
                            const std::filesystem::path& target) {
  DWORD flags = 0;
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

TEST(readlink, readlink_regular_file_fails) {
  TempDir tmp;
  tmp.write("target.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"readlink.exe", {L"target.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(readlink, readlink_verbose_reports_diagnostics) {
  TempDir tmp;
  tmp.write("target.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"readlink.exe", {L"-v", L"target.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_FALSE(r.stderr_text.empty());
}

TEST(readlink, readlink_quiet_suppresses_diagnostics) {
  TempDir tmp;
  tmp.write("target.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"readlink.exe", {L"-q", L"target.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(readlink, readlink_canonicalize_existing) {
  TempDir tmp;
  tmp.mkdir("existing");
  tmp.write("existing/present.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"readlink.exe", {L"-e", L"existing\\..\\existing\\present.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);

  auto expected =
      normalize_path_text((tmp.path / "existing" / "present.txt").string());
  EXPECT_EQ_TEXT(normalize_path_text(r.stdout_text), expected + "\n");
}

TEST(readlink, readlink_canonicalize_missing) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"readlink.exe", {L"-m", L"missing\\branch\\leaf.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);

  auto expected = normalize_path_text(
      (tmp.path / "missing" / "branch" / "leaf.txt").string());
  EXPECT_EQ_TEXT(normalize_path_text(r.stdout_text), expected + "\n");
}

TEST(readlink, readlink_no_newline_suppresses_delimiter) {
  TempDir tmp;
  tmp.write("present.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"readlink.exe", {L"-e", L"-n", L"present.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);

  auto expected = normalize_path_text((tmp.path / "present.txt").string());
  EXPECT_EQ_TEXT(normalize_path_text(r.stdout_text), expected);
}

TEST(readlink, readlink_zero_terminated_uses_nul) {
  TempDir tmp;
  tmp.write("present.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"readlink.exe", {L"-e", L"-z", L"present.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);

  auto expected = normalize_path_text((tmp.path / "present.txt").string());
  EXPECT_BYTES(r.stdout_text, expected + std::string(1, '\0'));
}

TEST(readlink, readlink_no_newline_multiple_operands_warns_and_uses_newlines) {
  TempDir tmp;
  tmp.write("one.txt", "1");
  tmp.write("two.txt", "2");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"readlink.exe", {L"-e", L"-n", L"one.txt", L"two.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stderr_text.empty());
  EXPECT_TRUE(r.stdout_text.ends_with("\n"));
  EXPECT_TRUE(r.stdout_text.find("\n") != r.stdout_text.rfind("\n"));
}

TEST(readlink, readlink_symlink_target_if_available) {
  TempDir tmp;
  tmp.write("target.txt", "hello\n");

  auto link = tmp.path / "link.txt";
  if (!create_symlink_or_skip(link, tmp.path / "target.txt")) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"readlink.exe", {L"link.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);

  auto expected = normalize_path_text((tmp.path / "target.txt").string());
  EXPECT_EQ_TEXT(normalize_path_text(r.stdout_text), expected + "\n");
}
