// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(expand, expand_basic) {
  TempDir tmp;
  tmp.write("test.txt", "hello\tworld\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"expand.exe", {L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Tab should be converted to spaces (default 8 spaces)
  EXPECT_TRUE(r.stdout_text.find("hello") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("world") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("\t") == std::string::npos);
}

TEST(expand, expand_custom_tab) {
  TempDir tmp;
  tmp.write("test.txt", "hello\tworld\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"expand.exe", {L"-t", L"4", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("\t") == std::string::npos);
}

TEST(expand, expand_tab_list) {
  Pipeline p;
  p.set_stdin("a\tb\tc\n");
  p.add(L"expand.exe", {L"-t", L"3,5"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a  b c\n");
}

TEST(expand, expand_tab_list_plus_repeat) {
  Pipeline p;
  p.set_stdin("a\tb\tc\n");
  p.add(L"expand.exe", {L"-t", L"3,+4"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a  b   c\n");
}

TEST(expand, expand_tab_list_slash_repeat) {
  Pipeline p;
  p.set_stdin("a\tb\tc\n");
  p.add(L"expand.exe", {L"-t", L"3,/4"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a  b    c\n");
}

TEST(expand, expand_initial_preserves_later_tabs) {
  Pipeline p;
  p.set_stdin(" \ta\tb\n");
  p.add(L"expand.exe", {L"-i"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "        a\tb\n");
}

TEST(expand, expand_backspace_decrements_column) {
  Pipeline p;
  p.set_stdin("ab\b\tc\n");
  p.add(L"expand.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "ab\b       c\n");
}

TEST(expand, expand_tabs_option_keeps_glob_literal) {
  TempDir tmp;
  tmp.write("4.txt", "ignored\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("a\tb\n");
  p.add(L"expand.exe", {L"-t", L"*.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
}

TEST(expand, expand_stdin) {
  Pipeline p;
  p.set_stdin("hello\tworld\n");
  p.add(L"expand.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("\t") == std::string::npos);
}

TEST(expand, expand_missing_input_reports_no_such_file) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"expand.exe", {L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "expand: cannot open 'missing.txt' for reading: No such "
                  "file or directory") != std::string::npos);
}

TEST(expand, expand_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("indir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"expand.exe", {L"indir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "expand: cannot open 'indir' for reading: Is a directory") !=
              std::string::npos);
}

// [#1204] expand keeps the CR like GNU; tab expansion never touches it.
TEST(expand, expand_preserves_cr_from_crlf_records_issue1204) {
  TempDir tmp;
  tmp.write_bytes("crlf.txt",
                  {'a', '\t', 'b', '\r', '\n', 'c', '\t', 'd', '\r', '\n'});

  Pipeline by_file;
  by_file.set_cwd(tmp.wpath());
  by_file.add(L"expand.exe", {L"-t", L"4", L"crlf.txt"});
  auto rf = by_file.run();

  EXPECT_EQ(rf.exit_code, 0);
  EXPECT_EQ(rf.stdout_text, std::string("a   b\r\nc   d\r\n", 14));

  const std::string payload("a\tb\r\nc\td\r\n", 10);
  Pipeline by_stdin;
  by_stdin.set_stdin(payload);
  by_stdin.add(L"expand.exe", {L"-t", L"4"});
  auto rs = by_stdin.run();

  EXPECT_EQ(rs.exit_code, 0);
  EXPECT_EQ(rs.stdout_text, rf.stdout_text);
}

TEST(expand, expand_tab_stop_error_messages_match_gnu) {
  Pipeline invalid;
  invalid.add(L"expand.exe", {L"-t", L"x"});
  auto r = invalid.run();
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "expand: tab size contains invalid character(s): 'x'\n");

  Pipeline negative;
  negative.add(L"expand.exe", {L"-t", L"-5"});
  auto negative_result = negative.run();
  EXPECT_EQ(negative_result.exit_code, 1);
  EXPECT_EQ_TEXT(negative_result.stderr_text,
                 "expand: tab size contains invalid character(s): '-5'\n");

  Pipeline zero;
  zero.add(L"expand.exe", {L"-t", L"0"});
  EXPECT_EQ_TEXT(zero.run().stderr_text, "expand: tab size cannot be 0\n");

  Pipeline ascending;
  ascending.add(L"expand.exe", {L"-t", L"4,2"});
  EXPECT_EQ_TEXT(ascending.run().stderr_text,
                 "expand: tab sizes must be ascending\n");

  Pipeline large;
  large.add(L"expand.exe", {L"-t", L"99999999999999999999"});
  EXPECT_EQ_TEXT(large.run().stderr_text,
                 "expand: tab stop is too large '99999999999999999999'\n");
}

// [GNU] tab stops land on exact multiples: 'a' occupies column 0, so the
// first tab pads to column 7 and the second to column 14.
TEST(expand, expand_tab_stops_hit_exact_multiples) {
  Pipeline p;
  p.set_stdin("a\tb\tc\n");
  p.add(L"expand.exe", {L"-t", L"7"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a      b      c\n");
}

// Regression: a large in-range tab size must stream its padding instead of
// failing silently. The old code tried to materialize the whole gap in one
// std::string and produced zero bytes when the allocation failed.
TEST(expand, expand_large_tab_stop_streams_full_padding) {
  Pipeline p;
  p.set_stdin("a\tb\n");
  p.add(L"expand.exe", {L"-t", L"1000000"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // 'a' + 999999 spaces (to column 1000000) + 'b' + '\n'.
  EXPECT_EQ(r.stdout_text.size(), 1000002u);
  EXPECT_EQ(r.stdout_text.front(), 'a');
  EXPECT_EQ(r.stdout_text.find('b'), 1000000u);
  EXPECT_EQ(r.stdout_text.back(), '\n');
}

// Regression: an in-range huge tab size must not produce zero bytes. GNU
// really writes all ~1e12 spaces for -t 999999999999; use a size large
// enough to prove streaming works without making the test itself huge.
TEST(expand, expand_huge_in_range_tab_stop_does_not_drop_output) {
  Pipeline p;
  p.set_stdin("a\tb\n");
  p.add(L"expand.exe", {L"-t", L"10000000"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text.size(), 10000002u);
  EXPECT_TRUE(r.stdout_text.find('\t') == std::string::npos);
}

// [GNU] multi-value tab lists keep working: stops at columns 4 and 8, and a
// tab beyond the last listed stop expands to a single space.
TEST(expand, expand_multi_value_tab_list_still_works) {
  Pipeline p;
  p.set_stdin("a\tb\tc\td\n");
  p.add(L"expand.exe", {L"-t", L"4,8"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a   b   c d\n");
}

// [#1212] fd 0 is text mode by default, so a pipe stopped at the first 0x1A
// and disagreed with the file operand. Both forms must be byte-identical.
TEST(expand, expand_stdin_ctrlz_matches_file_operand_issue1212) {
  const std::string payload(
      "AAAA\x1a"
      "BBBB\n",
      10);
  TempDir tmp;
  tmp.write_bytes("p.bin",
                  {'A', 'A', 'A', 'A', 0x1a, 'B', 'B', 'B', 'B', '\n'});

  Pipeline by_file;
  by_file.set_cwd(tmp.wpath());
  by_file.add(L"expand.exe", {L"p.bin"});
  auto rf = by_file.run();

  Pipeline by_stdin;
  by_stdin.set_stdin(payload);
  by_stdin.add(L"expand.exe", {});
  auto rs = by_stdin.run();

  EXPECT_EQ(rs.exit_code, 0);
  EXPECT_FALSE(rf.stdout_text.empty());
  EXPECT_EQ(rs.stdout_text, rf.stdout_text);
}
