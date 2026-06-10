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
 *  - File: cmp_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(cmp, cmp_same_files) {
  TempDir tmp;
  tmp.write("file1.txt", "hello\n");
  tmp.write("file2.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(cmp, cmp_different_files) {
  TempDir tmp;
  tmp.write("file1.txt", "hello\n");
  tmp.write("file2.txt", "world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "file1.txt file2.txt differ: byte 1, line 1\n");
}

TEST(cmp, cmp_quiet_mode) {
  TempDir tmp;
  tmp.write("file1.txt", "hello\n");
  tmp.write("file2.txt", "world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"-s", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(cmp, cmp_last_output_mode_option_wins_to_verbose) {
  TempDir tmp;
  tmp.write("file1.txt", "hello\n");
  tmp.write("file2.txt", "world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"-s", L"-l", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(cmp, cmp_last_output_mode_option_wins_to_quiet) {
  TempDir tmp;
  tmp.write("file1.txt", "hello\n");
  tmp.write("file2.txt", "world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"-l", L"--quiet", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(cmp, cmp_positional_skip1_skips_both_files) {
  TempDir tmp;
  tmp.write("file1.txt", "xhello\n");
  tmp.write("file2.txt", "yhello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"file1.txt", L"file2.txt", L"1"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(cmp, cmp_positional_skip1_and_skip2_skip_independently) {
  TempDir tmp;
  tmp.write("file1.txt", "xxhello\n");
  tmp.write("file2.txt", "yhello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"file1.txt", L"file2.txt", L"2", L"1"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(cmp, cmp_ignore_initial_colon_spec_skips_independently) {
  TempDir tmp;
  tmp.write("file1.txt", "xxhello\n");
  tmp.write("file2.txt", "yhello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"-i", L"2:1", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(cmp, cmp_default_output_counts_byte_from_comparison_start_after_skip) {
  TempDir tmp;
  tmp.write("file1.txt", "xxa\n");
  tmp.write("file2.txt", "yyb\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"file1.txt", L"file2.txt", L"2", L"2"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "file1.txt file2.txt differ: byte 1, line 1\n");
}

TEST(cmp, cmp_verbose_lists_all_differing_bytes) {
  TempDir tmp;
  tmp.write("file1.txt", "abx\ndf\n");
  tmp.write("file2.txt", "aby\neg\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"-l", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "3 170 171\n5 144 145\n6 146 147\n");
}

TEST(cmp, cmp_skip_beyond_shorter_file_reports_eof_without_underflow) {
  TempDir tmp;
  tmp.write("file1.txt", "abc");
  tmp.write("file2.txt", "abcdef");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"file1.txt", L"file2.txt", L"4", L"0"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "cmp: EOF on file1.txt\n");
}

TEST(cmp, cmp_wildcard_expansion_does_not_reinterpret_extra_file_as_skip) {
  TempDir tmp;
  tmp.write("a.txt", "a");
  tmp.write("b.txt", "b");
  tmp.write("c.txt", "c");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"*.txt", L"c.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.find("extra operand") != std::string::npos);
}

TEST(cmp, cmp_missing_operands_report_help_hint) {
  Pipeline p;
  p.add(L"cmp.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "cmp: missing operand\nTry 'cmp --help' for more information.\n");
}

TEST(cmp, cmp_single_operand_reports_help_hint) {
  Pipeline p;
  p.add(L"cmp.exe", {L"a"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "cmp: missing operand after 'a'\nTry 'cmp --help' for more "
      "information.\n");
}

TEST(cmp, cmp_extra_operand_reports_help_hint) {
  Pipeline p;
  p.add(L"cmp.exe", {L"a", L"b", L"1", L"2", L"extra"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "cmp: extra operand 'extra'\nTry 'cmp --help' for more information.\n");
}
