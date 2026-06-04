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
 *  - File: expand_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(expand, expand_basic) {
  TempDir tmp;
  tmp.write("test.txt", "hello\tworld\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"expand.exe", {L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Tab should be converted to spaces (default 8 spaces)
  EXPECT_TRUE(r.stdout_text.find("hello") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("world") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("\t") == std::string::npos);
}

TEST(expand, expand_custom_tab) {
  TempDir tmp;
  tmp.write("test.txt", "hello\tworld\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"expand.exe", {L"-t", L"4", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("\t") == std::string::npos);
}

TEST(expand, expand_tab_list) {
  Pipeline p;
  p.set_stdin("a\tb\tc\n");
  p.add(L"expand.exe", {L"-t", L"3,5"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a  b c\n");
}

TEST(expand, expand_tab_list_plus_repeat) {
  Pipeline p;
  p.set_stdin("a\tb\tc\n");
  p.add(L"expand.exe", {L"-t", L"3,+4"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a  b   c\n");
}

TEST(expand, expand_tab_list_slash_repeat) {
  Pipeline p;
  p.set_stdin("a\tb\tc\n");
  p.add(L"expand.exe", {L"-t", L"3,/4"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a  b    c\n");
}

TEST(expand, expand_initial_preserves_later_tabs) {
  Pipeline p;
  p.set_stdin(" \ta\tb\n");
  p.add(L"expand.exe", {L"-i"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "        a\tb\n");
}

TEST(expand, expand_backspace_decrements_column) {
  Pipeline p;
  p.set_stdin("ab\b\tc\n");
  p.add(L"expand.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "ab\b       c\n");
}

TEST(expand, expand_tabs_option_keeps_glob_literal) {
  TempDir tmp;
  tmp.write("4.txt", "ignored\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("a\tb\n");
  p.add(L"expand.exe", {L"-t", L"*.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
}

TEST(expand, expand_stdin) {
  Pipeline p;
  p.set_stdin("hello\tworld\n");
  p.add(L"expand.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("\t") == std::string::npos);
}
