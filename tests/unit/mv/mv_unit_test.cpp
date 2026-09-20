// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(mv, mv_basic) {
  TempDir tmp;
  tmp.write("source.txt", "hello world");

  TEST_LOG_FILE_CONTENT("source.txt", "hello world");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mv.exe", {L"source.txt", L"dest.txt"});

  TEST_LOG_CMD_LIST("mv.exe", L"source.txt", L"dest.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("mv.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify the source file was removed and the destination file was created
  bool source_exists = std::filesystem::exists(tmp.path / "source.txt");
  bool dest_exists = std::filesystem::exists(tmp.path / "dest.txt");
  EXPECT_TRUE(!source_exists);
  EXPECT_TRUE(dest_exists);

  // Verify the destination file has the correct content
  std::string dest_content = tmp.read("dest.txt");
  TEST_LOG("dest.txt content", dest_content);
  EXPECT_EQ(dest_content, "hello world");
}

TEST(mv, mv_cross_volume_directory_falls_back_to_copy_and_remove) {
  TempDir source_tmp;
  TempDir destination_tmp;
  std::filesystem::create_directories(source_tmp.path / "source_dir" /
                                      "nested");
  source_tmp.write("source_dir/file.txt", "content");
  source_tmp.write("source_dir/nested/child.txt", "child");

  Pipeline p;
  p.set_cwd(source_tmp.wpath());
  p.add(L"mv.exe", {source_tmp.wpath() + L"/source_dir",
                    destination_tmp.wpath() + L"/moved_dir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(source_tmp.path / "source_dir"));
  EXPECT_EQ(destination_tmp.read("moved_dir/file.txt"), "content");
  EXPECT_EQ(destination_tmp.read("moved_dir/nested/child.txt"), "child");
}

TEST(mv, mv_move_to_directory) {
  TempDir tmp;
  tmp.write("file.txt", "content");
  std::filesystem::create_directory(tmp.path / "dest_dir");

  TEST_LOG_FILE_CONTENT("file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mv.exe", {L"file.txt", L"dest_dir"});

  TEST_LOG_CMD_LIST("mv.exe", L"file.txt", L"dest_dir");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("mv.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify the file was moved to the directory
  bool source_exists = std::filesystem::exists(tmp.path / "file.txt");
  bool dest_exists =
      std::filesystem::exists(tmp.path / "dest_dir" / "file.txt");
  EXPECT_TRUE(!source_exists);
  EXPECT_TRUE(dest_exists);
}

TEST(mv, mv_move_multiple_files) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");
  tmp.write("file2.txt", "content2");
  std::filesystem::create_directory(tmp.path / "dest_dir");

  TEST_LOG_FILE_CONTENT("file1.txt", "content1");
  TEST_LOG_FILE_CONTENT("file2.txt", "content2");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mv.exe", {L"file1.txt", L"file2.txt", L"dest_dir"});

  TEST_LOG_CMD_LIST("mv.exe", L"file1.txt", L"file2.txt", L"dest_dir");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("mv.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify both files were moved to the directory
  bool file1_exists = std::filesystem::exists(tmp.path / "file1.txt");
  bool file2_exists = std::filesystem::exists(tmp.path / "file2.txt");
  bool dest1_exists =
      std::filesystem::exists(tmp.path / "dest_dir" / "file1.txt");
  bool dest2_exists =
      std::filesystem::exists(tmp.path / "dest_dir" / "file2.txt");
  EXPECT_TRUE(!file1_exists);
  EXPECT_TRUE(!file2_exists);
  EXPECT_TRUE(dest1_exists);
  EXPECT_TRUE(dest2_exists);
}

TEST(mv, mv_target_directory_option) {
  TempDir tmp;
  tmp.write("file.txt", "content");
  std::filesystem::create_directory(tmp.path / "dest_dir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mv.exe", {L"-t", L"dest_dir", L"file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(!std::filesystem::exists(tmp.path / "file.txt"));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "dest_dir" / "file.txt"));
}

TEST(mv, mv_target_directory_requires_existing_directory) {
  TempDir tmp;
  tmp.write("file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mv.exe", {L"-t", L"missing_dir", L"file.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "file.txt"));
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "missing_dir"));
}

TEST(mv, mv_target_directory_and_no_target_directory_conflict) {
  TempDir tmp;
  tmp.write("file.txt", "content");
  std::filesystem::create_directory(tmp.path / "dest_dir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mv.exe", {L"-t", L"dest_dir", L"-T", L"file.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "file.txt"));
}

TEST(mv, mv_no_clobber_keeps_existing_destination) {
  TempDir tmp;
  tmp.write("source.txt", "new content");
  tmp.write("dest.txt", "old content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mv.exe", {L"-n", L"source.txt", L"dest.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "source.txt"));
  EXPECT_EQ(tmp.read("dest.txt"), "old content");
}

TEST(mv, mv_last_overwrite_option_can_select_no_clobber) {
  TempDir tmp;
  tmp.write("source.txt", "new content");
  tmp.write("dest.txt", "old content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mv.exe", {L"source.txt", L"dest.txt", L"-f", L"-i", L"-n"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "source.txt"));
  EXPECT_EQ(tmp.read("dest.txt"), "old content");
}

TEST(mv, mv_last_overwrite_option_can_select_interactive) {
  TempDir tmp;
  tmp.write("source.txt", "new content");
  tmp.write("dest.txt", "old content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("n\n");
  p.add(L"mv.exe", {L"source.txt", L"dest.txt", L"-n", L"-f", L"-i"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stderr_text.find("overwrite 'dest.txt'?"), std::string::npos);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "source.txt"));
  EXPECT_EQ(tmp.read("dest.txt"), "old content");
}

TEST(mv, mv_last_overwrite_option_can_select_force) {
  TempDir tmp;
  tmp.write("source.txt", "new content");
  tmp.write("dest.txt", "old content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mv.exe", {L"source.txt", L"dest.txt", L"-i", L"-n", L"-f"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stderr_text.find("overwrite 'dest.txt'?"), std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "source.txt"));
  EXPECT_EQ(tmp.read("dest.txt"), "new content");
}

TEST(mv, mv_bare_interactive_long_option_prompts_like_always) {
  TempDir tmp;
  tmp.write("source.txt", "new content");
  tmp.write("dest.txt", "old content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("n\n");
  p.add(L"mv.exe", {L"--interactive", L"source.txt", L"dest.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stderr_text.find("overwrite 'dest.txt'?"), std::string::npos);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "source.txt"));
  EXPECT_EQ(tmp.read("dest.txt"), "old content");
}

TEST(mv, mv_invalid_interactive_mode_fails) {
  TempDir tmp;
  tmp.write("source.txt", "new content");
  tmp.write("dest.txt", "old content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mv.exe", {L"--interactive=maybe", L"source.txt", L"dest.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_NE(r.stderr_text.find("invalid argument 'maybe'"), std::string::npos);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "source.txt"));
  EXPECT_EQ(tmp.read("dest.txt"), "old content");
}

TEST(mv, mv_wildcard_sources_expand) {
  TempDir tmp;
  tmp.write("a.txt", "alpha");
  tmp.write("b.txt", "beta");
  std::filesystem::create_directory(tmp.path / "dest_dir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mv.exe", {L"*.txt", L"dest_dir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "a.txt"));
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "b.txt"));
  EXPECT_EQ(tmp.read("dest_dir/a.txt"), "alpha");
  EXPECT_EQ(tmp.read("dest_dir/b.txt"), "beta");
}

TEST(mv, mv_multiple_sources_require_existing_directory) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");
  tmp.write("file2.txt", "content2");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mv.exe", {L"file1.txt", L"file2.txt", L"missing_dir"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "file1.txt"));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "file2.txt"));
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "missing_dir"));
}

TEST(mv, mv_wildcard_multiple_sources_require_existing_directory) {
  TempDir tmp;
  tmp.write("a.txt", "alpha");
  tmp.write("b.txt", "beta");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mv.exe", {L"*.txt", L"missing_dir"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_EQ(tmp.read("a.txt"), "alpha");
  EXPECT_EQ(tmp.read("b.txt"), "beta");
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "missing_dir"));
}

// [GNU 9.4] a trailing separator forces a directory operand: a regular
// file with a trailing slash fails at stat time (uutils#10026 family).
TEST(mv, mv_trailing_slash_on_file_reports_not_a_directory) {
  TempDir tmp;
  tmp.write("file.txt", "data");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mv.exe", {L"file.txt/", L"renamed.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "mv: cannot stat 'file.txt/': Not a directory\n");
  EXPECT_EQ(tmp.read("file.txt"), "data");
}

// [GNU] a read-only destination is replaced without prompting when stdin
// is not a terminal (uutils#11321).  The test harness pipes stdin, so no
// "overriding mode" prompt may appear.
TEST(mv, mv_readonly_destination_no_prompt_on_non_tty) {
  TempDir tmp;
  tmp.write("src.txt", "new");
  tmp.write("dest.txt", "old");
  auto dest = tmp.path / "dest.txt";
  DWORD attrs = GetFileAttributesW(dest.wstring().c_str());
  SetFileAttributesW(dest.wstring().c_str(), attrs | FILE_ATTRIBUTE_READONLY);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("");  // guarantee a piped, non-tty stdin
  p.add(L"mv.exe", {L"src.txt", L"dest.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stderr_text.find("overriding mode"), std::string::npos);
  EXPECT_EQ(tmp.read("dest.txt"), "new");
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "src.txt"));
}

TEST(mv, mv_exchange_swaps_two_files) {
  TempDir tmp;
  tmp.write("a.txt", "AAA");
  tmp.write("b.txt", "BBB");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mv.exe", {L"--exchange", L"a.txt", L"b.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("a.txt"), "BBB");
  EXPECT_EQ(tmp.read("b.txt"), "AAA");
}

TEST(mv, mv_exchange_swaps_directories) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "d1");
  std::filesystem::create_directory(tmp.path / "d2");
  tmp.write("d1/f.txt", "one");
  tmp.write("d2/g.txt", "two");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mv.exe", {L"--exchange", L"d1", L"d2"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("d1/g.txt"), "two");
  EXPECT_EQ(tmp.read("d2/f.txt"), "one");
}

TEST(mv, mv_exchange_missing_operand_fails) {
  TempDir tmp;
  tmp.write("a.txt", "AAA");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mv.exe", {L"--exchange", L"a.txt", L"nonexist"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("No such file or directory"), std::string::npos);
  EXPECT_EQ(tmp.read("a.txt"), "AAA");
}
