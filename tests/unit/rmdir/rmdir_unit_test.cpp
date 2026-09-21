// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(rmdir, rmdir_basic_empty_directory) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "empty");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rmdir.exe", {L"empty"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(!std::filesystem::exists(tmp.path / "empty"));
}

TEST(rmdir, rmdir_non_empty_fails) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "dir");
  tmp.write("dir/file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rmdir.exe", {L"dir"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "dir"));
}

TEST(rmdir, rmdir_file_operand_reports_not_a_directory) {
  TempDir tmp;
  tmp.write("file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rmdir.exe", {L"file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "rmdir: failed to remove 'file.txt': Not a directory\n");
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "file.txt"));
}

TEST(rmdir, rmdir_ignore_non_empty) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "dir");
  tmp.write("dir/file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rmdir.exe", {L"--ignore-fail-on-non-empty", L"dir"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "dir"));
}

TEST(rmdir, rmdir_parents_option) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "a" / "b" / "c");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rmdir.exe", {L"-p", L"a/b/c"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(!std::filesystem::exists(tmp.path / "a"));
}

TEST(rmdir, rmdir_parents_reports_non_empty_ancestor_failure) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "a" / "b" / "c");
  tmp.write("a\\keep.txt", "keep");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rmdir.exe", {L"-p", L"a/b/c"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "rmdir: failed to remove 'a': Directory not empty\n");
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "a"));
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "a" / "b"));
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "a" / "b" / "c"));
}

TEST(rmdir, rmdir_missing_operand_reports_help_hint) {
  Pipeline p;
  p.add(L"rmdir.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "rmdir: missing operand\n"
                 "Try 'rmdir --help' for more information.\n");
}

TEST(rmdir, rmdir_verbose_single_uses_gnu_style_message) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "dir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rmdir.exe", {L"-v", L"dir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "rmdir: removing directory, 'dir'\n");
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "dir"));
}

TEST(rmdir, rmdir_verbose_reports_every_attempted_directory) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "dir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rmdir.exe", {L"-v", L"does_not_exist", L"dir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "rmdir: removing directory, 'does_not_exist'\n"
                 "rmdir: removing directory, 'dir'\n");
  EXPECT_EQ_TEXT(r.stderr_text,
                 "rmdir: failed to remove 'does_not_exist': No such file or "
                 "directory\n");
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "dir"));
}

TEST(rmdir, rmdir_parents_verbose_reports_failed_ancestor_attempt) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "a" / "b" / "c");
  tmp.write("a\\keep.txt", "keep");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rmdir.exe", {L"-pv", L"a/b/c"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "rmdir: removing directory, 'a/b/c'\n"
                 "rmdir: removing directory, 'a/b'\n"
                 "rmdir: removing directory, 'a'\n");
  EXPECT_EQ_TEXT(r.stderr_text,
                 "rmdir: failed to remove 'a': Directory not empty\n");
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "a"));
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "a" / "b"));
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "a" / "b" / "c"));
}

TEST(rmdir, rmdir_accepts_trailing_separator_operand) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "dir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rmdir.exe", {L"dir/"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "dir"));
}

TEST(rmdir, rmdir_file_with_trailing_separator_reports_not_directory) {
  TempDir tmp;
  tmp.write("file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rmdir.exe", {L"file.txt/"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "rmdir: failed to remove 'file.txt/': Not a directory\n");
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "file.txt"));
}

// [GNU] --path is a deprecated hidden alias for -p/--parents (issue #1075).
TEST(rmdir, rmdir_deprecated_path_alias_removes_ancestors) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "a" / "b");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rmdir.exe", {L"--path", L"a/b"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "a"));
}
