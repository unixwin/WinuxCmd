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
 *  - File: pr_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
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
  TempDir tmp;
  tmp.write("test.txt", "hello\tworld\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pr.exe", {L"-e", L"4", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
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
