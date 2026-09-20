// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(nl, nl_basic_file) {
  TempDir tmp;
  tmp.write("test.txt", "line1\nline2\nline3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nl.exe", {L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("1\tline1") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("2\tline2") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("3\tline3") != std::string::npos);
}

TEST(nl, nl_stdin) {
  Pipeline p;
  p.set_stdin("line1\nline2\n");
  p.add(L"nl.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("1\tline1") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("2\tline2") != std::string::npos);
}

TEST(nl, nl_empty_file) {
  TempDir tmp;
  tmp.write("empty.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nl.exe", {L"empty.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty() || r.stdout_text == "\n");
}

TEST(nl, nl_number_format_and_negative_increment) {
  TempDir tmp;
  tmp.write("test.txt", "line1\nline2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nl.exe", {L"-n", L"rz", L"-w", L"3", L"-v", L"-1", L"-i", L"-2",
                    L"-s", L":", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "-01:line1\n-03:line2\n");
}

TEST(nl, nl_numbers_logical_page_sections) {
  TempDir tmp;
  tmp.write("test.txt", "\\:\\:\\:\nheader\n\\:\\:\nbody\n\\:\nfooter\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nl.exe",
        {L"-h", L"a", L"-f", L"a", L"-w", L"1", L"-s", L":", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "\n1:header\n\n1:body\n\n1:footer\n");
}

TEST(nl, nl_no_renumber_keeps_count_across_sections) {
  TempDir tmp;
  tmp.write("test.txt", "\\:\\:\\:\nheader\n\\:\\:\nbody\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nl.exe", {L"-p", L"-h", L"a", L"-w", L"1", L"-s", L":", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "\n1:header\n\n2:body\n");
}

TEST(nl, nl_join_blank_lines_numbers_only_group_boundary) {
  TempDir tmp;
  tmp.write("test.txt", "line1\n\n\n\nline2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nl.exe",
        {L"-b", L"a", L"-l", L"2", L"-w", L"1", L"-s", L":", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "1:line1\n  \n2:\n  \n3:line2\n");
}

TEST(nl, nl_pattern_body_numbering) {
  TempDir tmp;
  tmp.write("test.txt", "ERR first\nok\nERR second\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nl.exe", {L"-b", L"p^ERR", L"-w", L"1", L"-s", L":", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "1:ERR first\n  ok\n2:ERR second\n");
}

TEST(nl, nl_unnumbered_lines_use_blank_number_field_not_separator) {
  TempDir tmp;
  tmp.write("test.txt", "line\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nl.exe", {L"-b", L"n", L"-w", L"3", L"-s", L"::", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "     line\n");
}

TEST(nl, nl_empty_number_separator) {
  TempDir tmp;
  tmp.write("test.txt", "line\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nl.exe", {L"-w", L"1", L"-s", L"", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "1line\n");
}

TEST(nl, nl_newline_mode_trims_trailing_cr_from_crlf_records) {
  TempDir tmp;
  tmp.write_bytes("crlf.txt", {'l', 'i', 'n', 'e', '1', '\r', '\n', 'l', 'i',
                               'n', 'e', '2', '\r', '\n'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nl.exe", {L"-w", L"1", L"crlf.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1\tline1\n2\tline2\n");
}

TEST(nl, nl_missing_input_reports_no_such_file) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nl.exe", {L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "nl: cannot open 'missing.txt' for reading: No such file "
                  "or directory") != std::string::npos);
}

TEST(nl, nl_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("indir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nl.exe", {L"indir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "nl: cannot open 'indir' for reading: Is a directory") !=
              std::string::npos);
}

// [GNU] nl 9.4 accepts a zero line increment (-i 0, -i +0,
// --line-increment=0) and repeats the line number; only non-numeric text is
// rejected with status 1 (#1011).
TEST(nl, nl_zero_line_increment_repeats_line_number) {
  TempDir tmp;
  tmp.write("in.txt", "a\nb\n");

  for (const wchar_t* opt : {L"0", L"+0", L"00"}) {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"nl.exe", {L"-i", opt, L"in.txt"});
    auto r = p.run();

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ_TEXT(r.stdout_text, "     1\ta\n     1\tb\n");
  }

  Pipeline long_opt;
  long_opt.set_cwd(tmp.wpath());
  long_opt.add(L"nl.exe", {L"--line-increment=0", L"in.txt"});
  auto long_result = long_opt.run();
  EXPECT_EQ(long_result.exit_code, 0);
  EXPECT_EQ_TEXT(long_result.stdout_text, "     1\ta\n     1\tb\n");
}

TEST(nl, nl_negative_line_increment_decrements) {
  TempDir tmp;
  tmp.write("in.txt", "a\nb\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nl.exe", {L"-i", L"-2", L"in.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "     1\ta\n    -1\tb\n");
}

TEST(nl, nl_invalid_line_increment_exits_1) {
  TempDir tmp;
  tmp.write("in.txt", "a\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nl.exe", {L"-i", L"1x", L"in.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("invalid line number increment: '1x'") !=
              std::string::npos);
}
