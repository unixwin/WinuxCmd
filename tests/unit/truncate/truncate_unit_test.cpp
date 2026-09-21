// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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

TEST(truncate, truncate_creates_utf8_filename) {
  TempDir tmp;
  const std::wstring name = L"\x6D4B\x8BD5.txt";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"truncate.exe", {L"-s", L"3", name});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(std::filesystem::file_size(tmp.path / name), 3);
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

TEST(truncate, truncate_missing_trailing_separator_is_not_created) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"truncate.exe", {L"-s", L"100", L"missing.txt/"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "missing.txt"));
  EXPECT_TRUE(r.stderr_text.find("truncate: cannot resize 'missing.txt/'") !=
              std::string::npos);
}

TEST(truncate, truncate_file_with_trailing_separator_reports_not_directory) {
  TempDir tmp;
  tmp.write("file.txt", "abcdef");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"truncate.exe", {L"-s", L"1", L"file.txt/"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ(tmp.read("file.txt"), "abcdef");
  EXPECT_TRUE(r.stderr_text.find("truncate: cannot resize 'file.txt/': Not a "
                                 "directory") != std::string::npos);
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

// [GNU] --reference combines with a *relative* --size applied to the
// reference file's size: ref=10, -6 -> 4 (truncate.c, uutils #12963).
TEST(truncate, truncate_size_overrides_reference) {
  TempDir tmp;
  tmp.write("reference.txt", "1234567890");
  tmp.write("test.txt", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"truncate.exe",
        {L"--reference", L"reference.txt", L"--size", L"-6", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(std::filesystem::file_size(tmp.path / "test.txt"), 4);
}

// [GNU] --reference with an *absolute* --size is a usage error.
TEST(truncate, truncate_absolute_size_with_reference_is_error) {
  TempDir tmp;
  tmp.write("reference.txt", "1234567890");
  tmp.write("test.txt", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"truncate.exe",
        {L"--reference", L"reference.txt", L"--size", L"4", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "you must specify a relative '--size' with '--reference'") !=
              std::string::npos);
  EXPECT_EQ(std::filesystem::file_size(tmp.path / "test.txt"), 3);
}

// [GNU] a missing reference reports "cannot stat 'R': No such file or
// directory" and exits 1 before touching any file.
TEST(truncate, truncate_missing_reference_cannot_stat) {
  TempDir tmp;
  tmp.write("test.txt", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"truncate.exe",
        {L"-r", L"nonexistent.ref", L"-s", L"+2", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("cannot stat 'nonexistent.ref': No such file "
                                 "or directory") != std::string::npos);
  EXPECT_EQ(std::filesystem::file_size(tmp.path / "test.txt"), 3);
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
