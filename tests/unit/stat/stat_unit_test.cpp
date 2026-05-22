/*
 *  Copyright © 2026 [caomengxuan666]
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
 *  - File: stat_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(stat, stat_file) {
  TempDir tmp;
  tmp.write("test.txt", "hello world\n");

  TEST_LOG_FILE_CONTENT("test.txt", "hello world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"stat.exe", {L"test.txt"});

  TEST_LOG_CMD_LIST("stat.exe", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("stat output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(stat, stat_directory) {
  TempDir tmp;
  tmp.write("test.txt", "test");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"stat.exe", {L"test.txt"});

  TEST_LOG_CMD_LIST("stat.exe", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("stat directory output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(stat, stat_terse) {
  TempDir tmp;
  tmp.write("test.txt", "test");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"stat.exe", {L"-t", L"test.txt"});

  TEST_LOG_CMD_LIST("stat.exe", L"-t", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("stat terse output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(stat, stat_wildcard) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");
  tmp.write("file2.txt", "content2");
  tmp.write("other.log", "log");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"stat.exe", {L"*.txt"});

  TEST_LOG_CMD_LIST("stat.exe", L"*.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("stat wildcard output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("file1.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file2.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("other.log") == std::string::npos);
}

TEST(stat, stat_printf_format) {
  TempDir tmp;
  tmp.write("test.txt", "data");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"stat.exe", {L"--printf", L"%n:%s\\n", L"test.txt"});

  TEST_LOG_CMD_LIST("stat.exe", L"--printf", L"%n:%s\\n", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("stat printf output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "test.txt:4\n");
}

TEST(stat, stat_format_appends_newline) {
  TempDir tmp;
  tmp.write("test.txt", "data");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"stat.exe", {L"-c", L"%n:%s", L"test.txt"});

  TEST_LOG_CMD_LIST("stat.exe", L"-c", L"%n:%s", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("stat format output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "test.txt:4\n");
}

TEST(stat, stat_format_common_fields) {
  TempDir tmp;
  tmp.write("test.txt", "data");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"stat.exe", {L"-c", L"%F|%a|%A|%h|%Y", L"test.txt"});

  TEST_LOG_CMD_LIST("stat.exe", L"-c", L"%F|%a|%A|%h|%Y", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("stat common fields output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.starts_with("regular file|666|-rw-rw-rw-|"));
  EXPECT_TRUE(r.stdout_text.ends_with("\n"));

  auto last_separator = r.stdout_text.find_last_of('|');
  EXPECT_TRUE(last_separator != std::string::npos);
  if (last_separator != std::string::npos) {
    auto timestamp = r.stdout_text.substr(last_separator + 1);
    if (!timestamp.empty() && timestamp.back() == '\n') {
      timestamp.pop_back();
    }
    EXPECT_FALSE(timestamp.empty());
    EXPECT_TRUE(std::ranges::all_of(timestamp, [](char ch) {
      return std::isdigit(static_cast<unsigned char>(ch)) != 0;
    }));
  }
}

TEST(stat, stat_file_system_format) {
  TempDir tmp;
  tmp.write("test.txt", "data");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"stat.exe", {L"-f", L"-c", L"%n|%s|%S|%T", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.starts_with("test.txt|"));
  EXPECT_TRUE(r.stdout_text.ends_with("\n"));

  auto first_separator = r.stdout_text.find('|');
  auto second_separator = r.stdout_text.find('|', first_separator + 1);
  auto third_separator = r.stdout_text.find('|', second_separator + 1);
  EXPECT_TRUE(first_separator != std::string::npos);
  EXPECT_TRUE(second_separator != std::string::npos);
  EXPECT_TRUE(third_separator != std::string::npos);
  if (first_separator != std::string::npos &&
      second_separator != std::string::npos &&
      third_separator != std::string::npos) {
    auto block_size = r.stdout_text.substr(
        first_separator + 1, second_separator - first_separator - 1);
    auto fundamental_size = r.stdout_text.substr(
        second_separator + 1, third_separator - second_separator - 1);
    EXPECT_FALSE(block_size.empty());
    EXPECT_EQ_TEXT(block_size, fundamental_size);
  }
}

TEST(stat, stat_printf_interprets_common_backslash_escapes) {
  TempDir tmp;
  tmp.write("test.txt", "data");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"stat.exe", {L"--printf", L"%n\\t\\011\\010", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_BYTES(r.stdout_text, "test.txt\t\t\b");
}
