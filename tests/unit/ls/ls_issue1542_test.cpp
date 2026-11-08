// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
//
// Issue #1542: print_grid picks the column count with
// `total_grid_width(candidate) <= width`, but GNU coreutils 9.4
// calculate_columns() (ls.c) accepts a candidate layout only when
// `line_len < line_length` (strict). A grid whose total width exactly
// equals the line width therefore drops one column in GNU and does
// not in WinuxCmd. print_grid serves both -C and -x, so both forms
// are pinned here.
//
// Fixture: six 3-char names. The two-column candidate totals
// 3 + 2 + 3 = 8 (column widths plus one two-space gutter), so width 9
// is total == width - 1, width 8 is total == width (the divergent
// cell), and width 7 is total == width + 1. Expected texts were
// measured byte-for-byte against GNU ls 9.4 (coreutils) with
// COLUMNS=<width>; -w sets the same line_length in GNU and reaches
// the same resolved width in print_grid.
#include <string>
#include <vector>

#include "framework/winuxtest.h"

namespace ls_issue1542_test {

std::vector<std::string> six_names() {
  return {"aa1", "aa2", "aa3", "bb1", "bb2", "bb3"};
}

// One entry per line: the one-column layout both forms fall back to.
const char* one_column_text() {
  return "aa1\naa2\naa3\nbb1\nbb2\nbb3\n";
}

}  // namespace ls_issue1542_test

// total == width - 1 (-C): 8 < 9 holds even under the strict GNU
// test, so both implementations take two columns.
TEST(ls, ls_grid_columns_total_eq_width_minus1_fits_two_issue1542) {
  TempDir tmp;
  for (const auto& name : ls_issue1542_test::six_names()) {
    tmp.write(name, "");
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-C", L"-w", L"9"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "aa1  bb1\naa2  bb2\naa3  bb3\n");
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

// total == width (-C): GNU rejects two columns (8 < 8 is false) and
// prints one column; the head tree's <= accepts two. This is the
// divergent cell pinned by #1542.
TEST(ls, ls_grid_columns_total_eq_width_drops_to_one_issue1542) {
  TempDir tmp;
  for (const auto& name : ls_issue1542_test::six_names()) {
    tmp.write(name, "");
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-C", L"-w", L"8"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, ls_issue1542_test::one_column_text());
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

// total == width + 1 (-C): two columns fit under neither test, so
// both implementations print one column.
TEST(ls, ls_grid_columns_total_eq_width_plus1_drops_to_one_issue1542) {
  TempDir tmp;
  for (const auto& name : ls_issue1542_test::six_names()) {
    tmp.write(name, "");
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-C", L"-w", L"7"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, ls_issue1542_test::one_column_text());
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

// total == width - 1 (-x): two columns under both tests; -x fills
// across rows.
TEST(ls, ls_grid_across_total_eq_width_minus1_fits_two_issue1542) {
  TempDir tmp;
  for (const auto& name : ls_issue1542_test::six_names()) {
    tmp.write(name, "");
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-x", L"-w", L"9"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "aa1  aa2\naa3  bb1\nbb2  bb3\n");
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

// total == width (-x): same divergent cell as the -C form; GNU
// prints one column.
TEST(ls, ls_grid_across_total_eq_width_drops_to_one_issue1542) {
  TempDir tmp;
  for (const auto& name : ls_issue1542_test::six_names()) {
    tmp.write(name, "");
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-x", L"-w", L"8"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, ls_issue1542_test::one_column_text());
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

// total == width + 1 (-x): one column under both tests.
TEST(ls, ls_grid_across_total_eq_width_plus1_drops_to_one_issue1542) {
  TempDir tmp;
  for (const auto& name : ls_issue1542_test::six_names()) {
    tmp.write(name, "");
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-x", L"-w", L"7"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, ls_issue1542_test::one_column_text());
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

// Control cell: at an ample width both implementations agree on
// three columns (three-column total 13 < 14; four columns total 15
// is rejected by both tests). Guards the pins against a fixture or
// harness fault that would make every grid cell fail alike.
TEST(ls, ls_grid_columns_ample_width_three_columns_issue1542) {
  TempDir tmp;
  for (const auto& name : ls_issue1542_test::six_names()) {
    tmp.write(name, "");
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-C", L"-w", L"14"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "aa1  aa3  bb2\naa2  bb1  bb3\n");
  EXPECT_EQ_TEXT(r.stderr_text, "");
}
