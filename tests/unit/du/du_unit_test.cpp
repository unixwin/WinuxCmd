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
 *  - File: du_unit_test.cpp
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

namespace {

auto first_usage_value(const std::string& text) -> std::uintmax_t {
  std::stringstream ss(text);
  std::uintmax_t value = 0;
  ss >> value;
  return value;
}

auto line_usage_values(const std::string& text) -> std::vector<std::uintmax_t> {
  std::vector<std::uintmax_t> values;
  std::stringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    std::stringstream ss(line);
    std::uintmax_t value = 0;
    if (ss >> value) {
      values.push_back(value);
    }
  }
  return values;
}

}  // namespace

TEST(du, du_basic) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");
  tmp.write("file2.txt", "content2");

  TEST_LOG_FILE_CONTENT("file1.txt", "content1");
  TEST_LOG_FILE_CONTENT("file2.txt", "content2");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {});

  TEST_LOG_CMD_LIST("du.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("du.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should show directory size
  EXPECT_TRUE(r.stdout_text.length() > 0);
}

TEST(du, du_human_readable) {
  TempDir tmp;
  tmp.write("file.txt", "Hello, World!");

  TEST_LOG_FILE_CONTENT("file.txt", "Hello, World!");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-h"});

  TEST_LOG_CMD_LIST("du.exe", L"-h");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("du.exe -h output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should show human-readable sizes
  EXPECT_TRUE(r.stdout_text.length() > 0);
}

TEST(du, du_summarize) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "subdir");
  tmp.write("subdir/file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-s"});

  TEST_LOG_CMD_LIST("du.exe", L"-s");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("du.exe -s output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should only show total, not individual files
  EXPECT_TRUE(r.stdout_text.length() > 0);
}

TEST(du, du_max_depth) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "subdir");
  std::filesystem::create_directory(tmp.path / "subdir" / "nested");
  tmp.write("subdir/file.txt", "content");
  tmp.write("subdir/nested/file2.txt", "content2");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-d", L"1"});

  TEST_LOG_CMD_LIST("du.exe", L"-d", L"1");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("du.exe -d 1 output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should show directory size
  EXPECT_TRUE(r.stdout_text.length() > 0);
}

TEST(du, du_kilobytes) {
  TempDir tmp;
  tmp.write("file.txt", "content");

  TEST_LOG_FILE_CONTENT("file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-k"});

  TEST_LOG_CMD_LIST("du.exe", L"-k");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("du.exe -k output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should show sizes in KB
  EXPECT_TRUE(r.stdout_text.length() > 0);
}

TEST(du, du_kilobytes_rounds_up_small_files) {
  TempDir tmp;
  tmp.write("file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-k", L"file.txt"});

  TEST_LOG_CMD_LIST("du.exe", L"-k", L"file.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("du.exe -k file output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(first_usage_value(r.stdout_text), 1);
}

TEST(du, du_default_uses_1024_byte_blocks) {
  TempDir tmp;
  tmp.write("file.txt", std::string(600, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(first_usage_value(r.stdout_text), 1);
}

TEST(du, du_H_is_dereference_args_not_si) {
  TempDir tmp;
  tmp.write("file.txt", std::string(1500, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-H", L"file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(first_usage_value(r.stdout_text), 2);
  EXPECT_EQ(r.stdout_text.find("K"), std::string::npos);
}

TEST(du, du_si_is_long_option_only) {
  TempDir tmp;
  tmp.write("file.txt", std::string(1500, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"--si", L"file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("1.5K"), std::string::npos);
}

TEST(du, du_block_size_one_reports_bytes) {
  TempDir tmp;
  tmp.write("file.txt", "abcdef");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"--block-size=1", L"file.txt"});

  TEST_LOG_CMD_LIST("du.exe", L"--block-size=1", L"file.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("du.exe --block-size=1 output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(first_usage_value(r.stdout_text), 6);
}

TEST(du, du_apparent_size_is_accepted_as_windows_file_length_mode) {
  TempDir tmp;
  tmp.write("file.txt", "abcdef");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"--apparent-size", L"--block-size=1", L"file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(first_usage_value(r.stdout_text), 6);
}

TEST(du, du_threshold_positive_excludes_smaller_entries) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "root");
  tmp.write("root/small.txt", "tiny");
  tmp.write("root/large.bin", std::string(1500, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-a", L"--block-size=1", L"--threshold=1000", L"root"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("large.bin"), std::string::npos);
  EXPECT_EQ(r.stdout_text.find("small.txt"), std::string::npos);
}

TEST(du, du_threshold_negative_excludes_larger_entries) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "root");
  tmp.write("root/small.txt", "tiny");
  tmp.write("root/large.bin", std::string(1500, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-a", L"--block-size=1", L"--threshold=-10", L"root"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("small.txt"), std::string::npos);
  EXPECT_EQ(r.stdout_text.find("large.bin"), std::string::npos);
}

TEST(du, du_exclude_patterns_skip_files_and_directories) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "root" / "skipdir");
  tmp.write("root/keep.txt", "abc");
  tmp.write("root/skip.log", "defg");
  tmp.write("root/skipdir/nested.txt", "hidden");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-b", L"--exclude=*.log", L"--exclude=skipdir", L"root"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(first_usage_value(r.stdout_text), 3);
  EXPECT_EQ(r.stdout_text.find("skip.log"), std::string::npos);
  EXPECT_EQ(r.stdout_text.find("skipdir"), std::string::npos);
  EXPECT_EQ(r.stdout_text.find("nested.txt"), std::string::npos);
}

TEST(du, du_later_block_size_option_overrides_human_readable) {
  TempDir tmp;
  tmp.write("file.txt", std::string(1500, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-h", L"-B", L"1", L"file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(first_usage_value(r.stdout_text), 1500);
  EXPECT_EQ(r.stdout_text.find("K"), std::string::npos);
}

TEST(du, du_later_human_readable_overrides_block_size) {
  TempDir tmp;
  tmp.write("file.txt", std::string(1500, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-B", L"1", L"-h", L"file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("1.5K"), std::string::npos);
}

TEST(du, du_block_size_human_readable_word) {
  TempDir tmp;
  tmp.write("file.txt", std::string(1500, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"--block-size=human-readable", L"file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("1.5K"), std::string::npos);
}

TEST(du, du_block_size_si_word) {
  TempDir tmp;
  tmp.write("file.txt", std::string(10000, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"--block-size=si", L"file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("10K"), std::string::npos);
}

TEST(du, du_max_depth_zero_emits_only_root_directory) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "root" / "subdir");
  tmp.write("root/file.txt", "root");
  tmp.write("root/subdir/nested.txt", "nested");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-d", L"0", L"root"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("root") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("subdir") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file.txt") == std::string::npos);
}

TEST(du, du_all_with_max_depth_zero_does_not_emit_child_files) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "root" / "subdir");
  tmp.write("root/file.txt", "root");
  tmp.write("root/subdir/nested.txt", "nested");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-a", L"-d", L"0", L"root"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("root") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file.txt") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("nested.txt") == std::string::npos);
}

TEST(du, du_total_with_bytes) {
  TempDir tmp;
  tmp.write("one.txt", "abc");
  tmp.write("two.txt", "defg");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-b", L"-c", L"one.txt", L"two.txt"});

  TEST_LOG_CMD_LIST("du.exe", L"-b", L"-c", L"one.txt", L"two.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("du.exe -b -c output", r.stdout_text);

  auto values = line_usage_values(r.stdout_text);
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(values.size() >= 3u);
  if (!values.empty()) {
    EXPECT_EQ(values.back(), 7);
  }
  EXPECT_NE(r.stdout_text.find("total"), std::string::npos);
}

TEST(du, du_invalid_block_size_fails) {
  TempDir tmp;
  tmp.write("file.txt", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-B", L"bogus", L"file.txt"});

  TEST_LOG_CMD_LIST("du.exe", L"-B", L"bogus", L"file.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("du.exe invalid block size stderr", r.stderr_text);

  EXPECT_NE(r.exit_code, 0);
}

TEST(du, du_inodes) {
  TempDir tmp;
  tmp.write("file1.txt", "abc");
  tmp.write("file2.txt", "def");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"--inodes", L"."});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // --inodes should show file count instead of size
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(du, du_one_file_system) {
  TempDir tmp;
  tmp.write("file.txt", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-x", L"."});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // -x should stay on same filesystem
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(du, du_separate_dirs) {
  TempDir tmp;
  tmp.write("file.txt", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-S", L"."});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // -S should not include subdirectory sizes
}

TEST(du, du_dereference) {
  TempDir tmp;
  tmp.write("file.txt", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-L", L"."});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // -L should follow symlinks
}

TEST(du, du_time) {
  TempDir tmp;
  tmp.write("file.txt", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"--time", L"."});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // --time should show timestamps
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(du, du_null_separator) {
  TempDir tmp;
  tmp.write("file.txt", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-0", L"."});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // -0 should use NUL separator
}
