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
 *  - File: link_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(link, link_basic) {
  TempDir tmp;
  tmp.write("original.txt", "hello world");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"link.exe", {L"original.txt", L"link.txt"});

  TEST_LOG_CMD_LIST("link.exe", L"original.txt", L"link.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(link, link_missing_operand) {
  Pipeline p;
  p.add(L"link.exe", {});

  TEST_LOG_CMD_LIST("link.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("link stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text, "link: 2 values required\n");
}

TEST(link, link_extra_operand) {
  Pipeline p;
  p.add(L"link.exe", {L"a", L"b", L"c"});

  TEST_LOG_CMD_LIST("link.exe", L"a", L"b", L"c");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("link stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text, "link: 2 values required\n");
}

TEST(link, link_unknown_option_reports_gnu_style_parse_error) {
  Pipeline p;
  p.add(L"link.exe", {L"--bogus", L"a", L"b"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "link: unrecognized option '--bogus'\n"
      "Try 'link --help' for more information.\n");
}

TEST(link, link_version_succeeds) {
  Pipeline p;
  p.add(L"link.exe", {L"--version"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("link (WinuxCmd)"), std::string::npos);
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(link, link_nonexistent_source_reports_gnu_style_error) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"link.exe", {L"missing.txt", L"copy.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "link: cannot create link 'copy.txt' to 'missing.txt': No such file or "
      "directory\n");
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "copy.txt"));
}

TEST(link, link_same_missing_path_reports_gnu_style_error) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"link.exe", {L"same.txt", L"same.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "link: cannot create link 'same.txt' to 'same.txt': No such file or "
      "directory\n");
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "same.txt"));
}

TEST(link, link_existing_destination_reports_gnu_style_error) {
  TempDir tmp;
  tmp.write("source.txt", "hello world");
  tmp.write("linked.txt", "existing");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"link.exe", {L"source.txt", L"linked.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "link: cannot create link 'linked.txt' to 'source.txt': File exists\n");
  EXPECT_EQ_TEXT(tmp.read("linked.txt"), "existing");
}

TEST(link, link_wildcard_source_single_match_is_expanded) {
  TempDir tmp;
  tmp.write("source.txt", "hello world");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"link.exe", {L"*.txt", L"linked.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "linked.txt"));
  EXPECT_EQ_TEXT(tmp.read("linked.txt"), "hello world");
}

TEST(link, link_wildcard_source_multiple_matches_are_rejected) {
  TempDir tmp;
  tmp.write("a.txt", "a");
  tmp.write("b.txt", "b");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"link.exe", {L"*.txt", L"linked.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text, "link: 2 values required\n");
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "linked.txt"));
}

TEST(link, link_wildcard_source_zero_match_falls_back_to_literal_path) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"link.exe", {L"*.txt", L"linked.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "link: cannot create link 'linked.txt' to '*.txt': No such file or "
      "directory\n");
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "linked.txt"));
}
