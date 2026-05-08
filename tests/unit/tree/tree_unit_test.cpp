/*
 *  Copyright © 2026 WinuxCmd
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
 *  - File: tree_unit_test.cpp
 *  - CopyrightYear: 2026
 */
#include <filesystem>

#include "framework/winuxtest.h"

TEST(tree, tree_basic) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");
  tmp.write("file2.txt", "content2");
  std::filesystem::create_directories(tmp.path / "subdir");
  tmp.write("subdir/file3.txt", "content3");

  TEST_LOG_FILE_CONTENT("file1.txt", "content1");
  TEST_LOG_FILE_CONTENT("file2.txt", "content2");
  TEST_LOG_FILE_CONTENT("subdir/file3.txt", "content3");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tree.exe", {});

  TEST_LOG_CMD_LIST("tree.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tree.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Verify the output contains the expected files
  EXPECT_TRUE(r.stdout_text.find("file1.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file2.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("subdir") != std::string::npos);
}

TEST(tree, tree_depth_limit) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "level1" / "level2");
  tmp.write("level1/level2/file.txt", "deep content");

  TEST_LOG_FILE_CONTENT("level1/level2/file.txt", "deep content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tree.exe", {L"-L", L"1"});

  TEST_LOG_CMD_LIST("tree.exe", L"-L", L"1");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tree.exe -L 1 output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Verify level1 is shown but level2 is not
  EXPECT_TRUE(r.stdout_text.find("level1") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("level2") == std::string::npos);
}

TEST(tree, tree_dirs_only) {
  TempDir tmp;
  tmp.write("file.txt", "content");
  std::filesystem::create_directories(tmp.path / "dir1");
  std::filesystem::create_directories(tmp.path / "dir2");
  tmp.write("dir1/inside.txt", "inside");

  TEST_LOG_FILE_CONTENT("file.txt", "content");
  TEST_LOG_FILE_CONTENT("dir1/inside.txt", "inside");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tree.exe", {L"-d"});

  TEST_LOG_CMD_LIST("tree.exe", L"-d");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tree.exe -d output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Verify only directories are shown
  EXPECT_TRUE(r.stdout_text.find("dir1") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("dir2") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file.txt") == std::string::npos);
}

TEST(tree, tree_all_files) {
  TempDir tmp;
  tmp.write("normal.txt", "content");
  tmp.write(".hidden.txt", "hidden content");

  TEST_LOG_FILE_CONTENT("normal.txt", "content");
  TEST_LOG_FILE_CONTENT(".hidden.txt", "hidden content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tree.exe", {L"-a"});

  TEST_LOG_CMD_LIST("tree.exe", L"-a");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tree.exe -a output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Verify both files are shown
  EXPECT_TRUE(r.stdout_text.find("normal.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find(".hidden.txt") != std::string::npos);
}

TEST(tree, tree_full_path) {
  TempDir tmp;
  tmp.write("file.txt", "content");

  TEST_LOG_FILE_CONTENT("file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tree.exe", {L"-f"});

  TEST_LOG_CMD_LIST("tree.exe", L"-f");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tree.exe -f output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Verify full path is shown (should contain tmp path)
  EXPECT_TRUE(r.stdout_text.find("file.txt") != std::string::npos);
}

TEST(tree, tree_exclude_pattern) {
  TempDir tmp;
  tmp.write("file.txt", "content");
  tmp.write("test.tmp", "temp content");
  tmp.write("other.txt", "other content");

  TEST_LOG_FILE_CONTENT("file.txt", "content");
  TEST_LOG_FILE_CONTENT("test.tmp", "temp content");
  TEST_LOG_FILE_CONTENT("other.txt", "other content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tree.exe", {L"-I", L"*.tmp"});

  TEST_LOG_CMD_LIST("tree.exe", L"-I", L"*.tmp");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tree.exe -I output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Verify .tmp file is excluded
  EXPECT_TRUE(r.stdout_text.find("file.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("other.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("test.tmp") == std::string::npos);
}

TEST(tree, tree_include_pattern) {
  TempDir tmp;
  tmp.write("file.cpp", "c++ content");
  tmp.write("file.txt", "text content");
  tmp.write("file.py", "python content");

  TEST_LOG_FILE_CONTENT("file.cpp", "c++ content");
  TEST_LOG_FILE_CONTENT("file.txt", "text content");
  TEST_LOG_FILE_CONTENT("file.py", "python content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tree.exe", {L"-P", L"*.cpp"});

  TEST_LOG_CMD_LIST("tree.exe", L"-P", L"*.cpp");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tree.exe -P output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Verify only .cpp files are shown
  EXPECT_TRUE(r.stdout_text.find("file.cpp") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file.txt") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file.py") == std::string::npos);
}

TEST(tree, tree_show_size) {
  TempDir tmp;
  tmp.write("small.txt", "abc");
  tmp.write("large.txt", std::string(1000, 'x'));

  TEST_LOG_FILE_CONTENT("small.txt", "abc");
  TEST_LOG_FILE_CONTENT("large.txt", std::string(1000, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tree.exe", {L"-s"});

  TEST_LOG_CMD_LIST("tree.exe", L"-s");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tree.exe -s output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Verify size is shown (should contain brackets with size)
  EXPECT_TRUE(r.stdout_text.find("[") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("]") != std::string::npos);
}

TEST(tree, tree_sort_by_time) {
  TempDir tmp;
  tmp.write("old.txt", "old");
  tmp.write("new.txt", "new");

  TEST_LOG_FILE_CONTENT("old.txt", "old");
  TEST_LOG_FILE_CONTENT("new.txt", "new");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tree.exe", {L"-t"});

  TEST_LOG_CMD_LIST("tree.exe", L"-t");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tree.exe -t output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Verify files are listed
  EXPECT_TRUE(r.stdout_text.find("old.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("new.txt") != std::string::npos);
}

TEST(tree, tree_single_directory) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "mydir");
  tmp.write("mydir/file.txt", "content");

  TEST_LOG_FILE_CONTENT("mydir/file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tree.exe", {L"mydir"});

  TEST_LOG_CMD_LIST("tree.exe", L"mydir");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tree.exe mydir output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("mydir") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file.txt") != std::string::npos);
}
