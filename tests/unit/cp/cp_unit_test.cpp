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
 *  - File: cp_unit_test.cpp
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(cp, cp_basic_copy) {
  TempDir tmp;
  tmp.write("source.txt", "hello world");

  TEST_LOG_FILE_CONTENT("source.txt", "hello world");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"source.txt", L"dest.txt"});

  TEST_LOG_CMD_LIST("cp.exe", L"source.txt", L"dest.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("cp.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify the destination file was created and has the correct content
  std::string dest_content = tmp.read("dest.txt");
  TEST_LOG("dest.txt content", dest_content);
  EXPECT_EQ(dest_content, "hello world");
}

TEST(cp, cp_copy_multiple_files) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");
  tmp.write("file2.txt", "content2");
  std::filesystem::create_directory(tmp.path / "dest_dir");

  TEST_LOG_FILE_CONTENT("file1.txt", "content1");
  TEST_LOG_FILE_CONTENT("file2.txt", "content2");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"file1.txt", L"file2.txt", L"dest_dir"});

  TEST_LOG_CMD_LIST("cp.exe", L"file1.txt", L"file2.txt", L"dest_dir");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("cp.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify the files were copied to the destination directory
  std::string dest1_content = tmp.read("dest_dir/file1.txt");
  std::string dest2_content = tmp.read("dest_dir/file2.txt");
  TEST_LOG("dest_dir/file1.txt content", dest1_content);
  TEST_LOG("dest_dir/file2.txt content", dest2_content);
  EXPECT_EQ(dest1_content, "content1");
  EXPECT_EQ(dest2_content, "content2");
}

TEST(cp, cp_recursive_copy) {
  TempDir tmp;

  // Create source directory structure
  std::filesystem::create_directory(tmp.path / "src_dir");
  std::filesystem::create_directory(tmp.path / "src_dir/sub_dir");
  tmp.write("src_dir/file1.txt", "content1");
  tmp.write("src_dir/sub_dir/file2.txt", "content2");

  TEST_LOG_FILE_CONTENT("src_dir/file1.txt", "content1");
  TEST_LOG_FILE_CONTENT("src_dir/sub_dir/file2.txt", "content2");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"-r", L"src_dir", L"dest_dir"});

  TEST_LOG_CMD_LIST("cp.exe", L"-r", L"src_dir", L"dest_dir");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("cp.exe -r output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify the directory structure was copied recursively
  bool dest_dir_exists = std::filesystem::exists(tmp.path / "dest_dir") &&
                         std::filesystem::is_directory(tmp.path / "dest_dir");
  bool sub_dir_exists =
      std::filesystem::exists(tmp.path / "dest_dir" / "sub_dir") &&
      std::filesystem::is_directory(tmp.path / "dest_dir" / "sub_dir");
  EXPECT_TRUE(dest_dir_exists);
  EXPECT_TRUE(sub_dir_exists);

  // Verify the files were copied correctly
  std::string dest1_content = tmp.read("dest_dir/file1.txt");
  std::string dest2_content = tmp.read("dest_dir/sub_dir/file2.txt");
  TEST_LOG("dest_dir/file1.txt content", dest1_content);
  TEST_LOG("dest_dir/sub_dir/file2.txt content", dest2_content);
  EXPECT_EQ(dest1_content, "content1");
  EXPECT_EQ(dest2_content, "content2");
}

TEST(cp, cp_verbose) {
  TempDir tmp;
  tmp.write("source.txt", "content");

  TEST_LOG_FILE_CONTENT("source.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"-v", L"source.txt", L"dest.txt"});

  TEST_LOG_CMD_LIST("cp.exe", L"-v", L"source.txt", L"dest.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("cp.exe -v output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify the output contains verbose information
  EXPECT_TRUE(r.stdout_text.find("'source.txt' -> 'dest.txt'") !=
              std::string::npos);

  // Verify the file was copied correctly
  std::string dest_content = tmp.read("dest.txt");
  TEST_LOG("dest.txt content", dest_content);
  EXPECT_EQ(dest_content, "content");
}

TEST(cp, cp_target_directory) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");
  tmp.write("file2.txt", "content2");
  std::filesystem::create_directory(tmp.path / "dest_dir");

  TEST_LOG_FILE_CONTENT("file1.txt", "content1");
  TEST_LOG_FILE_CONTENT("file2.txt", "content2");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe",
        {L"--target-directory", L"dest_dir", L"file1.txt", L"file2.txt"});

  TEST_LOG_CMD_LIST("cp.exe", L"--target-directory", L"dest_dir", L"file1.txt",
                    L"file2.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("cp.exe --target-directory output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify the files were copied to the destination directory
  std::string dest1_content = tmp.read("dest_dir/file1.txt");
  std::string dest2_content = tmp.read("dest_dir/file2.txt");
  TEST_LOG("dest_dir/file1.txt content", dest1_content);
  TEST_LOG("dest_dir/file2.txt content", dest2_content);
  EXPECT_EQ(dest1_content, "content1");
  EXPECT_EQ(dest2_content, "content2");
}

TEST(cp, cp_no_clobber) {
  TempDir tmp;
  tmp.write("source.txt", "new content");
  tmp.write("dest.txt", "old content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"-n", L"source.txt", L"dest.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("dest.txt"), "old content");
}

TEST(cp, cp_update_skips_newer_destination) {
  TempDir tmp;
  tmp.write("source.txt", "source content");
  tmp.write("dest.txt", "newer destination");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"-u", L"source.txt", L"dest.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("dest.txt"), "newer destination");
}
