// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
//
// Issue #1536: the ls COLUMNS face merged via #1534 diverges from GNU
// coreutils 9.4 in five recorded ways. GNU reference: ls.c
// decode_line_length() (ls.c:1867-1886) parses COLUMNS and -w with
// xstrtoumax base-0 semantics; 0 and too-large values mean "no limit";
// -w is decoded by the same function (ls.c:2024) and an invalid -w is a
// fatal "invalid line width" (exit 2); COLUMNS is consulted only when
// no -w and no tty width exist (ls.c:2294-2307) and an invalid value
// warns with quote() marks (the UTF-8 ‘’ form under C.UTF-8) and falls
// back to 80. Tests run ls.exe as a subprocess with piped stdout, so
// the console query fails and the COLUMNS branch is live.
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "framework/winuxtest.h"

namespace ls_issue1536_test {

std::vector<std::string> twenty_four_names() {
  std::vector<std::string> names;
  for (int i = 1; i <= 24; ++i) {
    names.push_back("file_" + std::to_string(i));
  }
  return names;
}

size_t count_lines(const std::string& text) {
  return static_cast<size_t>(std::count(text.begin(), text.end(), '\n'));
}

// All 24 fixture names on one unwrapped line: print_with_separator(' ')
// joins with exactly two spaces (ls.c:4143-4153 for -C and -x alike).
const char* one_line_text() {
  return "file_1  file_10  file_11  file_12  file_13  file_14  "
         "file_15  file_16  file_17  file_18  file_19  file_2  "
         "file_20  file_21  file_22  file_23  file_24  file_3  "
         "file_4  file_5  file_6  file_7  file_8  file_9\n";
}

}  // namespace ls_issue1536_test

// Divergence 1 (COLUMNS base): GNU parses COLUMNS base-0, so 0100 is
// octal 64 (4 lines for this fixture), not decimal 100 (3 lines).
TEST(ls, ls_columns_env_octal_uses_base0_issue1536) {
  TempDir tmp;
  for (const auto& name : ls_issue1536_test::twenty_four_names()) {
    tmp.write(name, "");
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"COLUMNS", L"0100");
  p.add(L"ls.exe", {L"-C"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(ls_issue1536_test::count_lines(r.stdout_text), size_t{4});
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

// Divergence 1 (COLUMNS hex): 0x20 is width 32 (8 lines), not a
// partial base-10 parse that warns and falls back to 80.
TEST(ls, ls_columns_env_hex_uses_base0_issue1536) {
  TempDir tmp;
  for (const auto& name : ls_issue1536_test::twenty_four_names()) {
    tmp.write(name, "");
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"COLUMNS", L"0x20");
  p.add(L"ls.exe", {L"-C"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(ls_issue1536_test::count_lines(r.stdout_text), size_t{8});
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

// Divergence 2: an overflowing COLUMNS is "no limit" in GNU
// (decode_line_length maps LONGINT_OVERFLOW to 0), never warn + 80.
TEST(ls, ls_columns_env_overflow_means_no_limit_issue1536) {
  TempDir tmp;
  for (const auto& name : ls_issue1536_test::twenty_four_names()) {
    tmp.write(name, "");
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"COLUMNS", L"99999999999999999999999999");
  p.add(L"ls.exe", {L"-C"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, ls_issue1536_test::one_line_text());
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

// Divergence 5: the invalid-COLUMNS warning carries GNU quote()
// marks — the UTF-8 ‘’ form — not ASCII single quotes. The layout
// still falls back to 80 (3 lines for this fixture).
TEST(ls, ls_columns_env_invalid_warns_utf8_quotes_issue1536) {
  TempDir tmp;
  for (const auto& name : ls_issue1536_test::twenty_four_names()) {
    tmp.write(name, "");
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"COLUMNS", L"abc");
  p.add(L"ls.exe", {L"-C"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "ls: ignoring invalid width in environment variable "
                 "COLUMNS: \xE2\x80\x98"
                 "abc"
                 "\xE2\x80\x99\n");
  EXPECT_EQ(ls_issue1536_test::count_lines(r.stdout_text), size_t{3});
}

// decode_line_length edges, probed against GNU 9.4: leading blanks
// are skipped (" 40" is a valid width 40, 6 lines), trailing junk is
// invalid ("40 " warns and falls back to 80).
TEST(ls, ls_columns_env_blank_and_trailing_junk_issue1536) {
  TempDir tmp;
  for (const auto& name : ls_issue1536_test::twenty_four_names()) {
    tmp.write(name, "");
  }

  Pipeline leading;
  leading.set_cwd(tmp.wpath());
  leading.set_env(L"COLUMNS", L" 40");
  leading.add(L"ls.exe", {L"-C"});
  auto lr = leading.run();
  EXPECT_EQ(lr.exit_code, 0);
  EXPECT_EQ(ls_issue1536_test::count_lines(lr.stdout_text), size_t{6});
  EXPECT_EQ_TEXT(lr.stderr_text, "");

  Pipeline trailing;
  trailing.set_cwd(tmp.wpath());
  trailing.set_env(L"COLUMNS", L"40 ");
  trailing.add(L"ls.exe", {L"-C"});
  auto tr = trailing.run();
  EXPECT_EQ(tr.exit_code, 0);
  EXPECT_EQ_TEXT(tr.stderr_text,
                 "ls: ignoring invalid width in environment variable "
                 "COLUMNS: \xE2\x80\x98"
                 "40 "
                 "\xE2\x80\x99\n");
  EXPECT_EQ(ls_issue1536_test::count_lines(tr.stdout_text), size_t{3});
}

// Divergence 3: -w 0 is "no limit", decided at the option; it must
// not fall through to COLUMNS (here 10, which would wrap to 24 lines).
TEST(ls, ls_width_option_zero_means_no_limit_issue1536) {
  TempDir tmp;
  for (const auto& name : ls_issue1536_test::twenty_four_names()) {
    tmp.write(name, "");
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"COLUMNS", L"10");
  p.add(L"ls.exe", {L"-C", L"-w", L"0"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, ls_issue1536_test::one_line_text());
}

// Divergence 1 (-w face): -w goes through the same decode_line_length
// as COLUMNS, so 0100 is octal 64 (4 lines), not decimal 100 (3).
TEST(ls, ls_width_option_octal_uses_base0_issue1536) {
  TempDir tmp;
  for (const auto& name : ls_issue1536_test::twenty_four_names()) {
    tmp.write(name, "");
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-C", L"-w", L"0100"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(ls_issue1536_test::count_lines(r.stdout_text), size_t{4});
}

// Divergence 1 (-w hex face): -w 0x20 is width 32 (8 lines); a
// base-10-only parser cannot accept it at all.
TEST(ls, ls_width_option_hex_uses_base0_issue1536) {
  TempDir tmp;
  for (const auto& name : ls_issue1536_test::twenty_four_names()) {
    tmp.write(name, "");
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-C", L"-w", L"0x20"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(ls_issue1536_test::count_lines(r.stdout_text), size_t{8});
}

// Divergence 2 (-w face): an overflowing -w is "no limit"
// (LONGINT_OVERFLOW maps to 0 in decode_line_length), not an error.
TEST(ls, ls_width_option_overflow_means_no_limit_issue1536) {
  TempDir tmp;
  for (const auto& name : ls_issue1536_test::twenty_four_names()) {
    tmp.write(name, "");
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-C", L"-w", L"99999999999999999999999999"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, ls_issue1536_test::one_line_text());
}

// -w invalid: GNU decode_switches dies with LS_FAILURE (2) and the
// "invalid line width" diagnostic carrying quote() marks (ls.c:2024).
TEST(ls, ls_width_option_invalid_is_fatal_issue1536) {
  TempDir tmp;
  tmp.write("file_1", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-C", L"-w", L"abc"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(r.stderr_text.find("ls: invalid line width: "
                                 "\xE2\x80\x98"
                                 "abc"
                                 "\xE2\x80\x99") != std::string::npos);
  EXPECT_EQ_TEXT(r.stdout_text, "");
}

// Divergence 4: -x with COLUMNS=0 lays out one unwrapped line
// (print_with_separator for horizontal format, ls.c:4148-4153); the
// merged face's single-column clamp is not GNU's rule.
TEST(ls, ls_across_columns_env_zero_one_line_issue1536) {
  TempDir tmp;
  for (const auto& name : ls_issue1536_test::twenty_four_names()) {
    tmp.write(name, "");
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"COLUMNS", L"0");
  p.add(L"ls.exe", {L"-x"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, ls_issue1536_test::one_line_text());
}

// Divergence 3+4: -x with -w 0 is likewise one unwrapped line, even
// with a narrow COLUMNS behind it.
TEST(ls, ls_across_width_option_zero_one_line_issue1536) {
  TempDir tmp;
  for (const auto& name : ls_issue1536_test::twenty_four_names()) {
    tmp.write(name, "");
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"COLUMNS", L"10");
  p.add(L"ls.exe", {L"-x", L"-w", L"0"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, ls_issue1536_test::one_line_text());
}

// Divergence 2 (-m face): with_commas honours the same width chain;
// an overflowing COLUMNS is one unwrapped line, not warn + 80 wraps.
TEST(ls, ls_commas_columns_env_overflow_one_line_issue1536) {
  TempDir tmp;
  for (const auto& name : ls_issue1536_test::twenty_four_names()) {
    tmp.write(name, "");
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"COLUMNS", L"99999999999999999999999999");
  p.add(L"ls.exe", {L"-m"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(ls_issue1536_test::count_lines(r.stdout_text), size_t{1});
  EXPECT_EQ_TEXT(r.stderr_text, "");
}
