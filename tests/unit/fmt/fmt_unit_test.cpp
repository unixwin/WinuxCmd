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

TEST(fmt, fmt_basic) {
  TempDir tmp;
  tmp.write("test.txt",
            "This is a long line that should be formatted to fit within a "
            "specified width.\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"fmt.exe", {L"-w", L"40", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(fmt, fmt_stdin) {
  Pipeline p;
  p.set_stdin("This is a long line that should be formatted.\n");
  p.add(L"fmt.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(fmt, fmt_preserves_blank_lines_and_indentation) {
  Pipeline p;
  p.set_stdin("  alpha beta gamma delta epsilon zeta\n\nnext para words\n");
  p.add(L"fmt.exe", {L"-w", L"16"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "  alpha beta\n"
                 "  gamma delta\n"
                 "  epsilon zeta\n"
                 "\n"
                 "next para\n"
                 "words\n");
}

TEST(fmt, fmt_split_only_does_not_refill) {
  Pipeline p;
  p.set_stdin("alpha beta\ngamma delta\n");
  p.add(L"fmt.exe", {L"-s", L"-w", L"8"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "alpha\nbeta\ngamma\ndelta\n");
}

TEST(fmt, fmt_prefix_only_formats_matching_lines) {
  Pipeline p;
  p.set_stdin("> alpha beta gamma\nplain * line\n> delta epsilon\n");
  p.add(L"fmt.exe", {L"-p", L">", L"-w", L"12"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 ">alpha beta\n"
                 ">gamma\n"
                 "plain * line\n"
                 ">delta\n"
                 ">epsilon\n");
}

TEST(fmt, fmt_uniform_spacing_adds_two_spaces_after_sentences) {
  Pipeline p;
  p.set_stdin("Hi. There now.\n");
  p.add(L"fmt.exe", {L"-u", L"-w", L"30"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "Hi.  There now.\n");
}

TEST(fmt, fmt_crown_margin_uses_second_line_indent) {
  Pipeline p;
  p.set_stdin(
      "  item one two three\n"
      "      continuation words more words\n");
  p.add(L"fmt.exe", {L"-c", L"-w", L"30"});

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
  p.add(L"fmt.exe", {L"-w", L"10x"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
}
