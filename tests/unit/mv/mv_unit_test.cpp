/*
 *  Copyright © 2026 WinuxCmd
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: mv_unit_test.cpp
 *  - CopyrightYear: 2026
 */
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
