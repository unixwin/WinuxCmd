// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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
  EXPECT_EQ_TEXT(r2.stdout_text, "      2       2      12\n");
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

TEST(wc, wc_chars_count_utf8_codepoint_split_across_read_block) {
  std::string input((64 * 1024) - 1, 'a');
  input.append("\xC3\xA9\n", 3);

  Pipeline chars;
  chars.set_stdin(input);
  chars.add(L"wc.exe", {L"-m"});
  auto chars_result = chars.run();

  Pipeline bytes;
  bytes.set_stdin(input);
  bytes.add(L"wc.exe", {L"-c"});
  auto bytes_result = bytes.run();

  EXPECT_EQ(chars_result.exit_code, 0);
  EXPECT_EQ(bytes_result.exit_code, 0);
  EXPECT_EQ_TEXT(chars_result.stdout_text, "65537\n");
  EXPECT_EQ_TEXT(bytes_result.stdout_text, "65538\n");
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

  EXPECT_EQ_TEXT(r.stdout_text, "      2       2      12\n");
}

TEST(wc, wc_file_output_uses_gnu_number_alignment) {
  TempDir tmp;
  tmp.write("a.txt", "hello\nworld\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"wc.exe", {L"a.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, " 2  2 12 a.txt\n");
}

TEST(wc, wc_multiple_files_align_single_count_and_total) {
  TempDir tmp;
  tmp.write("a.txt", "hello\nworld\n");
  tmp.write("b.txt", "one\ntwo\nthree\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"wc.exe", {L"-l", L"a.txt", L"b.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, " 2 a.txt\n 3 b.txt\n 5 total\n");
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

TEST(wc, wc_files0_from_empty_file_is_an_error) {
  TempDir tmp;
  tmp.write_bytes("list.bin", {});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"wc.exe", {L"--files0-from", L"list.bin"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(wc, wc_files0_from_empty_stdin_is_an_error) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("");
  p.add(L"wc.exe", {L"--files0-from", L"-"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(wc, wc_files0_from_rejects_zero_length_names) {
  TempDir tmp;
  tmp.write("a.txt", "one\n");
  tmp.write_bytes("list.bin", {'a', '.', 't', 'x', 't', '\0', '\0'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"wc.exe", {L"--files0-from", L"list.bin"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.find("wc: invalid zero-length file name") !=
              std::string::npos);
}

TEST(wc, wc_files0_from_stdin_rejects_dash_name) {
  TempDir tmp;
  tmp.write("a.txt", "one\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin(std::string("a.txt\0-\0", 8));
  p.add(L"wc.exe", {L"--files0-from", L"-"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.find("wc: when reading file names from stdin, no "
                                 "file name of '-' allowed") !=
              std::string::npos);
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

TEST(wc, wc_files0_from_reports_gnu_shaped_missing_list_diagnostic) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"wc.exe", {L"--files0-from", L"missing.bin"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(
      r.stderr_text.find("wc: missing.bin: No such file or directory") !=
      std::string::npos);
}

TEST(wc, wc_files0_from_reports_directory_list_input) {
  TempDir tmp;
  tmp.mkdir("indir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"wc.exe", {L"--files0-from", L"indir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.find("wc: indir: Is a directory") !=
              std::string::npos);
}

TEST(wc, wc_reports_gnu_shaped_missing_input_diagnostic) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"wc.exe", {L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(
      r.stderr_text.find("wc: missing.txt: No such file or directory") !=
      std::string::npos);
}

TEST(wc, wc_mixed_success_and_failure_still_prints_total_for_multiple_inputs) {
  TempDir tmp;
  tmp.write("a.txt", "one\ntwo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"wc.exe", {L"-l", L"a.txt", L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.find("2 a.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("2 total") != std::string::npos);
  EXPECT_TRUE(
      r.stderr_text.find("wc: missing.txt: No such file or directory") !=
      std::string::npos);
}

TEST(wc, wc_reports_is_a_directory_for_directory_input) {
  TempDir tmp;
  tmp.mkdir("indir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"wc.exe", {L"indir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.find("wc: indir: Is a directory") !=
              std::string::npos);
}
