// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(join, join_basic) {
  TempDir tmp;
  tmp.write("file1.txt", "  1   apple green\n2\tbanana\n3 cherry\n");
  tmp.write("file2.txt", "1 red\n2 yellow\n3 red\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"join.exe", {L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text,
            "1 apple green red\n2 banana yellow\n3 cherry red\n");
}

TEST(join, join_single_file) {
  TempDir tmp;
  tmp.write("file1.txt", "1 apple\n2 banana\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"join.exe", {L"file1.txt", L"file1.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should match with itself
  EXPECT_TRUE(r.stdout_text.find("1 apple") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("2 banana") != std::string::npos);
}

TEST(join, join_outputs_unpaired_lines_with_auto_format) {
  TempDir tmp;
  tmp.write("file1.txt", "k a b\nu only\n");
  tmp.write("file2.txt", "k x\nv y z\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"join.exe", {L"-a", L"1", L"-a", L"2", L"-e", L"NA", L"-o", L"auto",
                      L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "k a b x\nu only NA NA\nv NA NA y\n");
}

TEST(join, join_custom_output_and_only_unpaired) {
  TempDir tmp;
  tmp.write("left.csv", "id,name,role\n1,Ada,\n2,Ben,ops\n");
  tmp.write("right.csv", "id,team\n1,core\n3,infra\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"join.exe", {L"-t", L",", L"-v", L"1", L"-e", L"NA", L"-o",
                      L"0,1.2,2.2", L"left.csv", L"right.csv"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "2,Ben,NA\n");
}

TEST(join, join_ignore_case) {
  TempDir tmp;
  tmp.write("file1.txt", "Key left\n");
  tmp.write("file2.txt", "key right\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"join.exe", {L"-i", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "Key left right\n");
}

TEST(join, join_header_drives_auto_output_fields) {
  TempDir tmp;
  tmp.write("file1.txt", "id name\n1 Ada\n2 Ben Smith\n");
  tmp.write("file2.txt", "id team\n1 core\n3 infra\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"join.exe", {L"--header", L"-a", L"1", L"-a", L"2", L"-e", L"NA",
                      L"-o", L"auto", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "id name team\n1 Ada core\n2 Ben NA\n3 NA infra\n");
}

TEST(join, join_zero_terminated_records) {
  TempDir tmp;
  tmp.write("file1.bin", std::string("1 a\0x left\0", 11));
  tmp.write("file2.bin", std::string("1 b\0x right\0", 12));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"join.exe", {L"-z", L"file1.bin", L"file2.bin"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, std::string("1 a b\0x left right\0", 19));
}

TEST(join, join_mixed_lf_and_crlf_records_match_in_text_mode) {
  TempDir tmp;
  tmp.write_bytes("file1.txt", {'a', ' ', 'l', 'e', 'f', 't', '\n'});
  tmp.write_bytes("file2.txt", {'a', ' ', 'r', 'i', 'g', 'h', 't', '\r', '\n'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"join.exe", {L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a left right\n");
}

TEST(join, join_reports_gnu_shaped_missing_input_diagnostic) {
  TempDir tmp;
  tmp.write("file2.txt", "a right\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"join.exe", {L"missing.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(
      r.stderr_text.find("join: missing.txt: No such file or directory") !=
      std::string::npos);
}

TEST(join, join_reports_is_a_directory_for_directory_input) {
  TempDir tmp;
  tmp.mkdir("dir1");
  tmp.write("file2.txt", "a right\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"join.exe", {L"dir1", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.find("join: dir1: Is a directory") !=
              std::string::npos);
}

TEST(join, join_missing_operands_report_help_hint) {
  Pipeline p;
  p.add(L"join.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "join: missing operand\nTry 'join --help' for more information.\n");
}

TEST(join, join_extra_operand_reports_help_hint) {
  Pipeline p;
  p.add(L"join.exe", {L"a", L"b", L"c"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "join: extra operand 'c'\nTry 'join --help' for more information.\n");
}

TEST(join, join_reports_disorder_with_file_and_line_after_unpairable) {
  // [GNU] join.c: earlier joins are emitted; once an unpairable line has
  // been seen a disorder is diagnosed with file, line number, and content.
  TempDir tmp;
  tmp.write("u1.txt", "b 1\na 2\n");
  tmp.write("u2.txt", "a x\nb y\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"join.exe", {L"u1.txt", L"u2.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "b 1 y\n");
  EXPECT_TRUE(r.stderr_text.find("join: u1.txt:2: is not sorted: a 2") !=
              std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("join: input is not in sorted order") !=
              std::string::npos);
}

TEST(join, join_check_order_makes_first_disorder_fatal) {
  // [GNU] --check-order: no output after the offending line is reached.
  TempDir tmp;
  tmp.write("u1.txt", "b 1\na 2\n");
  tmp.write("u2.txt", "a x\nb y\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"join.exe", {L"--check-order", L"u1.txt", L"u2.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text, "join: u1.txt:2: is not sorted: a 2\n");
}

TEST(join, join_disorder_before_first_unpairable_is_silent) {
  // [GNU] join.c: the line read to break the first comparison is checked
  // before seen_unpairable is set, so this disorder is never diagnosed.
  TempDir tmp;
  tmp.write("p1.txt", "b\na\n");
  tmp.write("p2.txt", "c\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"join.exe", {L"p1.txt", L"p2.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(join, join_interleaves_unpaired_output_in_merge_order) {
  // [GNU] join.c: sequential merge; unpairable lines appear at the position
  // the merge cursor reaches them, not grouped per file.
  TempDir tmp;
  tmp.write("m1.txt", "b 1\n");
  tmp.write("m2.txt", "a x\nc y\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"join.exe", {L"-a", L"1", L"-a", L"2", L"m1.txt", L"m2.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a x\nb 1\nc y\n");
}

TEST(join, join_key_option_selects_join_field_of_both_files) {
  // [EXTENSION] #1070: -k N is equivalent to "-1 N -2 N".
  TempDir tmp;
  tmp.write("k1.txt", "x a 1\nx b 2\n");
  tmp.write("k2.txt", "y a r\ny b s\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"join.exe", {L"-k", L"2", L"k1.txt", L"k2.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a x 1 y r\nb x 2 y s\n");

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"join.exe", {L"--key", L"2", L"k1.txt", L"k2.txt"});
  auto r2 = p2.run();

  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_EQ_TEXT(r2.stdout_text, "a x 1 y r\nb x 2 y s\n");
}

TEST(join, join_missing_operand_after_reports_last_argument) {
  // [GNU] join.c: "missing operand after %s" quotes the last argument.
  Pipeline p;
  p.add(L"join.exe", {L"f1.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "join: missing operand after 'f1.txt'\nTry 'join --help' for more "
      "information.\n");
}
