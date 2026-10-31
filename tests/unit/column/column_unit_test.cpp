// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(column, column_table_mode) {
  Pipeline p;
  p.set_stdin("name\tage\tcity\nAlice\t30\tNY\nBob\t25\tLA\n");
  p.add(L"column.exe", {L"-t"});

  TEST_LOG_CMD_LIST("column.exe", L"-t");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("column table output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "name   age  city\n"
                 "Alice  30   NY\n"
                 "Bob    25   LA\n");
}

TEST(column, column_custom_separator) {
  Pipeline p;
  p.set_stdin("name,age,city\nAlice,30,NY\nBob,25,LA\n");
  p.add(L"column.exe", {L"-t", L"-s", L","});

  TEST_LOG_CMD_LIST("column.exe", L"-t", L"-s", L",");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("column custom separator output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "name   age  city\n"
                 "Alice  30   NY\n"
                 "Bob    25   LA\n");
}

TEST(column, column_table_separator_preserves_empty_fields) {
  Pipeline p;
  p.set_stdin("a,,c\nlong,b,\n");
  p.add(L"column.exe", {L"-t", L"-s", L","});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "a        c\n"
                 "long  b  \n");
}

TEST(column, column_file_input) {
  TempDir tmp;
  tmp.write("data.txt", "name\tage\ntest\t123\n");

  TEST_LOG_FILE_CONTENT("data.txt", "name\tage\ntest\t123\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"column.exe", {L"-t", L"data.txt"});

  TEST_LOG_CMD_LIST("column.exe", L"-t", L"data.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("column file output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(column, column_json_output) {
  Pipeline p;
  p.set_stdin("name\tage\tcity\nAlice\t30\tNY\nBob\t25\tLA\n");
  p.add(L"column.exe", {L"-t", L"--json"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // JSON output should contain array
  EXPECT_TRUE(r.stdout_text.find("[") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("]") != std::string::npos);
}

TEST(column, column_output_width) {
  Pipeline p;
  p.set_stdin("name\tage\tcity\nAlice\t30\tNY\nBob\t25\tLA\n");
  p.add(L"column.exe", {L"-t", L"--output-width", L"40"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Output should be truncated to 40 columns
}

TEST(column, column_right_align) {
  Pipeline p;
  p.set_stdin("name\tage\tcity\nAlice\t30\tNY\nBob\t25\tLA\n");
  p.add(L"column.exe", {L"-t", L"--table-right", L"2"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Column 2 (age) should be right-aligned
}

TEST(column, column_right_columns) {
  Pipeline p;
  p.set_stdin("name\tage\tscore\nAlice\t30\t95\nBob\t25\t87\n");
  p.add(L"column.exe", {L"-t", L"--table-right", L"2,3"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Columns 2 and 3 should be right-aligned
}

TEST(column, column_empty_input) {
  Pipeline p;
  p.set_stdin("");
  p.add(L"column.exe", {L"-t"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(column, column_single_column) {
  Pipeline p;
  p.set_stdin("hello\nworld\n");
  p.add(L"column.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(column, column_fill_columns) {
  Pipeline p;
  p.set_stdin("a\nb\nc\nd\ne\nf\n");
  p.add(L"column.exe", {L"-c", L"30"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(column, column_table_mode_trims_trailing_cr_from_crlf_records) {
  TempDir tmp;
  tmp.write_bytes("crlf.txt",
                  {'n', 'a', 'm', 'e', '\t', 'a',  'g', 'e', '\r', '\n',
                   'A', 'l', 'i', 'c', 'e',  '\t', '3', '0', '\r', '\n'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"column.exe", {L"-t", L"crlf.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find('\r') == std::string::npos);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "name   age\n"
                 "Alice  30\n");
}

TEST(column, column_missing_input_reports_no_such_file) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"column.exe", {L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "column: cannot open 'missing.txt' for reading: No such "
                  "file or directory") != std::string::npos);
}

TEST(column, column_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("indir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"column.exe", {L"indir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "column: cannot open 'indir' for reading: Is a directory") !=
              std::string::npos);
}

TEST(column, column_new_layout_options) {
  Pipeline p;
  p.set_stdin("a\tb\tc\n1\t2\t3\n");
  p.add(L"column.exe", {L"-x", L"1,2", L"-N", L"A,B,C", L"-E", L"-t"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(column, column_width_option_c_is_accepted) {
  Pipeline p;
  p.set_stdin("a\nb\nc\n");
  p.add(L"column.exe", {L"-c", L"20"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

// [#1212] fd 0 is text mode by default, so a pipe stopped at the first 0x1A
// and disagreed with the file operand. Both forms must be byte-identical.
TEST(column, column_stdin_ctrlz_matches_file_operand_issue1212) {
  const std::string payload(
      "AAAA\x1a"
      "BBBB\n",
      10);
  TempDir tmp;
  tmp.write_bytes("p.bin",
                  {'A', 'A', 'A', 'A', 0x1a, 'B', 'B', 'B', 'B', '\n'});

  Pipeline by_file;
  by_file.set_cwd(tmp.wpath());
  by_file.add(L"column.exe", {L"-t", L"p.bin"});
  auto rf = by_file.run();

  Pipeline by_stdin;
  by_stdin.set_stdin(payload);
  by_stdin.add(L"column.exe", {L"-t"});
  auto rs = by_stdin.run();

  EXPECT_EQ(rs.exit_code, 0);
  EXPECT_FALSE(rf.stdout_text.empty());
  EXPECT_EQ(rs.stdout_text, rf.stdout_text);
}

// [refs #1127] Default mode now columnates (it previously echoed the
// input): entries fill down columns on a tab-stop pitch (column.c).
TEST(column, default_columnation_fills_columns) {
  Pipeline p;
  p.set_stdin("a\nb\nc\nd\n");
  p.add(L"column.exe", {L"-c", L"20"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\tc\nb\td\n");
}

// [refs #1127] -x/--fillrows fills across rows first.
TEST(column, fillrows_fills_rows_first) {
  Pipeline p;
  p.set_stdin("a\nb\nc\nd\n");
  p.add(L"column.exe", {L"-c", L"20", L"--fillrows"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\tb\nc\td\n");
}

// [refs #1127] Blank lines are dropped by default and kept with -L.
TEST(column, empty_lines_dropped_unless_kept) {
  Pipeline dropped;
  dropped.set_stdin("a\n\nb\n");
  dropped.add(L"column.exe", {});
  auto r_dropped = dropped.run();
  EXPECT_EQ(r_dropped.exit_code, 0);
  EXPECT_EQ_TEXT(r_dropped.stdout_text, "a\tb\n");

  Pipeline kept;
  kept.set_stdin("a\n\nb\n");
  kept.add(L"column.exe", {L"-L"});
  auto r_kept = kept.run();
  EXPECT_EQ(r_kept.exit_code, 0);
  EXPECT_EQ_TEXT(r_kept.stdout_text, "a\t\tb\n");
}

// [refs #1127] -e/--table-header-repeat repeats the header row after a
// formfeed page break in table mode.
TEST(column, table_header_repeat_on_formfeed) {
  Pipeline p;
  p.set_stdin("h1 h2\na b\n\f\nc d\n");
  p.add(L"column.exe", {L"-t", L"--table-header-repeat"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  size_t first = r.stdout_text.find("h1");
  EXPECT_NE(first, std::string::npos);
  EXPECT_NE(r.stdout_text.find("h1", first + 1), std::string::npos);
}

// [refs #1127] -E/--table-noextreme: the named column's width gives
// way when the table must fit the display width.
TEST(column, table_noextreme_shrinks_named_column) {
  Pipeline p;
  p.set_stdin("name value\nshort averyverylongvalue\n");
  p.add(L"column.exe", {L"-t", L"-c", L"12", L"-E", L"2"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "avery");
  EXPECT_TRUE(r.stdout_text.find("verylong") == std::string::npos);
}

// [refs #1127] -x no longer means --output-fields; the long option
// still selects fields on its own.
TEST(column, output_fields_long_option_still_works) {
  Pipeline p;
  p.set_stdin("a b\nc d\n");
  p.add(L"column.exe", {L"-t", L"--output-fields", L"2"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "b\nd\n");
}
