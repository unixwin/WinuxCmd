/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the “Software”), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: uniq_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(uniq, uniq_basic_adjacent_behavior) {
  TempDir tmp;
  tmp.write("a.txt", "a\na\nb\na\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"uniq.exe", {L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\nb\na\n");
}

TEST(uniq, uniq_count) {
  TempDir tmp;
  tmp.write("a.txt", "x\nx\ny\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"uniq.exe", {L"-c", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("2 x") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("1 y") != std::string::npos);
}

TEST(uniq, uniq_repeated_and_unique_filters) {
  TempDir tmp;
  tmp.write("a.txt", "a\na\nb\nc\nc\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"uniq.exe", {L"-d", L"a.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);
  EXPECT_EQ_TEXT(r1.stdout_text, "a\nc\n");

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"uniq.exe", {L"-u", L"a.txt"});
  auto r2 = p2.run();
  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_EQ_TEXT(r2.stdout_text, "b\n");
}

TEST(uniq, uniq_ignore_case_skip_fields_chars) {
  TempDir tmp;
  tmp.write("a.txt", "id1 Same\nid2 same\nid3 diff\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"uniq.exe", {L"-i", L"-f", L"1", L"-s", L"1", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "id1 Same\nid3 diff\n");
}

TEST(uniq, uniq_all_repeated) {
  TempDir tmp;
  tmp.write("a.txt", "a\na\nb\nc\nc\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"uniq.exe", {L"-D", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\na\nc\nc\n");
}
