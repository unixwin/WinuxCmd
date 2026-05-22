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
 *  - File: truncate_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(truncate, truncate_create) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"truncate.exe", {L"-s", L"100", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "test.txt"));
}

TEST(truncate, truncate_shrink) {
  TempDir tmp;
  tmp.write("test.txt", "hello world, this is a long string");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"truncate.exe", {L"-s", L"5", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "test.txt"));
  std::string content = tmp.read("test.txt");
  EXPECT_EQ(content, "hello");
}

TEST(truncate, truncate_empty) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"truncate.exe", {L"-s", L"0", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "test.txt"));
  std::string content = tmp.read("test.txt");
  EXPECT_TRUE(content.empty());
}

TEST(truncate, truncate_no_create_skips_missing_file) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"truncate.exe", {L"-c", L"-s", L"100", L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "missing.txt"));
}

TEST(truncate, truncate_relative_extend) {
  TempDir tmp;
  tmp.write("test.txt", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"truncate.exe", {L"-s", L"+2", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(std::filesystem::file_size(tmp.path / "test.txt"), 5);
  EXPECT_EQ(tmp.read("test.txt").substr(0, 3), "abc");
}

TEST(truncate, truncate_round_up) {
  TempDir tmp;
  tmp.write("test.txt", "abcde");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"truncate.exe", {L"-s", L"%4", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(std::filesystem::file_size(tmp.path / "test.txt"), 8);
}

TEST(truncate, truncate_size_overrides_reference) {
  TempDir tmp;
  tmp.write("reference.txt", "1234567890");
  tmp.write("test.txt", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"truncate.exe",
        {L"--reference", L"reference.txt", L"--size", L"4", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(std::filesystem::file_size(tmp.path / "test.txt"), 4);
}

TEST(truncate, truncate_reference_operand_is_literal_not_globbed) {
  TempDir tmp;
  tmp.write("ref-a.txt", "12345");
  tmp.write("test.txt", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"truncate.exe", {L"--reference", L"ref-*.txt", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ(std::filesystem::file_size(tmp.path / "test.txt"), 3);
}

TEST(truncate, truncate_file_operand_glob_expands) {
  TempDir tmp;
  tmp.write("a.txt", "abc");
  tmp.write("b.txt", "defg");
  tmp.write("c.log", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"truncate.exe", {L"-s", L"1", L"*.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(std::filesystem::file_size(tmp.path / "a.txt"), 1);
  EXPECT_EQ(std::filesystem::file_size(tmp.path / "b.txt"), 1);
  EXPECT_EQ(std::filesystem::file_size(tmp.path / "c.log"), 5);
}
