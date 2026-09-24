// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(comm, comm_basic) {
  TempDir tmp;
  tmp.write("file1.txt", "apple\nbanana\ncherry\n");
  tmp.write("file2.txt", "banana\ndate\nfig\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"comm.exe", {L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Column 1: lines only in file1 (apple, cherry)
  // Column 2: lines only in file2 (date, fig)
  // Column 3: lines in both (banana)
  EXPECT_TRUE(r.stdout_text.find("apple") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("cherry") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("date") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("fig") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("banana") != std::string::npos);
}

TEST(comm, comm_suppress_column1) {
  TempDir tmp;
  tmp.write("file1.txt", "apple\nbanana\n");
  tmp.write("file2.txt", "banana\ndate\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"comm.exe", {L"-1", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Column 1 suppressed
  EXPECT_TRUE(r.stdout_text.find("banana") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("date") != std::string::npos);
}

TEST(comm, comm_total_counts_all_columns) {
  TempDir tmp;
  tmp.write("file1.txt", "apple\nbanana\ncherry\n");
  tmp.write("file2.txt", "banana\ncherry\ndate\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"comm.exe", {L"--total", L"-123", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1\t1\t2\ttotal\n");
}

TEST(comm, comm_check_order_rejects_unsorted_input) {
  TempDir tmp;
  tmp.write("file1.txt", "banana\napple\n");
  tmp.write("file2.txt", "apple\nbanana\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"comm.exe", {L"--check-order", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
}

TEST(comm, comm_nocheck_order_accepts_unsorted_input) {
  TempDir tmp;
  tmp.write("file1.txt", "banana\napple\n");
  tmp.write("file2.txt", "apple\nbanana\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"comm.exe", {L"--nocheck-order", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(comm, comm_default_order_check_warns_after_unpairable_disorder) {
  TempDir tmp;
  tmp.write("file1.txt", "banana\napple\n");
  tmp.write("file2.txt", "carrot\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"comm.exe", {L"file1.txt", L"file2.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "banana\napple\n\tcarrot\n");
  EXPECT_TRUE(r.stderr_text.find("comm: file 1 is not in sorted order") !=
              std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("comm: input is not in sorted order") !=
              std::string::npos);
}

TEST(comm, comm_default_order_check_all_pairable_disorder_succeeds) {
  TempDir tmp;
  tmp.write("file1.txt", "banana\napple\n");
  tmp.write("file2.txt", "banana\napple\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"comm.exe", {L"file1.txt", L"file2.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "\t\tbanana\n\t\tapple\n");
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(comm, comm_rejects_multiple_different_output_delimiters) {
  TempDir tmp;
  tmp.write("file1.txt", "apple\n");
  tmp.write("file2.txt", "banana\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"comm.exe", {L"--output-delimiter=:", L"--output-delimiter=,",
                      L"file1.txt", L"file2.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.find("multiple output delimiters specified") !=
              std::string::npos);
}
TEST(comm, comm_empty_output_delimiter_is_nul) {
  TempDir tmp;
  tmp.write("file1.txt", "apple\nbanana\n");
  tmp.write("file2.txt", "banana\ncherry\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"comm.exe", {L"--output-delimiter=", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  std::string expected = "apple\n";
  expected += std::string("\0\0banana\n", 9);
  expected += std::string("\0cherry\n", 8);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, expected);
}

TEST(comm, comm_separate_empty_output_delimiter_is_nul) {
  TempDir tmp;
  tmp.write("file1.txt", "apple\nbanana\n");
  tmp.write("file2.txt", "banana\ncherry\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"comm.exe", {L"--output-delimiter", L"", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  std::string expected = "apple\n";
  expected += std::string("\0\0banana\n", 9);
  expected += std::string("\0cherry\n", 8);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, expected);
}

TEST(comm, comm_zero_terminated_records) {
  TempDir tmp;
  tmp.write("file1.bin", std::string("apple\0banana\0", 13));
  tmp.write("file2.bin", std::string("banana\0cherry\0", 14));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"comm.exe", {L"-z", L"-12", L"file1.bin", L"file2.bin"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, std::string("banana\0", 7));
}

TEST(comm, comm_newline_mode_trims_trailing_cr_from_crlf_records) {
  TempDir tmp;
  tmp.write_bytes("file1.txt", {'a', 'p', 'p', 'l', 'e', '\r', '\n', 'b', 'a',
                                'n', 'a', 'n', 'a', '\r', '\n'});
  tmp.write_bytes("file2.txt", {'b', 'a', 'n', 'a', 'n', 'a', '\r', '\n', 'c',
                                'h', 'e', 'r', 'r', 'y', '\r', '\n'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"comm.exe", {L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "apple\n\t\tbanana\n\tcherry\n");
}

TEST(comm, comm_missing_input_reports_no_such_file) {
  TempDir tmp;
  tmp.write("file2.txt", "banana\ndate\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"comm.exe", {L"missing.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "comm: cannot open 'missing.txt' for reading: No such file "
                  "or directory") != std::string::npos);
}

TEST(comm, comm_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.write("file2.txt", "banana\ndate\n");
  tmp.mkdir("indir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"comm.exe", {L"indir", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "comm: cannot open 'indir' for reading: Is a directory") !=
              std::string::npos);
}

TEST(comm, comm_missing_operands_report_help_hint) {
  Pipeline p;
  p.add(L"comm.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "comm: missing operand\nTry 'comm --help' for more information.\n");
}

TEST(comm, comm_extra_operand_reports_help_hint) {
  Pipeline p;
  p.add(L"comm.exe", {L"a", L"b", L"c"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "comm: extra operand 'c'\nTry 'comm --help' for more information.\n");
}

TEST(comm, comm_check_order_keeps_output_before_disorder) {
  // [GNU] comm.c: with --check-order the merge up to the disordered line is
  // printed, then the offending file is reported without emitting the line.
  TempDir tmp;
  tmp.write("file1.txt", "a\nb\nc\n");
  tmp.write("file2.txt", "a\nc\nb\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"comm.exe", {L"--check-order", L"file1.txt", L"file2.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "\t\ta\nb\n\t\tc\n");
  EXPECT_EQ_TEXT(r.stderr_text, "comm: file 2 is not in sorted order\n");
}

TEST(comm, comm_missing_operand_after_reports_operand) {
  // [GNU] comm.c: "missing operand after %s" quotes the last operand.
  Pipeline p;
  p.add(L"comm.exe", {L"file1.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "comm: missing operand after 'file1.txt'\nTry 'comm --help' for more "
      "information.\n");
}
