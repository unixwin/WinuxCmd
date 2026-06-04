/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a
 *  copy of this software and associated documentation files (the "Software"),
 *  to deal in the Software without restriction, including without limitation
 *  the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
 *  Software is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 *
 *  - File: install_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(install, install_basic) {
  TempDir tmp;
  tmp.write("source.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"source.txt", L"dest.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "dest.txt"));
  std::string content = tmp.read("dest.txt");
  EXPECT_EQ(content, "hello\n");
}

TEST(install, install_directory) {
  TempDir tmp;
  tmp.write("source.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"-d", L"parent\\newdir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "parent" / "newdir"));
}

TEST(install, install_target_directory) {
  TempDir tmp;
  tmp.write("source.txt", "hello\n");
  std::filesystem::create_directory(tmp.path / "dest_dir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"-t", L"dest_dir", L"source.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "dest_dir" / "source.txt"));
}

TEST(install, install_target_directory_requires_existing_directory) {
  TempDir tmp;
  tmp.write("source.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"-t", L"missing_dir", L"source.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "missing_dir"));
}

TEST(install, install_target_directory_and_no_target_directory_conflict) {
  TempDir tmp;
  tmp.write("source.txt", "hello\n");
  std::filesystem::create_directory(tmp.path / "dest_dir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"-t", L"dest_dir", L"-T", L"source.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "dest_dir" / "source.txt"));
}

TEST(install, install_create_leading_dirs) {
  TempDir tmp;
  tmp.write("source.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"-D", L"source.txt", L"nested\\dir\\dest.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(
      std::filesystem::exists(tmp.path / "nested" / "dir" / "dest.txt"));
}

TEST(install, install_create_target_directory_with_D_and_t) {
  TempDir tmp;
  tmp.write("source.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"-D", L"-t", L"nested\\dir", L"source.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(
      std::filesystem::exists(tmp.path / "nested" / "dir" / "source.txt"));
}

TEST(install, install_no_target_directory) {
  TempDir tmp;
  tmp.write("source.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"-T", L"source.txt", L"dest.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "dest.txt"));
}

TEST(install, install_compare_skips_identical_destination) {
  TempDir tmp;
  tmp.write("source.txt", "hello\n");
  tmp.write("dest.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"-C", L"-b", L"source.txt", L"dest.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "dest.txt~"));
  EXPECT_EQ(tmp.read("dest.txt"), "hello\n");
}

TEST(install, install_preserve_timestamps) {
  TempDir tmp;
  tmp.write("source.txt", "hello\n");

  auto old_time =
      std::filesystem::file_time_type::clock::now() - std::chrono::hours(24);
  std::filesystem::last_write_time(tmp.path / "source.txt", old_time);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"-p", L"source.txt", L"dest.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  auto source_time = std::filesystem::last_write_time(tmp.path / "source.txt");
  auto dest_time = std::filesystem::last_write_time(tmp.path / "dest.txt");
  EXPECT_EQ(dest_time, source_time);
}

TEST(install, install_destination_operand_is_literal_not_glob) {
  TempDir tmp;
  tmp.write("source.txt", "new\n");
  tmp.write("desta.txt", "old\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"source.txt", L"dest[abc].txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("desta.txt"), "old\n");
  EXPECT_EQ(tmp.read("dest[abc].txt"), "new\n");
}

TEST(install, install_directory_operand_is_literal_not_glob) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "dira");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"-d", L"dir[abc]"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "dir[abc]"));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "dira"));
}

TEST(install, install_multiple_sources_require_existing_directory) {
  TempDir tmp;
  tmp.write("file1.txt", "one\n");
  tmp.write("file2.txt", "two\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"file1.txt", L"file2.txt", L"missing_dir"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "missing_dir"));
}

TEST(install, install_wildcard_multiple_sources_require_existing_directory) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\n");
  tmp.write("b.txt", "beta\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"*.txt", L"missing_dir"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "missing_dir"));
}
