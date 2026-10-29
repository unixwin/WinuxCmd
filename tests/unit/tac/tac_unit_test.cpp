// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(tac, tac_basic_file) {
  TempDir tmp;
  tmp.write("test.txt", "line1\nline2\nline3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tac.exe", {L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // tac should print lines in reverse order
  EXPECT_TRUE(r.stdout_text.find("line3") < r.stdout_text.find("line2"));
  EXPECT_TRUE(r.stdout_text.find("line2") < r.stdout_text.find("line1"));
}

TEST(tac, tac_reads_utf8_filename) {
  TempDir tmp;
  const std::wstring name = L"\x6D4B\x8BD5.txt";
  {
    std::ofstream out(tmp.path / name, std::ios::binary);
    out << "one\ntwo\n";
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tac.exe", {name});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "two\none\n");
}

TEST(tac, tac_reverses_each_file_independently) {
  TempDir tmp;
  tmp.write("a.txt", "a1\na2\n");
  tmp.write("b.txt", "b1\nb2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tac.exe", {L"a.txt", L"b.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a2\na1\nb2\nb1\n");
}

TEST(tac, tac_custom_separator) {
  TempDir tmp;
  tmp.write("colon.txt", "one:two:three:");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tac.exe", {L"-s", L":", L"colon.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "three:two:one:");
}

TEST(tac, tac_before_attaches_separator_to_next_record) {
  TempDir tmp;
  tmp.write("colon.txt", "one:two:three:");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tac.exe", {L"-b", L"-s", L":", L"colon.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "::three:twoone");
}

TEST(tac, tac_empty_separator_treats_input_as_one_record) {
  Pipeline p;
  p.set_stdin(std::string("a\0b\0c\0", 6));
  p.add(L"tac.exe", {L"--separator="});

  auto r = p.run();

  // [GNU] An empty separator never occurs in the input, so the whole
  // input is a single record and comes back verbatim (coreutils 9.4,
  // #1444). The earlier pin here claimed tac.c rejects it outright;
  // that holds only for the regex form (next test).
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, std::string("a\0b\0c\0", 6));
}

TEST(tac, tac_empty_separator_via_short_option_keeps_input_verbatim) {
  Pipeline p;
  p.set_stdin("a\nb\n");
  p.add(L"tac.exe", {L"-s", L""});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\nb\n");
}

TEST(tac, tac_regex_empty_separator_is_rejected) {
  Pipeline p;
  p.set_stdin("a\nb\n");
  p.add(L"tac.exe", {L"-r", L"--separator="});

  auto r = p.run();

  // [GNU] Only the regex form rejects an empty separator (#1444).
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text, "tac: separator cannot be empty\n");
}

TEST(tac, tac_regex_separator) {
  TempDir tmp;
  tmp.write("digits.txt", "aa11bb22cc33");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tac.exe", {L"-r", L"-s", L"[0-9]", L"digits.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // [GNU] single-digit separators; each record keeps its separator.
  EXPECT_EQ_TEXT(r.stdout_text, "3cc32bb21aa1");
}

TEST(tac, tac_issue1361_bare_pipe_is_literal_in_bre_separator) {
  // [GNU] In a BRE, '|' is an ordinary character: the separator is the
  // literal string "x|yy", which never occurs, so nothing is split.
  TempDir tmp;
  tmp.write("letters.txt", "axbyyc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tac.exe", {L"-r", L"-s", L"x|yy", L"letters.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "axbyyc");
}

TEST(tac, tac_issue1361_escaped_pipe_is_alternation_in_bre_separator) {
  // [GNU] In a BRE, `\|` is alternation: separators are "x" and "yy".
  TempDir tmp;
  tmp.write("letters.txt", "axbyyc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tac.exe", {L"-r", L"-s", L"x\\|yy", L"letters.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "cbyyax");
}

TEST(tac, tac_stdin) {
  Pipeline p;
  p.set_stdin("line1\nline2\nline3\n");
  p.add(L"tac.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("line3") < r.stdout_text.find("line2"));
  EXPECT_TRUE(r.stdout_text.find("line2") < r.stdout_text.find("line1"));
}

TEST(tac, tac_single_line) {
  TempDir tmp;
  tmp.write("single.txt", "only one line\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tac.exe", {L"single.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "only one line\n");
}

TEST(tac, tac_newline_mode_preserves_crlf_records) {
  TempDir tmp;
  tmp.write_bytes("crlf.txt",
                  {'l', 'i',  'n',  'e', '1', '\r', '\n', 'l', 'i',  'n', 'e',
                   '2', '\r', '\n', 'l', 'i', 'n',  'e',  '3', '\r', '\n'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tac.exe", {L"crlf.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "line3\r\nline2\r\nline1\r\n");
}

TEST(tac, tac_missing_input_reports_no_such_file) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tac.exe", {L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "tac: cannot open 'missing.txt' for reading: No such file "
                  "or directory") != std::string::npos);
}

TEST(tac, tac_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("indir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tac.exe", {L"indir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "tac: cannot open 'indir' for reading: Is a directory") !=
              std::string::npos);
}

TEST(tac, tac_file_with_trailing_separator_reports_not_directory) {
  TempDir tmp;
  tmp.write("file.txt", "payload\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tac.exe", {L"file.txt/"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(
      r.stderr_text.find(
          "tac: cannot open 'file.txt/' for reading: Not a directory") !=
      std::string::npos);
}

// ---------------------------------------------------------------------------
// Issue #1368: tac.c's full_write() failure reaches write_error()
// (system.h:960): one "tac: write error: <strerror>" diagnostic, exit 1.
// With stdout closed (`>&-`) the first record write fails with
// ERROR_INVALID_HANDLE — the Win32 image of EBADF — and tac must stop
// printing instead of exiting 0 silently.
// ---------------------------------------------------------------------------
TEST(tac, issue1368_closed_stdout_reports_write_error) {
  TempDir tmp;
  tmp.write("input.txt", "line1\nline2\nline3\n");
  const std::wstring operand = (tmp.path / L"input.txt").wstring();

  auto r =
      run_command_closed_stdout(ProjectPaths::exe(L"tac.exe"), {operand});

  EXPECT_EXIT_CODE(r, 1);
  EXPECT_CONTAINS(r.stderr_text, "tac: write error: ");
  EXPECT_CONTAINS(r.stderr_text, "Bad file descriptor");
}
