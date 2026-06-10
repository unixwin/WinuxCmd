/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *  sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 *
 *  - File: fmt_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

static std::wstring fmt_exe() { return ProjectPaths::exe(L"fmt.exe").wstring(); }

TEST(fmt, fmt_basic) {
  TempDir tmp;
  tmp.write("test.txt",
            "This is a long line that should be formatted to fit within a "
            "specified width.\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(fmt_exe(), {L"-w", L"40", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(fmt, fmt_stdin) {
  Pipeline p;
  p.set_stdin("This is a long line that should be formatted.\n");
  p.add(fmt_exe(), {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(fmt, fmt_preserves_blank_lines_and_indentation) {
  Pipeline p;
  p.set_stdin("  alpha beta gamma delta epsilon zeta\n\nnext para words\n");
  p.add(fmt_exe(), {L"-w", L"16"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "  alpha beta\n"
                 "  gamma delta\n"
                 "  epsilon zeta\n"
                 "\n"
                 "next para words\n");
}

TEST(fmt, fmt_split_only_does_not_refill) {
  Pipeline p;
  p.set_stdin("alpha beta\ngamma delta\n");
  p.add(fmt_exe(), {L"-s", L"-w", L"8"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "alpha\nbeta\ngamma\ndelta\n");
}

TEST(fmt, fmt_prefix_only_formats_matching_lines) {
  auto r = run_command(fmt_exe(), {L"-p", L">", L"-w", L"12"},
                       "> alpha beta gamma\nplain * line\n> delta epsilon\n");

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "> alpha beta\n"
                 "> gamma\n"
                 "plain * line\n"
                 "> delta epsilon\n");
}

TEST(fmt, fmt_prefix_single_matching_line_is_left_verbatim) {
  auto r =
      run_command(fmt_exe(), {L"-p", L">", L"-w", L"12"}, "> delta epsilon\n");

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "> delta epsilon\n");
}

TEST(fmt, fmt_prefix_attachment_change_starts_new_paragraph) {
  auto r = run_command(fmt_exe(), {L"-p", L">", L"-w", L"12"},
                       ">alpha beta gamma\n> delta epsilon\n");

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 ">alpha beta\n"
                 ">gamma\n"
                 "> delta epsilon\n");
}

TEST(fmt, fmt_exact_prefix_does_not_match_after_indentation) {
  auto r = run_command(fmt_exe(), {L"-p", L">", L"-x", L"-w", L"12"},
                       "  > alpha beta gamma\n> delta epsilon\n");

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "  > alpha beta gamma\n"
                 "> delta epsilon\n");
}

TEST(fmt, fmt_skip_prefix_leaves_matching_lines_verbatim) {
  auto r = run_command(fmt_exe(), {L"-P", L"SKIP", L"-w", L"12"},
                       "alpha beta gamma\nSKIP left intact words\n"
                       "delta epsilon zeta\n");

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "alpha beta\n"
                 "gamma\n"
                 "SKIP left intact words\n"
                 "delta epsilon\n"
                 "zeta\n");
}

TEST(fmt, fmt_exact_skip_prefix_keeps_indented_line_formatting) {
  auto r = run_command(fmt_exe(), {L"-P", L"SKIP", L"-X", L"-w", L"12"},
                       "  SKIP left intact words\nSKIP exact line words\n"
                       "alpha beta gamma\n");

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "  SKIP left\n"
                 "  intact words\n"
                 "SKIP exact line words\n"
                 "alpha beta\n"
                 "gamma\n");
}

TEST(fmt, fmt_preserve_headers_detects_and_reflows_header_lines) {
  auto r = run_command(
      fmt_exe(), {L"-m", L"-w", L"20"},
      "Subject: alpha beta gamma delta\nBody words here keep wrapping maybe\n");

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "Subject: alpha beta\n"
                 "  gamma delta\n"
                 "Body words here keep\n"
                 "wrapping maybe\n");
}

TEST(fmt, fmt_preserve_headers_merges_continuation_lines) {
  auto r = run_command(
      fmt_exe(), {L"-m", L"-w", L"20"},
      "From: alpha beta gamma delta\n"
      "Subject: one two three four five six\n"
      "\tcontinued words here again\n"
      "Body words here keep wrapping maybe\n");

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "From: alpha beta gamma\n"
                 "  delta\n"
                 "Subject: one two\n"
                 "  three four five\n"
                 "  six continued\n"
                 "  words here again\n"
                 "Body words here keep\n"
                 "wrapping maybe\n");
}

TEST(fmt, fmt_uniform_spacing_adds_two_spaces_after_sentences) {
  Pipeline p;
  p.set_stdin("Hi. There now.\n");
  p.add(fmt_exe(), {L"-u", L"-w", L"30"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "Hi.  There now.\n");
}

TEST(fmt, fmt_default_tab_width_counts_indent_as_eight_columns) {
  Pipeline p;
  p.set_stdin("\talpha beta gamma delta\n");
  p.add(fmt_exe(), {L"-w", L"16"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "\talpha\n\tbeta\n\tgamma\n\tdelta\n");
}

TEST(fmt, fmt_tab_width_option_changes_indent_measurement_but_preserves_tabs) {
  Pipeline p;
  p.set_stdin("\talpha beta gamma delta\n");
  p.add(fmt_exe(), {L"-w", L"16", L"-T", L"4"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "\talpha beta\n\tgamma delta\n");
}

TEST(fmt, fmt_crown_margin_uses_second_line_indent) {
  Pipeline p;
  p.set_stdin(
      "  item one two three\n"
      "      continuation words more words\n");
  p.add(fmt_exe(), {L"-c", L"-w", L"30"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "  item one two three\n"
                 "      continuation words\n"
                 "      more words\n");
}

TEST(fmt, fmt_rejects_trailing_junk_width) {
  Pipeline p;
  p.set_stdin("alpha beta\n");
  p.add(fmt_exe(), {L"-w", L"10x"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
}

TEST(fmt, fmt_missing_input_reports_no_such_file) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(fmt_exe(), {L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "fmt: cannot open 'missing.txt' for reading: No such file "
                  "or directory") != std::string::npos);
}

TEST(fmt, fmt_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("indir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(fmt_exe(), {L"indir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(
      r.stderr_text.find("fmt: cannot open 'indir' for reading: Is a directory") !=
      std::string::npos);
}
