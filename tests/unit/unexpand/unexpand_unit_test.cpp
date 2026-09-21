// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(unexpand, unexpand_basic) {
  TempDir tmp;
  tmp.write("test.txt", "hello        world\n");  // 8 spaces between words

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"unexpand.exe", {L"-t", L"8", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Spaces at tab positions should be converted to tabs
  EXPECT_TRUE(r.stdout_text.find("hello") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("world") != std::string::npos);
}

TEST(unexpand, unexpand_stdin) {
  Pipeline p;
  p.set_stdin("hello        world\n");
  p.add(L"unexpand.exe", {L"-t", L"8"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(unexpand, unexpand_default_only_converts_leading_blanks) {
  Pipeline p;
  p.set_stdin("        x        y\n");
  p.add(L"unexpand.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "\tx        y\n");
}

TEST(unexpand, unexpand_tabs_option_implies_all) {
  Pipeline p;
  p.set_stdin("ab  cd\n");
  p.add(L"unexpand.exe", {L"-t", L"4"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "ab\tcd\n");
}

TEST(unexpand, unexpand_first_only_overrides_tabs_all) {
  Pipeline p;
  p.set_stdin("    x    y\n");
  p.add(L"unexpand.exe", {L"-t", L"4", L"--first-only"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "\tx    y\n");
}

TEST(unexpand, unexpand_f_alias_matches_first_only) {
  Pipeline p;
  p.set_stdin("        x    y\n");
  p.add(L"unexpand.exe", {L"-a", L"-f"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "\tx    y\n");
}

TEST(unexpand, unexpand_tab_list) {
  Pipeline p;
  p.set_stdin("a  b\n");
  p.add(L"unexpand.exe", {L"-t", L"3,5"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\tb\n");
}

TEST(unexpand, unexpand_tab_list_slash_repeat) {
  Pipeline p;
  p.set_stdin("a  b    c\n");
  p.add(L"unexpand.exe", {L"-t", L"3,/4"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\tb\tc\n");
}

TEST(unexpand, unexpand_tabs_option_keeps_glob_literal) {
  TempDir tmp;
  tmp.write("4.txt", "ignored\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("a  b\n");
  p.add(L"unexpand.exe", {L"-t", L"*.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
}

TEST(unexpand, unexpand_missing_input_reports_no_such_file) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"unexpand.exe", {L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "unexpand: cannot open 'missing.txt' for reading: No such "
                  "file or directory") != std::string::npos);
}

TEST(unexpand, unexpand_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("indir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"unexpand.exe", {L"indir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(
      r.stderr_text.find(
          "unexpand: cannot open 'indir' for reading: Is a directory") !=
      std::string::npos);
}

TEST(unexpand, unexpand_no_utf8_preserves_utf8_bom_bytes) {
  TempDir tmp;
  tmp.write_bytes("bom.txt", {static_cast<char>(0xEF), static_cast<char>(0xBB),
                              static_cast<char>(0xBF), ' ', ' ', ' ', ' ', ' ',
                              ' ', ' ', ' ', 'x', '\n'});

  Pipeline default_mode;
  default_mode.set_cwd(tmp.wpath());
  default_mode.add(L"unexpand.exe", {L"bom.txt"});
  auto default_result = default_mode.run();

  EXPECT_EQ(default_result.exit_code, 0);
  EXPECT_EQ_TEXT(default_result.stdout_text, "\tx\n");

  Pipeline ascii_mode;
  ascii_mode.set_cwd(tmp.wpath());
  ascii_mode.add(L"unexpand.exe", {L"-U", L"-a", L"bom.txt"});
  auto ascii_result = ascii_mode.run();

  EXPECT_EQ(ascii_result.exit_code, 0);
  EXPECT_EQ(ascii_result.stdout_text, std::string("\xEF\xBB\xBF\t   x\n", 9));
}

TEST(unexpand, unexpand_tab_stop_error_messages_match_gnu) {
  Pipeline invalid;
  invalid.add(L"unexpand.exe", {L"-t", L"x"});
  auto r = invalid.run();
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "unexpand: tab size contains invalid character(s): 'x'\n");

  Pipeline ascending;
  ascending.add(L"unexpand.exe", {L"-t", L"4,2"});
  EXPECT_EQ_TEXT(ascending.run().stderr_text,
                 "unexpand: tab sizes must be ascending\n");

  Pipeline large;
  large.add(L"unexpand.exe", {L"-t", L"99999999999999999999"});
  auto large_result = large.run();
  EXPECT_EQ(large_result.exit_code, 1);
  EXPECT_EQ_TEXT(large_result.stderr_text,
                 "unexpand: tab stop is too large '99999999999999999999'\n");
}

// Audit regression: an in-range huge tab interval must not break conversion
// of ordinary input — 'a' is not a blank, so nothing is rewritten and no
// giant tab column is ever reached.
TEST(unexpand, unexpand_huge_tab_stop_handles_short_input) {
  Pipeline p;
  p.set_stdin("a\n");
  p.add(L"unexpand.exe", {L"-t", L"999999999999"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\n");
}

// Leading blanks that never reach the huge tab stop pass through verbatim.
TEST(unexpand, unexpand_huge_tab_stop_preserves_leading_blanks) {
  Pipeline p;
  p.set_stdin("   x\n");
  p.add(L"unexpand.exe", {L"-t", L"999999999999"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "   x\n");
}

// [GNU] obsolescent -NUM options select tab stops without implying -a;
// consecutive digit options accumulate one value, so "-4 -8" is stop 48
// and "-48" matches it (#1082).
TEST(unexpand, unexpand_obsolete_numeric_options) {
  Pipeline stop_one;
  stop_one.set_stdin("    x\n");
  stop_one.add(L"unexpand.exe", {L"-1"});
  auto stop_one_result = stop_one.run();
  EXPECT_EQ(stop_one_result.exit_code, 0);
  EXPECT_EQ_TEXT(stop_one_result.stdout_text, "\t\t\t\tx\n");

  Pipeline stop_nine;
  stop_nine.set_stdin("    x\n");
  stop_nine.add(L"unexpand.exe", {L"-9"});
  auto stop_nine_result = stop_nine.run();
  EXPECT_EQ(stop_nine_result.exit_code, 0);
  EXPECT_EQ_TEXT(stop_nine_result.stdout_text, "    x\n");

  // The digit form does not imply -a, so mid-line blanks are untouched.
  Pipeline leading_only;
  leading_only.set_stdin("a        b\n");
  leading_only.add(L"unexpand.exe", {L"-4"});
  auto leading_only_result = leading_only.run();
  EXPECT_EQ(leading_only_result.exit_code, 0);
  EXPECT_EQ_TEXT(leading_only_result.stdout_text, "a        b\n");

  Pipeline leading_stop;
  leading_stop.set_stdin("         x\n");
  leading_stop.add(L"unexpand.exe", {L"-4"});
  auto leading_stop_result = leading_stop.run();
  EXPECT_EQ(leading_stop_result.exit_code, 0);
  EXPECT_EQ_TEXT(leading_stop_result.stdout_text, "\t\t x\n");
}

TEST(unexpand, unexpand_numeric_options_accumulate_one_stop) {
  // [GNU] "-4 -8" accumulates the digits into tab stop 48, identical to
  // "-48"; nine leading blanks never reach it.
  Pipeline separate;
  separate.set_stdin("         x\n");
  separate.add(L"unexpand.exe", {L"-4", L"-8"});
  auto separate_result = separate.run();
  EXPECT_EQ(separate_result.exit_code, 0);
  EXPECT_EQ_TEXT(separate_result.stdout_text, "         x\n");

  Pipeline glued;
  glued.set_stdin("         x\n");
  glued.add(L"unexpand.exe", {L"-48"});
  auto glued_result = glued.run();
  EXPECT_EQ(glued_result.exit_code, 0);
  EXPECT_EQ_TEXT(glued_result.stdout_text, "         x\n");
}

TEST(unexpand, unexpand_numeric_stops_mix_with_dash_t_in_gnu_order) {
  // [GNU] a pending digit value flushes after the whole option list, so a
  // -t spec lands first and both orderings fail as non-ascending.
  Pipeline num_first;
  num_first.add(L"unexpand.exe", {L"-4", L"-t", L"8"});
  auto num_first_result = num_first.run();
  EXPECT_EQ(num_first_result.exit_code, 1);
  EXPECT_EQ_TEXT(num_first_result.stderr_text,
                 "unexpand: tab sizes must be ascending\n");

  Pipeline t_first;
  t_first.add(L"unexpand.exe", {L"-t", L"8", L"-4"});
  auto t_first_result = t_first.run();
  EXPECT_EQ(t_first_result.exit_code, 1);
  EXPECT_EQ_TEXT(t_first_result.stderr_text,
                 "unexpand: tab sizes must be ascending\n");
}

// [GNU] mid-line blanks that land on or cross a tab stop convert when -a or
// -t selects whole-line conversion, including a run crossing a stop by a
// single column (#1012).
TEST(unexpand, unexpand_converts_mid_line_blanks_crossing_tab_stops) {
  Pipeline crossing;
  crossing.set_stdin("abcdefg  x\n");
  crossing.add(L"unexpand.exe", {L"-a"});
  auto crossing_result = crossing.run();
  EXPECT_EQ(crossing_result.exit_code, 0);
  EXPECT_EQ_TEXT(crossing_result.stdout_text, "abcdefg\t x\n");

  Pipeline implied_all;
  implied_all.set_stdin("x  y\n");
  implied_all.add(L"unexpand.exe", {L"-t", L"3"});
  auto implied_all_result = implied_all.run();
  EXPECT_EQ(implied_all_result.exit_code, 0);
  EXPECT_EQ_TEXT(implied_all_result.stdout_text, "x\ty\n");

  Pipeline custom_stops;
  custom_stops.set_stdin("   x   y   z\n");
  custom_stops.add(L"unexpand.exe", {L"-t", L"3"});
  auto custom_stops_result = custom_stops.run();
  EXPECT_EQ(custom_stops_result.exit_code, 0);
  EXPECT_EQ_TEXT(custom_stops_result.stdout_text, "\tx\t y\t  z\n");
}

TEST(unexpand, unexpand_finite_tab_list_stops_converting_after_last) {
  // [GNU] with a finite list there is no stop beyond the last one, so the
  // remaining blanks stay verbatim.
  Pipeline p;
  p.set_stdin("         x\n");
  p.add(L"unexpand.exe", {L"-t", L"3,6"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "\t\t   x\n");
}
