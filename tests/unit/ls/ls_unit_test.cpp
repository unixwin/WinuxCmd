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
 *  - File: ls_unit_test.cpp
 *  - CopyrightYear: 2026
 */
#include <filesystem>
#include <regex>

#include "framework/winuxtest.h"

TEST(ls, ls_basic) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");
  tmp.write("file2.txt", "content2");

  TEST_LOG_FILE_CONTENT("file1.txt", "content1");
  TEST_LOG_FILE_CONTENT("file2.txt", "content2");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {});

  TEST_LOG_CMD_LIST("ls.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ls.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Verify the output contains the expected files
  EXPECT_TRUE(r.stdout_text.find("file1.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file2.txt") != std::string::npos);
}

TEST(ls, ls_long_format) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");

  TEST_LOG_FILE_CONTENT("file1.txt", "content1");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-l"});

  TEST_LOG_CMD_LIST("ls.exe", L"-l");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ls.exe -l output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Verify the output contains the expected file in long format
  EXPECT_TRUE(r.stdout_text.find("file1.txt") != std::string::npos);
}

TEST(ls, ls_long_format_reports_hardlink_count) {
  TempDir tmp;
  tmp.write("original.txt", "content");

  std::filesystem::path original = tmp.path / "original.txt";
  std::filesystem::path alias = tmp.path / "alias.txt";
  bool created = CreateHardLinkW(alias.wstring().c_str(),
                                 original.wstring().c_str(), nullptr) != FALSE;
  EXPECT_TRUE(created);
  if (!created) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-l", L"original.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(
      std::regex_search(r.stdout_text, std::regex(R"([dl-][rwx-]{9}\s+2\s+)")));
}

TEST(ls, ls_all) {
  TempDir tmp;
  tmp.write("file.txt", "content");
  tmp.write(".hidden.txt", "hidden content");

  TEST_LOG_FILE_CONTENT("file.txt", "content");
  TEST_LOG_FILE_CONTENT(".hidden.txt", "hidden content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-a"});

  TEST_LOG_CMD_LIST("ls.exe", L"-a");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ls.exe -a output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Verify the output contains both regular and hidden files
  EXPECT_TRUE(r.stdout_text.find("file.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find(".hidden.txt") != std::string::npos);
}

TEST(ls, ls_single_file) {
  TempDir tmp;
  tmp.write("myfile.txt", "test content");

  TEST_LOG_FILE_CONTENT("myfile.txt", "test content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"myfile.txt"});

  TEST_LOG_CMD_LIST("ls.exe", L"myfile.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ls.exe myfile.txt output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("myfile.txt") != std::string::npos);
}

TEST(ls, ls_wildcard) {
  TempDir tmp;
  tmp.write("test1.txt", "content1");
  tmp.write("test2.txt", "content2");
  tmp.write("other.log", "log content");

  TEST_LOG_FILE_CONTENT("test1.txt", "content1");
  TEST_LOG_FILE_CONTENT("test2.txt", "content2");
  TEST_LOG_FILE_CONTENT("other.log", "log content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"*.txt"});

  TEST_LOG_CMD_LIST("ls.exe", L"*.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ls.exe *.txt output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("test1.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("test2.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("other.log") == std::string::npos);
}

TEST(ls, ls_char_class_wildcard) {
  TempDir tmp;
  tmp.write("a1.txt", "a1");
  tmp.write("a2.txt", "a2");
  tmp.write("b1.txt", "b1");
  tmp.write("c.log", "c");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"a?.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("a1.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("a2.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b1.txt") == std::string::npos);
}

TEST(ls, ls_char_class_range_wildcard) {
  TempDir tmp;
  tmp.write("a.txt", "a");
  tmp.write("b.txt", "b");
  tmp.write("c.txt", "c");
  tmp.write("d.txt", "d");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"[ab]*.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("a.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("c.txt") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("d.txt") == std::string::npos);
}

TEST(ls, ls_directory_only) {
  TempDir tmp;
  tmp.write("file.txt", "content");

  TEST_LOG_FILE_CONTENT("file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-d", L"."});

  TEST_LOG_CMD_LIST("ls.exe", L"-d", L".");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ls.exe -d . output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should display the directory itself, not its contents
  EXPECT_TRUE(r.stdout_text.find(".") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file.txt") == std::string::npos);
}

TEST(ls, ls_time_sort) {
  TempDir tmp;
  tmp.write("old.txt", "old content");
  tmp.write("new.txt", "new content");

  TEST_LOG_FILE_CONTENT("old.txt", "old content");
  TEST_LOG_FILE_CONTENT("new.txt", "new content");

  // Use touch -d to set different file times
  // Set old.txt to an older time: 2025-01-01 10:00
  Pipeline touch_old;
  touch_old.set_cwd(tmp.wpath());
  touch_old.add(L"touch.exe", {L"-d", L"202501011000", L"old.txt"});
  touch_old.run();

  // Set new.txt to a newer time: 2025-01-01 12:00
  Pipeline touch_new;
  touch_new.set_cwd(tmp.wpath());
  touch_new.add(L"touch.exe", {L"-d", L"202501011200", L"new.txt"});
  touch_new.run();

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-lt"});

  TEST_LOG_CMD_LIST("ls.exe", L"-lt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ls.exe -lt output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Check that both files are in the output
  bool has_new = r.stdout_text.find("new.txt") != std::string::npos;
  bool has_old = r.stdout_text.find("old.txt") != std::string::npos;
  EXPECT_TRUE(has_new);
  EXPECT_TRUE(has_old);

  // In -t mode, newer files should appear before older ones
  // Since new.txt is set to 12:00 and old.txt to 10:00,
  // new.txt should appear first in the output
  size_t new_pos = r.stdout_text.find("new.txt");
  size_t old_pos = r.stdout_text.find("old.txt");

  if (has_new && has_old) {
    EXPECT_LT(new_pos, old_pos);
  }
}

TEST(ls, ls_size_sort) {
  TempDir tmp;
  tmp.write("small.txt", "x");
  tmp.write("large.txt", std::string(1000, 'x'));

  TEST_LOG_FILE_CONTENT("small.txt", "x");
  TEST_LOG_FILE_CONTENT("large.txt", std::string(1000, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-lS"});

  TEST_LOG_CMD_LIST("ls.exe", L"-lS");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ls.exe -lS output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("small.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("large.txt") != std::string::npos);

  // In -S mode, larger files should appear before smaller ones
  size_t large_pos = r.stdout_text.find("large.txt");
  size_t small_pos = r.stdout_text.find("small.txt");
  EXPECT_LT(large_pos, small_pos);
}

TEST(ls, ls_recursive) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "subdir1");
  std::filesystem::create_directory(tmp.path / "subdir2");
  tmp.write("subdir1/file1.txt", "content1");
  tmp.write("subdir2/file2.txt", "content2");
  tmp.write("root.txt", "root content");

  TEST_LOG_FILE_CONTENT("subdir1/file1.txt", "content1");
  TEST_LOG_FILE_CONTENT("subdir2/file2.txt", "content2");
  TEST_LOG_FILE_CONTENT("root.txt", "root content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-R"});

  TEST_LOG_CMD_LIST("ls.exe", L"-R");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ls.exe -R output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should contain root directory contents
  EXPECT_TRUE(r.stdout_text.find("root.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("subdir1") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("subdir2") != std::string::npos);
  // Should contain subdirectory contents
  EXPECT_TRUE(r.stdout_text.find("file1.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file2.txt") != std::string::npos);
}

TEST(ls, ls_reverse_sort) {
  TempDir tmp;
  tmp.write("aaa.txt", "a");
  tmp.write("bbb.txt", "b");
  tmp.write("ccc.txt", "c");

  TEST_LOG_FILE_CONTENT("aaa.txt", "a");
  TEST_LOG_FILE_CONTENT("bbb.txt", "b");
  TEST_LOG_FILE_CONTENT("ccc.txt", "c");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-r"});

  TEST_LOG_CMD_LIST("ls.exe", L"-r");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ls.exe -r output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("aaa.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("bbb.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("ccc.txt") != std::string::npos);

  // In reverse mode, files should appear in reverse alphabetical order
  size_t aaa_pos = r.stdout_text.find("aaa.txt");
  size_t bbb_pos = r.stdout_text.find("bbb.txt");
  size_t ccc_pos = r.stdout_text.find("ccc.txt");
  EXPECT_GT(aaa_pos, bbb_pos);
  EXPECT_GT(bbb_pos, ccc_pos);
}

TEST(ls, ls_extension_sort) {
  TempDir tmp;
  tmp.write("README", "readme");
  tmp.write("alpha.log", "log");
  tmp.write("beta.txt", "txt");
  tmp.write("gamma.txt", "txt2");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-1", L"-X"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  size_t readme_pos = r.stdout_text.find("README");
  size_t log_pos = r.stdout_text.find("alpha.log");
  size_t beta_pos = r.stdout_text.find("beta.txt");
  size_t gamma_pos = r.stdout_text.find("gamma.txt");
  EXPECT_TRUE(readme_pos != std::string::npos);
  EXPECT_TRUE(log_pos != std::string::npos);
  EXPECT_TRUE(beta_pos != std::string::npos);
  EXPECT_TRUE(gamma_pos != std::string::npos);
  EXPECT_LT(readme_pos, log_pos);
  EXPECT_LT(log_pos, beta_pos);
  EXPECT_LT(beta_pos, gamma_pos);
}

TEST(ls, ls_ignore_pattern) {
  TempDir tmp;
  tmp.write("keep.txt", "keep");
  tmp.write("skip.tmp", "skip");
  tmp.write("also.txt", "also");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-1", L"-I", L"*.tmp"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("keep.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("also.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("skip.tmp") == std::string::npos);
}

TEST(ls, ls_version_sort) {
  TempDir tmp;
  tmp.write("file1.txt", "1");
  tmp.write("file10.txt", "10");
  tmp.write("file2.txt", "2");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-1", L"-v"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  size_t file1_pos = r.stdout_text.find("file1.txt");
  size_t file2_pos = r.stdout_text.find("file2.txt");
  size_t file10_pos = r.stdout_text.find("file10.txt");
  EXPECT_TRUE(file1_pos != std::string::npos);
  EXPECT_TRUE(file2_pos != std::string::npos);
  EXPECT_TRUE(file10_pos != std::string::npos);
  EXPECT_LT(file1_pos, file2_pos);
  EXPECT_LT(file2_pos, file10_pos);
}

TEST(ls, ls_long_with_file) {
  TempDir tmp;
  tmp.write("testfile.txt", "test content for long format");

  TEST_LOG_FILE_CONTENT("testfile.txt", "test content for long format");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-l", L"testfile.txt"});

  TEST_LOG_CMD_LIST("ls.exe", L"-l", L"testfile.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ls.exe -l testfile.txt output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("testfile.txt") != std::string::npos);
  // Long format should include file permissions, size, date
  EXPECT_TRUE(r.stdout_text.find("-rw") != std::string::npos ||
              r.stdout_text.find("-r-") != std::string::npos);
}
