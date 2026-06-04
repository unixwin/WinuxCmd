/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the “Software”), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: wc_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(wc, wc_direct_input) {
  Pipeline p;
  p.set_stdin("hello\nworld\n");
  p.add(L"wc.exe", {L"-l"});

  TEST_LOG_CMD_LIST("wc.exe", L"-l");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  std::cout << "wc.exe -l direct test:" << std::endl;
  TEST_LOG("Output", r.stdout_text);

  Pipeline p2;
  p2.set_stdin("hello\nworld\n");
  p2.add(L"wc.exe", {});

  TEST_LOG_CMD_LIST("wc.exe");

  auto r2 = p2.run();

  TEST_LOG_EXIT_CODE(r2);
  TEST_LOG("wc.exe (no args) output", r2.stdout_text);

  EXPECT_EQ_TEXT(r.stdout_text, "2\n");
  EXPECT_EQ_TEXT(r2.stdout_text, "2 2 12\n");
}

TEST(wc, wc_with_options) {
  Pipeline p;
  p.set_stdin("hello\nworld\n");
  p.add(L"wc.exe", {L"-c"});

  TEST_LOG_CMD_LIST("wc.exe", L"-c");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("wc.exe -c output", r.stdout_text);

  Pipeline p2;
  p2.set_stdin("hello\nworld\n");
  p2.add(L"wc.exe", {L"-w"});

  TEST_LOG_CMD_LIST("wc.exe", L"-w");

  auto r2 = p2.run();

  TEST_LOG_EXIT_CODE(r2);
  TEST_LOG("wc.exe -w output", r2.stdout_text);

  Pipeline p3;
  p3.set_stdin("hello\nworld\n");
  p3.add(L"wc.exe", {L"-m"});

  TEST_LOG_CMD_LIST("wc.exe", L"-m");

  auto r3 = p3.run();

  TEST_LOG_EXIT_CODE(r3);
  TEST_LOG("wc.exe -m output", r3.stdout_text);

  Pipeline p4;
  p4.set_stdin("hello\nworld\n");
  p4.add(L"wc.exe", {L"-L"});

  TEST_LOG_CMD_LIST("wc.exe", L"-L");

  auto r4 = p4.run();

  TEST_LOG_EXIT_CODE(r4);
  TEST_LOG("wc.exe -L output", r4.stdout_text);

  EXPECT_EQ_TEXT(r.stdout_text, "12\n");
  EXPECT_EQ_TEXT(r2.stdout_text, "2\n");
  EXPECT_EQ_TEXT(r3.stdout_text, "12\n");
  EXPECT_EQ_TEXT(r4.stdout_text, "5\n");
}

TEST(wc, wc_chars_count_utf8_codepoints_not_bytes) {
  Pipeline chars;
  chars.set_stdin(std::string("\xC3\xA9\n", 3));
  chars.add(L"wc.exe", {L"-m"});
  auto chars_result = chars.run();

  Pipeline bytes;
  bytes.set_stdin(std::string("\xC3\xA9\n", 3));
  bytes.add(L"wc.exe", {L"-c"});
  auto bytes_result = bytes.run();

  EXPECT_EQ(chars_result.exit_code, 0);
  EXPECT_EQ(bytes_result.exit_code, 0);
  EXPECT_EQ_TEXT(chars_result.stdout_text, "2\n");
  EXPECT_EQ_TEXT(bytes_result.stdout_text, "3\n");
}

TEST(wc, wc_max_line_length_expands_tabs) {
  Pipeline p;
  p.set_stdin("a\tb\n");
  p.add(L"wc.exe", {L"-L"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "9\n");
}

TEST(wc, wc_debug_reports_counting_path_without_changing_stdout) {
  Pipeline p;
  p.set_stdin("alpha\nbeta\n");
  p.add(L"wc.exe", {L"--debug", L"-l"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "2\n");
  EXPECT_TRUE(r.stderr_text.find("wc: debug:") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("line count implementation") !=
              std::string::npos);
}

TEST(wc, wc_combined_options) {
  Pipeline p;
  p.set_stdin("hello\nworld\n");
  p.add(L"wc.exe", {L"-l", L"-w", L"-c"});

  TEST_LOG_CMD_LIST("wc.exe", L"-l", L"-w", L"-c");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("wc.exe -l -w -c output", r.stdout_text);

  EXPECT_EQ_TEXT(r.stdout_text, "2 2 12\n");
}

TEST(wc, wc_wildcard) {
  TempDir tmp;
  tmp.write("file1.txt", "hello\nworld\n");
  tmp.write("file2.txt", "foo\nbar\nbaz\n");
  tmp.write("other.log", "line1\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"wc.exe", {L"-l", L"*.txt"});

  TEST_LOG_CMD_LIST("wc.exe", L"-l", L"*.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("wc output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // file1.txt has 2 lines, file2.txt has 3 lines
  EXPECT_TRUE(r.stdout_text.find("file1.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file2.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("other.log") == std::string::npos);
}

TEST(wc, wc_counts_newlines_only_for_lines) {
  TempDir tmp;
  tmp.write("a.txt", "one\ntwo");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"wc.exe", {L"-l", L"a.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1 a.txt\n");
}

TEST(wc, wc_total_only_omits_label) {
  TempDir tmp;
  tmp.write("a.txt", "one\n");
  tmp.write("b.txt", "one\ntwo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"wc.exe", {L"-l", L"--total=only", L"*.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "3\n");
}

TEST(wc, wc_files0_from_reads_nul_terminated_names) {
  TempDir tmp;
  tmp.write("a.txt", "hello\nworld\n");
  tmp.write("b.txt", "foo\nbar\nbaz\n");
  tmp.write_bytes("list.bin", {'a', '.', 't', 'x', 't', '\0', 'b', '.', 't',
                               'x', 't', '\0'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"wc.exe", {L"-l", L"--files0-from", L"list.bin"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("2 a.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("3 b.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("5 total") != std::string::npos);
}

TEST(wc, wc_files0_from_stdin_reads_nul_terminated_names) {
  TempDir tmp;
  tmp.write("a.txt", "one\n");
  tmp.write("b.txt", "two\nthree\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin(std::string("a.txt\0b.txt\0", 12));
  p.add(L"wc.exe", {L"-l", L"--files0-from", L"-"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("1 a.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("2 b.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("3 total") != std::string::npos);
}

TEST(wc, wc_files0_from_rejects_named_operands) {
  TempDir tmp;
  tmp.write("a.txt", "one\n");
  tmp.write_bytes("list.bin", {'a', '.', 't', 'x', 't', '\0'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"wc.exe", {L"--files0-from", L"list.bin", L"a.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("--files0-from disallows") !=
              std::string::npos);
}
