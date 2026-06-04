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
 *  - File: csplit_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(csplit, csplit_basic) {
  TempDir tmp;
  tmp.write("test.txt", "line1\nseparator\nline2\nseparator\nline3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"csplit.exe", {L"test.txt", L"/separator/"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should create split files
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "xx00") ||
              std::filesystem::exists(tmp.path / "xx00.txt"));
}

TEST(csplit, csplit_pattern) {
  TempDir tmp;
  tmp.write("test.txt", "aaa\nbbb\nccc\nddd\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"csplit.exe", {L"test.txt", L"3"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "xx00") ||
              std::filesystem::exists(tmp.path / "xx00.txt"));
  EXPECT_EQ_TEXT(tmp.read("xx00"), "aaa\nbbb\n");
  EXPECT_EQ_TEXT(tmp.read("xx01"), "ccc\nddd\n");
  EXPECT_EQ_TEXT(r.stdout_text, "8\n8\n");
}

TEST(csplit, csplit_regex_repeat_with_offset) {
  TempDir tmp;
  tmp.write("test.txt", "h1\nA\nh2\nB\nh3\nC\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"csplit.exe", {L"test.txt", L"/^h/+1", L"{2}"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("xx00"), "h1\n");
  EXPECT_EQ_TEXT(tmp.read("xx01"), "A\nh2\n");
  EXPECT_EQ_TEXT(tmp.read("xx02"), "B\nh3\n");
  EXPECT_EQ_TEXT(tmp.read("xx03"), "C\n");
  EXPECT_EQ_TEXT(r.stdout_text, "3\n5\n5\n2\n");
}

TEST(csplit, csplit_skip_pattern_discards_segment) {
  TempDir tmp;
  tmp.write("test.txt", "drop\nMARK\nkeep\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"csplit.exe", {L"test.txt", L"%MARK%+1"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("xx00"), "keep\n");
  EXPECT_EQ_TEXT(r.stdout_text, "5\n");
}

TEST(csplit, csplit_suppress_matched_omits_delimiter_line) {
  TempDir tmp;
  tmp.write("test.txt", "before\nMARK\nafter\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"csplit.exe", {L"--suppress-matched", L"test.txt", L"/MARK/"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("xx00"), "before\n");
  EXPECT_EQ_TEXT(tmp.read("xx01"), "after\n");
  EXPECT_EQ_TEXT(r.stdout_text, "7\n6\n");
}

TEST(csplit, csplit_quiet_and_elide_empty_files) {
  TempDir tmp;
  tmp.write("test.txt", "MARK\nbody\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"csplit.exe", {L"-s", L"-z", L"test.txt", L"/MARK/"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "xx01"));
  EXPECT_EQ_TEXT(tmp.read("xx00"), "MARK\nbody\n");
  EXPECT_EQ_TEXT(r.stdout_text, "");
}

TEST(csplit, csplit_wildcard_input_rejects_ambiguous_match) {
  TempDir tmp;
  tmp.write("a.txt", "line1\nseparator\n");
  tmp.write("b.txt", "line2\nseparator\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"csplit.exe", {L"*.txt", L"/separator/"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("exactly one file") != std::string::npos ||
              r.stderr_text.find("exactly one file") != std::string::npos);
}
