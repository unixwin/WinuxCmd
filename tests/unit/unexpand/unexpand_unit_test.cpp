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
 *  - File: unexpand_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(unexpand, unexpand_basic) {
  TempDir tmp;
  tmp.write("test.txt", "hello        world\n");  // 8 spaces between words

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"unexpand.exe", {L"-t", L"8", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Spaces at tab positions should be converted to tabs
  EXPECT_TRUE(r.stdout_text.find("hello") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("world") != std::string::npos);
}

TEST(unexpand, unexpand_stdin) {
  Pipeline p;
  p.set_stdin("hello        world\n");
  p.add(L"unexpand.exe", {L"-t", L"8"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(unexpand, unexpand_default_only_converts_leading_blanks) {
  Pipeline p;
  p.set_stdin("        x        y\n");
  p.add(L"unexpand.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "\tx        y\n");
}

TEST(unexpand, unexpand_tabs_option_implies_all) {
  Pipeline p;
  p.set_stdin("ab  cd\n");
  p.add(L"unexpand.exe", {L"-t", L"4"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "ab\tcd\n");
}

TEST(unexpand, unexpand_first_only_overrides_tabs_all) {
  Pipeline p;
  p.set_stdin("    x    y\n");
  p.add(L"unexpand.exe", {L"-t", L"4", L"--first-only"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "\tx    y\n");
}

TEST(unexpand, unexpand_f_alias_matches_first_only) {
  Pipeline p;
  p.set_stdin("        x    y\n");
  p.add(L"unexpand.exe", {L"-a", L"-f"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "\tx    y\n");
}

TEST(unexpand, unexpand_tab_list) {
  Pipeline p;
  p.set_stdin("a  b\n");
  p.add(L"unexpand.exe", {L"-t", L"3,5"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\tb\n");
}

TEST(unexpand, unexpand_tab_list_slash_repeat) {
  Pipeline p;
  p.set_stdin("a  b    c\n");
  p.add(L"unexpand.exe", {L"-t", L"3,/4"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\tb\tc\n");
}

TEST(unexpand, unexpand_tabs_option_keeps_glob_literal) {
  TempDir tmp;
  tmp.write("4.txt", "ignored\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("a  b\n");
  p.add(L"unexpand.exe", {L"-t", L"*.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
}

TEST(unexpand, unexpand_missing_input_reports_no_such_file) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"unexpand.exe", {L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "unexpand: cannot open 'missing.txt' for reading: No such "
                  "file or directory") != std::string::npos);
}

TEST(unexpand, unexpand_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("indir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"unexpand.exe", {L"indir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(
      r.stderr_text.find("unexpand: cannot open 'indir' for reading: Is a directory") !=
      std::string::npos);
}

TEST(unexpand, unexpand_no_utf8_preserves_utf8_bom_bytes) {
  TempDir tmp;
  tmp.write_bytes("bom.txt", {static_cast<char>(0xEF), static_cast<char>(0xBB),
                              static_cast<char>(0xBF), ' ', ' ', ' ', ' ',
                              ' ', ' ', ' ', ' ',
                              'x', '\n'});

  Pipeline default_mode;
  default_mode.set_cwd(tmp.wpath());
  default_mode.add(L"unexpand.exe", {L"bom.txt"});
  auto default_result = default_mode.run();

  EXPECT_EQ(default_result.exit_code, 0);
  EXPECT_EQ_TEXT(default_result.stdout_text, "\tx\n");

  Pipeline ascii_mode;
  ascii_mode.set_cwd(tmp.wpath());
  ascii_mode.add(L"unexpand.exe", {L"-U", L"-a", L"bom.txt"});
  auto ascii_result = ascii_mode.run();

  EXPECT_EQ(ascii_result.exit_code, 0);
  EXPECT_EQ(ascii_result.stdout_text,
            std::string("\xEF\xBB\xBF\t   x\n", 9));
}
