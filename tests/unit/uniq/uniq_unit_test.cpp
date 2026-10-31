// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(uniq, uniq_basic_adjacent_behavior) {
  TempDir tmp;
  tmp.write("a.txt", "a\na\nb\na\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"uniq.exe", {L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\nb\na\n");
}

TEST(uniq, uniq_reads_utf8_filename) {
  TempDir tmp;
  const std::wstring name = L"\x6D4B\x8BD5.txt";
  {
    std::ofstream out(tmp.path / name, std::ios::binary);
    out << "x\nx\ny\n";
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"uniq.exe", {name});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "x\ny\n");
}

TEST(uniq, uniq_preserves_crlf_input_records) {
  // GNU uniq preserves carriage returns as input bytes.
  TempDir tmp;
  tmp.write_bytes("a.txt", {'a', '\r', '\n', 'a', '\r', '\n', 'b', '\r', '\n',
                            'a', '\r', '\n'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"uniq.exe", {L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "a\r\nb\r\na\r\n");
}

TEST(uniq, uniq_count) {
  TempDir tmp;
  tmp.write("a.txt", "x\nx\ny\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"uniq.exe", {L"-c", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("2 x") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("1 y") != std::string::npos);
}

TEST(uniq, uniq_repeated_and_unique_filters) {
  TempDir tmp;
  tmp.write("a.txt", "a\na\nb\nc\nc\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"uniq.exe", {L"-d", L"a.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);
  EXPECT_EQ_TEXT(r1.stdout_text, "a\nc\n");

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"uniq.exe", {L"-u", L"a.txt"});
  auto r2 = p2.run();
  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_EQ_TEXT(r2.stdout_text, "b\n");
}

TEST(uniq, uniq_ignore_case_skip_fields_chars) {
  TempDir tmp;
  tmp.write("a.txt", "id1 Same\nid2 same\nid3 diff\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"uniq.exe", {L"-i", L"-f", L"1", L"-s", L"1", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "id1 Same\nid3 diff\n");
}

TEST(uniq, uniq_all_repeated) {
  TempDir tmp;
  tmp.write("a.txt", "a\na\nb\nc\nc\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"uniq.exe", {L"-D", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\na\nc\nc\n");
}

TEST(uniq, uniq_combines_duplicate_unique_output_flags_like_gnu) {
  TempDir tmp;
  tmp.write("a.txt", "a\na\nb\nc\nc\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"uniq.exe", {L"-d", L"-u", L"a.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);
  EXPECT_TRUE(r1.stdout_text.empty());

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"uniq.exe", {L"-D", L"-u", L"a.txt"});
  auto r2 = p2.run();
  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_EQ_TEXT(r2.stdout_text, "a\nc\n");
}

TEST(uniq, uniq_rejects_count_with_all_repeated_like_gnu) {
  TempDir tmp;
  tmp.write("a.txt", "a\na\nb\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"uniq.exe", {L"-c", L"-D", L"a.txt"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(
      r.stderr_text.find("printing all duplicated lines and repeat counts") !=
      std::string::npos);
}
TEST(uniq, uniq_group_default_keeps_input_operand) {
  TempDir tmp;
  tmp.write("a.txt", "a\na\nb\nc\nc\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"uniq.exe", {L"--group", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\na\n\nb\n\nc\nc\n");
}

TEST(uniq, uniq_group_prepend_method) {
  TempDir tmp;
  tmp.write("a.txt", "a\na\nb\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"uniq.exe", {L"--group=prepend", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "\na\na\n\nb\n");
}

TEST(uniq, uniq_all_repeated_rejects_group_only_methods) {
  TempDir tmp;
  tmp.write("a.txt", "a\na\nb\nc\nc\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"uniq.exe", {L"--all-repeated=append", L"a.txt"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(
      r.stderr_text.find("invalid argument append for --all-repeated") !=
      std::string::npos);
}

TEST(uniq, uniq_group_is_mutually_exclusive_with_output_options) {
  TempDir tmp;
  tmp.write("a.txt", "a\na\nb\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"uniq.exe", {L"--group", L"-c", L"a.txt"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(
      r.stderr_text.find("--group is mutually exclusive with -c/-d/-D/-u") !=
      std::string::npos);
}

TEST(uniq, uniq_group_rejects_invalid_method) {
  TempDir tmp;
  tmp.write("a.txt", "a\na\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"uniq.exe", {L"--group=bad", L"a.txt"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  // [GNU] uniq.c XARGMATCH ("--group", ...) over the table at
  // uniq.c (prepend/append/separate/both): the argmatch shape is
  // "invalid argument '<v>' for '--group'" plus the valid list;
  // no "invalid grouping method" text exists in GNU uniq.
  EXPECT_TRUE(r.stderr_text.find("invalid argument 'bad' for '--group'") !=
              std::string::npos);
}

TEST(uniq, uniq_reports_gnu_shaped_missing_input_diagnostic) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"uniq.exe", {L"missing.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(
      r.stderr_text.find("uniq: missing.txt: No such file or directory") !=
      std::string::npos);
}

TEST(uniq, uniq_reports_is_a_directory_for_directory_input) {
  TempDir tmp;
  tmp.mkdir("indir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"uniq.exe", {L"indir"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.find("uniq: indir: Is a directory") !=
              std::string::npos);
}

TEST(uniq, uniq_file_with_trailing_separator_reports_not_directory) {
  TempDir tmp;
  tmp.write("file.txt", "a\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"uniq.exe", {L"file.txt/"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.find("uniq: file.txt/: Not a directory") !=
              std::string::npos);
}

TEST(uniq, uniq_group_both_separates_groups_with_single_blank_lines) {
  // [GNU] uniq.c: --group=both emits exactly one separator before and one
  // after each group, never two blank lines between adjacent groups.
  TempDir tmp;
  tmp.write("a.txt", "a\na\nb\nb\nc\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"uniq.exe", {L"--group=both", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "\na\na\n\nb\nb\n\nc\n\n");
}

// [#1212] fd 0 is text mode by default, so a pipe stopped at the first 0x1A
// and disagreed with the file operand. Both forms must be byte-identical.
TEST(uniq, uniq_stdin_ctrlz_matches_file_operand_issue1212) {
  const std::string payload(
      "AAAA\x1a"
      "BBBB\n",
      10);
  TempDir tmp;
  tmp.write_bytes("p.bin",
                  {'A', 'A', 'A', 'A', 0x1a, 'B', 'B', 'B', 'B', '\n'});

  Pipeline by_file;
  by_file.set_cwd(tmp.wpath());
  by_file.add(L"uniq.exe", {L"p.bin"});
  auto rf = by_file.run();

  Pipeline by_stdin;
  by_stdin.set_stdin(payload);
  by_stdin.add(L"uniq.exe", {});
  auto rs = by_stdin.run();

  EXPECT_EQ(rs.exit_code, 0);
  EXPECT_FALSE(rf.stdout_text.empty());
  EXPECT_EQ(rs.stdout_text, rf.stdout_text);
}

// #1306: -D is a flag inside a cluster, so -Dc means -D plus -c and
// reports the count/all-repeated conflict instead of treating "c" as
// -D's value.
TEST(uniq, uniq_cluster_flag_does_not_swallow_next_flag) {
  Pipeline p;
  p.set_stdin("dup\ndup\n");
  p.add(L"uniq.exe", {L"-Dc"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("printing all duplicated lines and repeat counts") !=
              std::string::npos);
}

// #1306: digits inside a cluster are the -NUM check-chars count, so
// -D5 compares only the first 5 characters.
TEST(uniq, uniq_cluster_digits_are_check_chars) {
  Pipeline p;
  p.set_stdin("abcdefX\nabcdefY\n");
  p.add(L"uniq.exe", {L"-D5"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "abcdefX\nabcdefY\n");
}

// #1374: in WINUX_SHELL_GLOB=native mode the shell leaves expansion to
// the command; uniq must expand its input operand through the shared
// glob path like cat does, instead of failing on the literal pattern.
TEST(uniq, uniq_native_glob_expands_input_operand_issue1374) {
  TempDir tmp;
  tmp.write("u.dat", "dup\ndup\nuniq\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"WINUX_SHELL_GLOB", L"native");
  p.add(L"uniq.exe", {L"u.*"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "dup\nuniq\n");
}

