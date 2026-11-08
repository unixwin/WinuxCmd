// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

// [#1515] GNU grep pads the line-number and byte-offset fields to a
// uniform width when -T/--initial-tab is in effect (grep.c print_offset
// prints "%*"PRIdMAX with offset_width; grep.c:1541-1550 computes the
// width once per input from the input size: digits of st_size, +1 when
// line numbers are printed, or digits of INTMAX_MAX = 19 when the input
// size is unusable, e.g. a pipe). Without -T the width is 0 and nothing
// is padded. Expectations below were taken byte-for-byte from GNU grep
// 3.11.

namespace {

// 15 lines; line 5 is "five MATCH", line 12 is "twelve MATCH".
// On-disk size is 55 bytes, so the -T width is digits(55 + 1) = 2.
std::string cell_a_text() {
  std::string text;
  for (int i = 1; i <= 15; ++i) {
    if (i == 5) {
      text += "five MATCH\n";
    } else if (i == 12) {
      text += "twelve MATCH\n";
    } else {
      text += std::to_string(i);
      text += '\n';
    }
  }
  return text;
}

// 98 lines of 10 bytes plus a final 19-byte line: exactly 999 bytes,
// the last line (number 99, byte offset 980) contains the match.
std::string cell_b_text() {
  std::string text;
  for (int i = 0; i < 98; ++i) text += "012345678\n";
  text += "needle MATCH xxxxx\n";
  return text;
}

}  // namespace

TEST(grep, grep_initial_tab_pads_context_line_numbers_issue1515) {
  TempDir tmp;
  tmp.write("a.txt", cell_a_text());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-T", L"-n", L"-C1", L"MATCH", L"a.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 " 4-\t4\n"
                 " 5:\tfive MATCH\n"
                 " 6-\t6\n"
                 "--\n"
                 "11-\t11\n"
                 "12:\ttwelve MATCH\n"
                 "13-\t13\n");
}

TEST(grep, grep_no_initial_tab_no_padding_issue1515) {
  TempDir tmp;
  tmp.write("a.txt", cell_a_text());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-n", L"-C1", L"MATCH", L"a.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "4-4\n"
                 "5:five MATCH\n"
                 "6-6\n"
                 "--\n"
                 "11-11\n"
                 "12:twelve MATCH\n"
                 "13-13\n");
}

TEST(grep, grep_initial_tab_byte_offset_width_issue1515) {
  TempDir tmp;
  tmp.write("b.txt", cell_b_text());

  // -b only: width is digits(999) = 3, the offset needs no padding.
  Pipeline offset_only;
  offset_only.set_cwd(tmp.wpath());
  offset_only.add(L"grep.exe", {L"-T", L"-b", L"MATCH", L"b.txt"});
  auto r1 = offset_only.run();
  EXPECT_EQ(r1.exit_code, 0);
  EXPECT_EQ_TEXT(r1.stdout_text, "980:\tneedle MATCH xxxxx\n");

  // -b -n: line numbers are origin-1, so the bound is 999 + 1 = 1000
  // and the shared width grows to 4 for BOTH fields.
  Pipeline both;
  both.set_cwd(tmp.wpath());
  both.add(L"grep.exe", {L"-T", L"-b", L"-n", L"MATCH", L"b.txt"});
  auto r2 = both.run();
  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_EQ_TEXT(r2.stdout_text, "  99: 980:\tneedle MATCH xxxxx\n");
}

TEST(grep, grep_initial_tab_stdin_pipe_width_19_issue1515) {
  // A pipe has no usable size, so GNU falls back to INTMAX_MAX and the
  // width is 19 for every line of the stdin input.
  Pipeline p;
  p.set_stdin(cell_a_text());
  p.add(L"grep.exe", {L"-T", L"-n", L"MATCH"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "                  5:\tfive MATCH\n"
                 "                 12:\ttwelve MATCH\n");
}

TEST(grep, grep_initial_tab_width_resets_per_file_issue1515) {
  TempDir tmp;
  tmp.write("a.txt", cell_a_text());
  // 200 lines of 10 bytes: size 2000, width digits(2000 + 1) = 4.
  std::string big;
  for (int i = 0; i < 200; ++i) {
    big += (i == 149) ? "MATCH 678\n" : "012345678\n";
  }
  tmp.write("d.txt", big);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-T", L"-n", L"MATCH", L"a.txt", L"d.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "a.txt: 5:\tfive MATCH\n"
                 "a.txt:12:\ttwelve MATCH\n"
                 "d.txt: 150:\tMATCH 678\n");
}

TEST(grep, grep_initial_tab_only_matching_padded_issue1515) {
  TempDir tmp;
  tmp.write("a.txt", cell_a_text());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-T", L"-n", L"-o", L"MATCH", L"a.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 " 5:\tMATCH\n"
                 "12:\tMATCH\n");
}
