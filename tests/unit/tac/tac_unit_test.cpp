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
 *  - File: tac_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(tac, tac_basic_file) {
  TempDir tmp;
  tmp.write("test.txt", "line1\nline2\nline3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tac.exe", {L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // tac should print lines in reverse order
  EXPECT_TRUE(r.stdout_text.find("line3") < r.stdout_text.find("line2"));
  EXPECT_TRUE(r.stdout_text.find("line2") < r.stdout_text.find("line1"));
}

TEST(tac, tac_reverses_each_file_independently) {
  TempDir tmp;
  tmp.write("a.txt", "a1\na2\n");
  tmp.write("b.txt", "b1\nb2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tac.exe", {L"a.txt", L"b.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a2\na1\nb2\nb1\n");
}

TEST(tac, tac_custom_separator) {
  TempDir tmp;
  tmp.write("colon.txt", "one:two:three:");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tac.exe", {L"-s", L":", L"colon.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "three:two:one:");
}

TEST(tac, tac_before_attaches_separator_to_next_record) {
  TempDir tmp;
  tmp.write("colon.txt", "one:two:three:");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tac.exe", {L"-b", L"-s", L":", L"colon.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "::three:twoone");
}

TEST(tac, tac_empty_separator_uses_nul) {
  TempDir tmp;
  tmp.write_bytes("nul.bin", {'a', '\0', 'b', '\0', 'c', '\0'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tac.exe", {L"-s", L"", L"nul.bin"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, std::string("c\0b\0a\0", 6));
}

TEST(tac, tac_regex_separator) {
  TempDir tmp;
  tmp.write("digits.txt", "aa11bb22cc33");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tac.exe", {L"-r", L"-s", L"[0-9]+", L"digits.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "cc33bb22aa11");
}

TEST(tac, tac_stdin) {
  Pipeline p;
  p.set_stdin("line1\nline2\nline3\n");
  p.add(L"tac.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("line3") < r.stdout_text.find("line2"));
  EXPECT_TRUE(r.stdout_text.find("line2") < r.stdout_text.find("line1"));
}

TEST(tac, tac_single_line) {
  TempDir tmp;
  tmp.write("single.txt", "only one line\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tac.exe", {L"single.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "only one line\n");
}
