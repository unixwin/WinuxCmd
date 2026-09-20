// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(pr, pr_basic) {
  TempDir tmp;
  tmp.write("test.txt", "line1\nline2\nline3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pr.exe", {L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should format output for printing
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(pr, pr_stdin) {
  Pipeline p;
  p.set_stdin("line1\nline2\n");
  p.add(L"pr.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(pr, pr_page_length) {
  TempDir tmp;
  tmp.write("test.txt", "line1\nline2\nline3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pr.exe", {L"-l", L"10", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(pr, pr_columns) {
  TempDir tmp;
  tmp.write("test.txt", "a\nb\nc\nd\ne\nf\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pr.exe", {L"-2", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(pr, pr_double_space) {
  TempDir tmp;
  tmp.write("test.txt", "line1\nline2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pr.exe", {L"-d", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(pr, pr_header) {
  TempDir tmp;
  tmp.write("test.txt", "line1\nline2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pr.exe", {L"-h", L"My Header", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(pr, pr_no_header) {
  TempDir tmp;
  tmp.write("test.txt", "line1\nline2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pr.exe", {L"-t", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // -t should suppress header/trailer
}

TEST(pr, pr_merge_files) {
  TempDir tmp;
  tmp.write("a.txt", "line1\nline2\n");
  tmp.write("b.txt", "line3\nline4\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pr.exe", {L"-m", L"a.txt", L"b.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(pr, pr_line_numbers) {
  TempDir tmp;
  tmp.write("test.txt", "line1\nline2\nline3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pr.exe", {L"-n", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(pr, pr_page_range) {
  TempDir tmp;
  tmp.write("test.txt", "line1\nline2\nline3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pr.exe", {L"+1:2", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(pr, pr_column_width) {
  TempDir tmp;
  tmp.write("test.txt", "hello\tworld\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pr.exe", {L"-w", L"20", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(pr, pr_expand_tabs) {
  // GNU: -e takes an optional attached arg (-e[CHAR[WIDTH]]); a separate
  // "4" operand would be treated as an input file.  A tab after column 5
  // expands to the next multiple of 4: "hello   world".
  TempDir tmp;
  tmp.write("test.txt", "hello\tworld\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pr.exe", {L"-e4", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("hello   world"), std::string::npos);
}

TEST(pr, pr_form_feed) {
  TempDir tmp;
  tmp.write("test.txt", "page1\n\f\npage2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pr.exe", {L"-f", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(pr, pr_multi_file_paginates_independently) {
  // [GNU] Each file is paginated on its own: its own header name and its
  // own Page 1..N sequence (pr.c calls print_files per input file).
  TempDir tmp;
  tmp.write("a.txt", "a1\na2\n");
  tmp.write("b.txt", "b1\nb2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pr.exe", {L"a.txt", L"b.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  const std::string& out = r.stdout_text;
  // Two independent page-1 headers.
  auto first_page = out.find("Page 1");
  EXPECT_TRUE(first_page != std::string::npos);
  EXPECT_TRUE(out.find("Page 1", first_page + 1) != std::string::npos);
  // Each header names its own file.
  EXPECT_TRUE(out.find("a.txt") != std::string::npos);
  EXPECT_TRUE(out.find("b.txt") != std::string::npos);
  // The second file starts a fresh page after the first file's block.
  EXPECT_TRUE(out.find("a1") < out.find("b.txt"));
  EXPECT_TRUE(out.find("b.txt") < out.find("b1"));
  // No Page 2: each file fits on one page.
  EXPECT_TRUE(out.find("Page 2") == std::string::npos);
}

TEST(pr, pr_missing_file_fails_but_continues) {
  // [GNU] A failed open prints a diagnostic and makes pr exit nonzero
  // (failed_opens), but later files are still printed (pr.c main_exit).
  TempDir tmp;
  tmp.write("ok.txt", "content\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pr.exe", {L"missing.txt", L"ok.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_FALSE(r.stderr_text.empty());
  EXPECT_TRUE(r.stdout_text.find("content") != std::string::npos);
}

TEST(pr, pr_newline_mode_trims_trailing_cr_from_crlf_records) {
  TempDir tmp;
  tmp.write_bytes("test.txt", {'l', 'i', 'n', 'e', '1', '\r', '\n', 'l', 'i',
                               'n', 'e', '2', '\r', '\n'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pr.exe", {L"-t", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text.find('\r'), std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("line1\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("line2\n") != std::string::npos);
}
