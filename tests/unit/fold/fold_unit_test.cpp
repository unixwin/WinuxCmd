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
 *  - File: fold_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(fold, fold_basic) {
  TempDir tmp;
  tmp.write(
      "test.txt",
      "This is a very long line that should be wrapped at a certain width\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"fold.exe", {L"-w", L"20", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Line should be wrapped
  EXPECT_TRUE(r.stdout_text.find("This is a very long") != std::string::npos);
}

TEST(fold, fold_stdin) {
  Pipeline p;
  p.set_stdin("This is a very long line that should be wrapped\n");
  p.add(L"fold.exe", {L"-w", L"20"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(fold, fold_short_line) {
  TempDir tmp;
  tmp.write("test.txt", "short\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"fold.exe", {L"-w", L"20", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "short\n");
}

TEST(fold, fold_preserves_missing_final_newline) {
  Pipeline p;
  p.set_stdin("abcdef");
  p.add(L"fold.exe", {L"-w", L"3"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "abc\ndef");
}

TEST(fold, fold_tabs_count_to_next_tab_stop) {
  Pipeline p;
  p.set_stdin("a\tb\n");
  p.add(L"fold.exe", {L"-w", L"8"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\t\nb\n");
}

TEST(fold, fold_bytes_counts_control_bytes) {
  Pipeline p;
  p.set_stdin("ab\bcd\n");
  p.add(L"fold.exe", {L"-b", L"-w", L"3"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "ab\b\ncd\n");
}

TEST(fold, fold_columns_account_for_backspace) {
  Pipeline p;
  p.set_stdin("ab\bcd\n");
  p.add(L"fold.exe", {L"-w", L"3"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "ab\bcd\n");
}

TEST(fold, fold_characters_option) {
  Pipeline p;
  p.set_stdin("abcd\n");
  p.add(L"fold.exe", {L"-c", L"-w", L"2"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "ab\ncd\n");
}

TEST(fold, fold_rejects_trailing_junk_width) {
  Pipeline p;
  p.set_stdin("abcd\n");
  p.add(L"fold.exe", {L"-w", L"2x"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
}

TEST(fold, fold_spaces_breaks_at_last_blank) {
  Pipeline p;
  p.set_stdin("aa bb cc\n");
  p.add(L"fold.exe", {L"-s", L"-w", L"5"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "aa \nbb cc\n");
}
