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
#include <chrono>
#include <filesystem>
#include <regex>
#include <thread>

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

TEST(ls, ls_long_directory_prints_total_line) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");
  tmp.write("file2.txt", "content2");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-l"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(
      std::regex_search(r.stdout_text, std::regex(R"(^total\s+\d+\n)")));
  EXPECT_TRUE(r.stdout_text.find("file1.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file2.txt") != std::string::npos);
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

TEST(ls, ls_multiple_file_operands_do_not_print_headers) {
  TempDir tmp;
  tmp.write("one.txt", "one");
  tmp.write("two.txt", "two");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"one.txt", L"two.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("one.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("two.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("one.txt:") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("two.txt:") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("\n\n") == std::string::npos);
}

TEST(ls, ls_mixed_file_and_directory_prints_directory_header) {
  TempDir tmp;
  tmp.write("top.txt", "top");
  std::filesystem::create_directories(tmp.path / "sub");
  tmp.write("sub/inside.txt", "inside");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"top.txt", L"sub"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("top.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("sub:") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("inside.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("top.txt:") == std::string::npos);
}

TEST(ls, ls_wildcard_directory_lists_contents_unless_directory_option) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "dirA");
  tmp.write("dirA/inside.txt", "inside");

  Pipeline contents;
  contents.set_cwd(tmp.wpath());
  contents.add(L"ls.exe", {L"dir*"});
  auto contents_result = contents.run();

  EXPECT_EQ(contents_result.exit_code, 0);
  EXPECT_TRUE(contents_result.stdout_text.find("inside.txt") !=
              std::string::npos);

  Pipeline directory_only;
  directory_only.set_cwd(tmp.wpath());
  directory_only.add(L"ls.exe", {L"-d", L"dir*"});
  auto directory_result = directory_only.run();

  EXPECT_EQ(directory_result.exit_code, 0);
  EXPECT_TRUE(directory_result.stdout_text.find("dirA") != std::string::npos);
  EXPECT_TRUE(directory_result.stdout_text.find("inside.txt") ==
              std::string::npos);
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

TEST(ls, ls_sort_option_last_wins) {
  TempDir tmp;
  tmp.write("big_old.txt", std::string(1000, 'x'));
  tmp.write("small_new.txt", "x");

  Pipeline touch_old;
  touch_old.set_cwd(tmp.wpath());
  touch_old.add(L"touch.exe", {L"-d", L"202501011000", L"big_old.txt"});
  touch_old.run();

  Pipeline touch_new;
  touch_new.set_cwd(tmp.wpath());
  touch_new.add(L"touch.exe", {L"-d", L"202501011200", L"small_new.txt"});
  touch_new.run();

  Pipeline size_last;
  size_last.set_cwd(tmp.wpath());
  size_last.add(L"ls.exe", {L"-1", L"-tS"});
  auto size_last_result = size_last.run();

  EXPECT_EQ(size_last_result.exit_code, 0);
  EXPECT_LT(size_last_result.stdout_text.find("big_old.txt"),
            size_last_result.stdout_text.find("small_new.txt"));

  Pipeline time_last;
  time_last.set_cwd(tmp.wpath());
  time_last.add(L"ls.exe", {L"-1", L"-St"});
  auto time_last_result = time_last.run();

  EXPECT_EQ(time_last_result.exit_code, 0);
  EXPECT_LT(time_last_result.stdout_text.find("small_new.txt"),
            time_last_result.stdout_text.find("big_old.txt"));
}

TEST(ls, ls_invalid_sort_and_time_options_fail) {
  TempDir tmp;
  tmp.write("sample.txt", "sample");

  Pipeline bad_sort;
  bad_sort.set_cwd(tmp.wpath());
  bad_sort.add(L"ls.exe", {L"--sort=bad"});
  auto bad_sort_result = bad_sort.run();

  EXPECT_NE(bad_sort_result.exit_code, 0);

  Pipeline bad_time;
  bad_time.set_cwd(tmp.wpath());
  bad_time.add(L"ls.exe", {L"--time=bad"});
  auto bad_time_result = bad_time.run();

  EXPECT_NE(bad_time_result.exit_code, 0);
}

TEST(ls, ls_comma_format) {
  TempDir tmp;
  tmp.write("a.txt", "a");
  tmp.write("b.txt", "b");
  tmp.write("c.txt", "c");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-m"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("a.txt, b.txt, c.txt") != std::string::npos);
}

TEST(ls, ls_horizontal_layout) {
  TempDir tmp;
  tmp.write("a", "a");
  tmp.write("b", "b");
  tmp.write("c", "c");
  tmp.write("d", "d");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-x", L"-w", L"7"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  auto newline = r.stdout_text.find('\n');
  EXPECT_NE(newline, std::string::npos);
  if (newline == std::string::npos) return;
  std::string first_line = r.stdout_text.substr(0, newline);
  EXPECT_TRUE(first_line.find("a") != std::string::npos);
  EXPECT_TRUE(first_line.find("b") != std::string::npos);
  EXPECT_TRUE(first_line.find("c") == std::string::npos);
}

TEST(ls, ls_access_time_sort) {
  TempDir tmp;
  tmp.write("old.txt", "old");
  tmp.write("new.txt", "new");

  Pipeline touch_old;
  touch_old.set_cwd(tmp.wpath());
  touch_old.add(L"touch.exe", {L"-a", L"-d", L"202501011000", L"old.txt"});
  touch_old.run();

  Pipeline touch_new;
  touch_new.set_cwd(tmp.wpath());
  touch_new.add(L"touch.exe", {L"-a", L"-d", L"202501011200", L"new.txt"});
  touch_new.run();

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-1", L"-u"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  size_t new_pos = r.stdout_text.find("new.txt");
  size_t old_pos = r.stdout_text.find("old.txt");
  EXPECT_TRUE(new_pos != std::string::npos);
  EXPECT_TRUE(old_pos != std::string::npos);
  EXPECT_LT(new_pos, old_pos);
}

TEST(ls, ls_birth_time_sort) {
  TempDir tmp;
  tmp.write("old.txt", "old");
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  tmp.write("new.txt", "new");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-1", L"-c"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  size_t new_pos = r.stdout_text.find("new.txt");
  size_t old_pos = r.stdout_text.find("old.txt");
  EXPECT_TRUE(new_pos != std::string::npos);
  EXPECT_TRUE(old_pos != std::string::npos);
  EXPECT_LT(new_pos, old_pos);
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

TEST(ls, ls_extension_sort_long_option) {
  TempDir tmp;
  tmp.write("README", "readme");
  tmp.write("alpha.log", "log");
  tmp.write("beta.txt", "txt");
  tmp.write("gamma.txt", "txt2");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-1", L"--sort=extension"});

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

TEST(ls, ls_indicator_style_and_file_type) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "subdir");
  tmp.write("plain.txt", "plain");
  tmp.write("script.bat", "@echo off");

  Pipeline classify;
  classify.set_cwd(tmp.wpath());
  classify.add(L"ls.exe", {L"-1", L"--indicator-style=classify"});

  auto classify_result = classify.run();

  EXPECT_EQ(classify_result.exit_code, 0);
  EXPECT_TRUE(classify_result.stdout_text.find("subdir/") != std::string::npos);
  EXPECT_TRUE(classify_result.stdout_text.find("script.bat*") !=
              std::string::npos);

  Pipeline file_type;
  file_type.set_cwd(tmp.wpath());
  file_type.add(L"ls.exe", {L"-1", L"--file-type"});

  auto file_type_result = file_type.run();

  EXPECT_EQ(file_type_result.exit_code, 0);
  EXPECT_TRUE(file_type_result.stdout_text.find("subdir/") !=
              std::string::npos);
  EXPECT_TRUE(file_type_result.stdout_text.find("script.bat*") ==
              std::string::npos);
}

TEST(ls, ls_quote_name_uses_c_quoting_and_suffix_outside_quotes) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "dir");
  tmp.write("two words.txt", "content");

  Pipeline quoted_file;
  quoted_file.set_cwd(tmp.wpath());
  quoted_file.add(L"ls.exe", {L"-1", L"-Q", L"two words.txt"});
  auto quoted_file_result = quoted_file.run();

  EXPECT_EQ(quoted_file_result.exit_code, 0);
  EXPECT_EQ_TEXT(quoted_file_result.stdout_text, "\"two words.txt\"\n");

  Pipeline quoted_dir;
  quoted_dir.set_cwd(tmp.wpath());
  quoted_dir.add(L"ls.exe", {L"-d", L"-Q", L"-F", L"dir"});
  auto quoted_dir_result = quoted_dir.run();

  EXPECT_EQ(quoted_dir_result.exit_code, 0);
  EXPECT_EQ_TEXT(quoted_dir_result.stdout_text, "\"dir\"/\n");
}

TEST(ls, ls_quoting_style_aliases) {
  TempDir tmp;
  tmp.write("two words.txt", "content");
  tmp.write("quote's.txt", "content");

  Pipeline c_style;
  c_style.set_cwd(tmp.wpath());
  c_style.add(L"ls.exe", {L"-1", L"--quoting-style=c", L"two words.txt"});
  auto c_style_result = c_style.run();

  EXPECT_EQ(c_style_result.exit_code, 0);
  EXPECT_EQ_TEXT(c_style_result.stdout_text, "\"two words.txt\"\n");

  Pipeline escape_style;
  escape_style.set_cwd(tmp.wpath());
  escape_style.add(L"ls.exe",
                   {L"-1", L"--quoting-style=escape", L"two words.txt"});
  auto escape_style_result = escape_style.run();

  EXPECT_EQ(escape_style_result.exit_code, 0);
  EXPECT_EQ_TEXT(escape_style_result.stdout_text, "two words.txt\n");

  Pipeline shell_style;
  shell_style.set_cwd(tmp.wpath());
  shell_style.add(L"ls.exe", {L"-1", L"--quoting-style=shell-escape-always",
                              L"quote's.txt"});
  auto shell_style_result = shell_style.run();

  EXPECT_EQ(shell_style_result.exit_code, 0);
  EXPECT_EQ_TEXT(shell_style_result.stdout_text, "'quote'\\''s.txt'\n");
}

TEST(ls, ls_quoting_style_last_wins_and_invalid_fails) {
  TempDir tmp;
  tmp.write("two words.txt", "content");

  Pipeline last_wins;
  last_wins.set_cwd(tmp.wpath());
  last_wins.add(L"ls.exe", {L"-1", L"--quoting-style=c",
                            L"--quoting-style=literal", L"two words.txt"});
  auto last_wins_result = last_wins.run();

  EXPECT_EQ(last_wins_result.exit_code, 0);
  EXPECT_EQ_TEXT(last_wins_result.stdout_text, "two words.txt\n");

  Pipeline invalid;
  invalid.set_cwd(tmp.wpath());
  invalid.add(L"ls.exe", {L"--quoting-style=bad", L"two words.txt"});
  auto invalid_result = invalid.run();

  EXPECT_NE(invalid_result.exit_code, 0);
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

TEST(ls, ls_inode_and_blocks_prefixes) {
  TempDir tmp;
  tmp.write("sample.txt", "sample");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-lis", L"sample.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::regex_search(
      r.stdout_text, std::regex(R"(^\s*\d+\s+\d+\s+[dl-][rwx-]{9}\s+\d+\s+)")));
  EXPECT_TRUE(r.stdout_text.find("sample.txt") != std::string::npos);
}

TEST(ls, ls_human_readable_file_sizes_match_gnu_shape) {
  TempDir tmp;
  tmp.write("tiny.txt", "12345678");
  tmp.write("two_k.bin", std::string(2048, 'x'));

  Pipeline tiny;
  tiny.set_cwd(tmp.wpath());
  tiny.add(L"ls.exe", {L"-lh", L"tiny.txt"});
  auto tiny_result = tiny.run();

  EXPECT_EQ(tiny_result.exit_code, 0);
  EXPECT_TRUE(tiny_result.stdout_text.find("8.0B") == std::string::npos);
  EXPECT_TRUE(std::regex_search(
      tiny_result.stdout_text,
      std::regex(
          R"(\s8\s+[A-Z][a-z][a-z]\s+\d{1,2}\s+\d{2}:\d{2}\s+tiny\.txt)")));

  Pipeline two_k;
  two_k.set_cwd(tmp.wpath());
  two_k.add(L"ls.exe", {L"-lh", L"two_k.bin"});
  auto two_k_result = two_k.run();

  EXPECT_EQ(two_k_result.exit_code, 0);
  EXPECT_TRUE(two_k_result.stdout_text.find("2.0K") != std::string::npos);
}

TEST(ls, ls_block_size_scales_long_size_column) {
  TempDir tmp;
  tmp.write("big.bin", std::string(2049, 'x'));

  Pipeline bytes;
  bytes.set_cwd(tmp.wpath());
  bytes.add(L"ls.exe", {L"-l", L"--block-size=1", L"big.bin"});
  auto bytes_result = bytes.run();

  EXPECT_EQ(bytes_result.exit_code, 0);
  EXPECT_TRUE(bytes_result.stdout_text.find("2049") != std::string::npos);

  Pipeline kib;
  kib.set_cwd(tmp.wpath());
  kib.add(L"ls.exe", {L"-l", L"--block-size=K", L"big.bin"});
  auto kib_result = kib.run();

  EXPECT_EQ(kib_result.exit_code, 0);
  EXPECT_TRUE(std::regex_search(
      kib_result.stdout_text,
      std::regex(
          R"(\s3\s+[A-Z][a-z][a-z]\s+\d{1,2}\s+\d{2}:\d{2}\s+big\.bin)")));
}

TEST(ls, ls_block_size_humanizes_blocks_and_total) {
  TempDir tmp;
  tmp.write("sample.txt", std::string(2048, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-lsh"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::regex_search(r.stdout_text, std::regex(R"(^total\s+\S+)")));
  EXPECT_TRUE(r.stdout_text.find("2.0K") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("sample.txt") != std::string::npos);
}

TEST(ls, ls_invalid_block_size_fails) {
  TempDir tmp;
  tmp.write("sample.txt", "sample");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"--block-size=not-a-size", L"sample.txt"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
}
