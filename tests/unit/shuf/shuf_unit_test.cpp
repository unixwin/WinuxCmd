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
 *  - File: shuf_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

namespace {
std::vector<std::string> split_records(const std::string &text,
                                       char delimiter) {
  std::vector<std::string> records;
  std::string current;
  for (char ch : text) {
    if (ch == delimiter) {
      records.push_back(current);
      current.clear();
    } else {
      current.push_back(ch);
    }
  }
  if (!current.empty()) records.push_back(current);
  return records;
}

int count_char(const std::string &text, char needle) {
  int count = 0;
  for (char ch : text) {
    if (ch == needle) ++count;
  }
  return count;
}
}  // namespace

TEST(shuf, shuf_basic) {
  TempDir tmp;
  tmp.write("test.txt", "line1\nline2\nline3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shuf.exe", {L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // All lines should be present
  EXPECT_TRUE(r.stdout_text.find("line1") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("line2") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("line3") != std::string::npos);
}

TEST(shuf, shuf_stdin) {
  Pipeline p;
  p.set_stdin("line1\nline2\nline3\n");
  p.add(L"shuf.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("line1") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("line2") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("line3") != std::string::npos);
}

TEST(shuf, shuf_echo_mode) {
  Pipeline p;
  p.add(L"shuf.exe", {L"-e", L"a", L"b", L"c"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("a") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("c") != std::string::npos);
}

TEST(shuf, shuf_input_range_and_head_count) {
  Pipeline p;
  p.add(L"shuf.exe", {L"--input-range=4-8", L"--head-count=3"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(count_char(r.stdout_text, '\n'), 3);
  for (const auto &record : split_records(r.stdout_text, '\n')) {
    EXPECT_TRUE(record >= "4");
    EXPECT_TRUE(record <= "8");
  }
}

TEST(shuf, shuf_repeat_allows_repeated_output) {
  Pipeline p;
  p.add(L"shuf.exe", {L"-r", L"-n", L"5", L"-e", L"only"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "only\nonly\nonly\nonly\nonly\n");
}

TEST(shuf, shuf_zero_terminated_reads_and_writes_nul_records) {
  TempDir tmp;
  tmp.write_bytes("nul.txt", {'a', '\0', 'b', '\0', 'c', '\0'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shuf.exe", {L"-z", L"nul.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(count_char(r.stdout_text, '\0'), 3);
  EXPECT_TRUE(r.stdout_text.find("a") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("c") != std::string::npos);
}

TEST(shuf, shuf_trims_cr_from_crlf_records_in_newline_mode) {
  TempDir tmp;
  tmp.write_bytes("crlf.txt",
                  {'l', 'i', 'n', 'e', '1', '\r', '\n', 'l', 'i', 'n', 'e',
                   '2', '\r', '\n', 'l', 'i', 'n', 'e', '3', '\r', '\n'});
  tmp.write_bytes("random.bin", {'s', 'e', 'e', 'd', '1', '2', '3', '4'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shuf.exe", {L"--random-source=random.bin", L"crlf.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text.find('\r'), std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("line1\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("line2\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("line3\n") != std::string::npos);
  EXPECT_EQ(count_char(r.stdout_text, '\n'), 3);
}

TEST(shuf, shuf_output_file_is_written_after_input_is_read) {
  TempDir tmp;
  tmp.write("in.txt", "red\ngreen\nblue\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shuf.exe", {L"-o", L"in.txt", L"in.txt"});

  auto r = p.run();
  const auto output = tmp.read("in.txt");

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "");
  EXPECT_TRUE(output.find("red") != std::string::npos);
  EXPECT_TRUE(output.find("green") != std::string::npos);
  EXPECT_TRUE(output.find("blue") != std::string::npos);
  EXPECT_EQ(count_char(output, '\n'), 3);
}

TEST(shuf, shuf_random_source_makes_shuffle_reproducible) {
  TempDir tmp;
  tmp.write("input.txt", "1\n2\n3\n4\n5\n");
  tmp.write_bytes("random.bin", {'s', 'e', 'e', 'd', '1', '2', '3', '4'});

  Pipeline first;
  first.set_cwd(tmp.wpath());
  first.add(L"shuf.exe", {L"--random-source=random.bin", L"input.txt"});
  auto r1 = first.run();

  Pipeline second;
  second.set_cwd(tmp.wpath());
  second.add(L"shuf.exe", {L"--random-source=random.bin", L"input.txt"});
  auto r2 = second.run();

  EXPECT_EQ(r1.exit_code, 0);
  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_EQ_TEXT(r1.stdout_text, r2.stdout_text);
}

TEST(shuf, shuf_random_source_matches_microsoft_file_head_count_output) {
  TempDir tmp;
  tmp.write("input.txt", "a\nb\nc\nd\ne\n");
  tmp.write_bytes("random.bin", {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shuf.exe",
        {L"--random-source=random.bin", L"-n", L"3", L"input.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "b\na\nc\n");
}

TEST(shuf, shuf_random_source_matches_microsoft_repeat_input_range_output) {
  TempDir tmp;
  tmp.write_bytes("random.bin", {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shuf.exe",
        {L"--random-source=random.bin", L"-r", L"-n", L"8", L"-i", L"1-3"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "2\n1\n1\n1\n1\n3\n1\n1\n");
}

TEST(shuf, shuf_random_source_matches_microsoft_input_range_output) {
  TempDir tmp;
  tmp.write_bytes("random.bin", {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shuf.exe", {L"--random-source=random.bin", L"-i", L"1-5"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "2\n1\n3\n4\n5\n");
}

TEST(shuf, shuf_random_seed_makes_shuffle_reproducible) {
  TempDir tmp;
  tmp.write("input.txt", "1\n2\n3\n4\n5\n");
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shuf.exe", {L"--random-seed=abc", L"input.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "4\n3\n5\n1\n2\n");
}

TEST(shuf, shuf_random_seed_makes_repeat_reproducible) {
  Pipeline p;
  p.add(L"shuf.exe",
        {L"--random-seed=repeat", L"-r", L"-n", L"6", L"-e", L"a", L"b"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\nb\na\nb\nb\nb\n");
}

TEST(shuf, shuf_random_seed_matches_microsoft_head_count_output) {
  TempDir tmp;
  tmp.write("input.txt", "1\n2\n3\n4\n5\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shuf.exe", {L"--random-seed=abc", L"-n", L"3", L"input.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "4\n3\n5\n");
}

TEST(shuf, shuf_random_seed_matches_microsoft_input_range_output) {
  Pipeline p;
  p.add(L"shuf.exe", {L"--random-seed=abc", L"-i", L"4-8"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "7\n6\n8\n4\n5\n");
}

TEST(shuf, shuf_random_seed_matches_microsoft_echo_output) {
  Pipeline p;
  p.add(L"shuf.exe", {L"--random-seed=abc", L"-e", L"a", L"b", L"c"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "c\nb\na\n");
}

TEST(shuf, shuf_repeated_short_head_count_uses_lowest_value) {
  Pipeline p;
  p.add(L"shuf.exe", {L"-n", L"2", L"-n", L"5", L"-i", L"1-10"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(count_char(r.stdout_text, '\n'), 2);
}

TEST(shuf, shuf_repeated_long_head_count_uses_lowest_value) {
  Pipeline p;
  p.add(L"shuf.exe",
        {L"--head-count=2", L"--head-count=5", L"-i", L"1-10"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(count_char(r.stdout_text, '\n'), 2);
}

TEST(shuf, shuf_random_seed_conflicts_with_random_source) {
  TempDir tmp;
  tmp.write("input.txt", "1\n2\n");
  tmp.write_bytes("random.bin", {'a', 'b', 'c'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shuf.exe",
        {L"--random-seed=abc", L"--random-source=random.bin", L"input.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(
      r.stderr_text.find("shuf: the arguments '--random-seed <STRING>' and "
                         "'--random-source <FILE>' are mutually exclusive") !=
      std::string::npos);
}

TEST(shuf, shuf_missing_input_reports_no_such_file) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shuf.exe", {L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "shuf: cannot open 'missing.txt' for reading: No such file "
                  "or directory") != std::string::npos);
}

TEST(shuf, shuf_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("indir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shuf.exe", {L"indir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "shuf: cannot open 'indir' for reading: Is a directory") !=
              std::string::npos);
}
