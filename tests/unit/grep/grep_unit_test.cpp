/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the “Software”), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: grep_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(grep, grep_basic_match) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\nbeta\nalpha beta\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"alpha", L"a.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "alpha\nalpha beta\n");
}

TEST(grep, grep_color_auto_respects_terminal_state) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\n");

  Pipeline auto_color;
  auto_color.set_cwd(tmp.wpath());
  auto_color.add(L"grep.exe", {L"--color=auto", L"alpha", L"a.txt"});
  auto auto_result = auto_color.run();

  EXPECT_EQ(auto_result.exit_code, 0);
  EXPECT_EQ(auto_result.stdout_text.find("\x1b["), std::string::npos);

  Pipeline always_color;
  always_color.set_cwd(tmp.wpath());
  always_color.add(L"grep.exe", {L"--color=always", L"alpha", L"a.txt"});
  auto always_result = always_color.run();

  EXPECT_EQ(always_result.exit_code, 0);
  EXPECT_NE(always_result.stdout_text.find("\x1b["), std::string::npos);
}

TEST(grep, grep_color_optional_argument_defaults_to_auto) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"--color", L"alpha", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "alpha\n");
  EXPECT_EQ(r.stdout_text.find("\x1b["), std::string::npos);
}

TEST(grep, grep_color_never_and_colour_alias) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\n");

  Pipeline never;
  never.set_cwd(tmp.wpath());
  never.add(L"grep.exe", {L"--color=never", L"alpha", L"a.txt"});
  auto never_result = never.run();

  EXPECT_EQ(never_result.exit_code, 0);
  EXPECT_EQ_TEXT(never_result.stdout_text, "alpha\n");

  Pipeline alias;
  alias.set_cwd(tmp.wpath());
  alias.add(L"grep.exe", {L"--colour=always", L"alpha", L"a.txt"});
  auto alias_result = alias.run();

  EXPECT_EQ(alias_result.exit_code, 0);
  EXPECT_NE(alias_result.stdout_text.find("\x1b["), std::string::npos);
}

TEST(grep, grep_color_rejects_invalid_when) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"--color=bad", L"alpha", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(r.stderr_text.find("invalid color mode") != std::string::npos);
}

TEST(grep, grep_ignore_case_and_line_number) {
  TempDir tmp;
  tmp.write("a.txt", "One\nTWO\nthree\nTwo again\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-i", L"-n", L"two", L"a.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "2:TWO\n4:Two again\n");
}

TEST(grep, grep_line_buffered_option_is_accepted) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\nbeta\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"--line-buffered", L"alpha", L"a.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "alpha\n");
}

TEST(grep, grep_label_names_standard_input_when_filename_is_shown) {
  Pipeline p;
  p.set_stdin("needle\nhay\n");
  p.add(L"grep.exe", {L"-H", L"--label", L"virtual.txt", L"needle"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "virtual.txt:needle\n");
}

TEST(grep, grep_multiple_regexp_options_match_any_pattern) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\nbeta\ngamma\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-e", L"alpha", L"-e", L"beta", L"a.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "alpha\nbeta\n");
}

TEST(grep, grep_multiple_long_regexp_options) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\nbeta\ngamma\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"--regexp=alpha", L"--regexp", L"gamma", L"a.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "alpha\ngamma\n");
}

TEST(grep, grep_multiple_pattern_files) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\nbeta\ngamma\n");
  tmp.write("p1.txt", "alpha\n");
  tmp.write("p2.txt", "gamma\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-f", L"p1.txt", L"-f", L"p2.txt", L"a.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "alpha\ngamma\n");
}

TEST(grep, grep_mixes_regexp_and_pattern_file) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\nbeta\ngamma\n");
  tmp.write("patterns.txt", "gamma\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-e", L"alpha", L"-f", L"patterns.txt", L"a.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "alpha\ngamma\n");
}

TEST(grep, grep_empty_pattern_file_matches_nothing) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\nbeta\n");
  tmp.write("empty.patterns", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-f", L"empty.patterns", L"a.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "");
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

TEST(grep, grep_pattern_file_empty_line_matches_every_line) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\nbeta\n");
  tmp.write("empty-line.patterns", "\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-f", L"empty-line.patterns", L"a.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "alpha\nbeta\n");
}

TEST(grep, grep_empty_regexp_matches_every_line) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\nbeta\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-e", L"", L"a.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "alpha\nbeta\n");
}

TEST(grep, grep_count_and_files_with_matches) {
  TempDir tmp;
  tmp.write("a.txt", "x\ny\nx\n");
  tmp.write("b.txt", "z\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"grep.exe", {L"-c", L"x", L"a.txt", L"b.txt"});
  auto r1 = p1.run();

  EXPECT_EQ(r1.exit_code, 0);
  EXPECT_TRUE(r1.stdout_text.find("a.txt:2") != std::string::npos);
  EXPECT_TRUE(r1.stdout_text.find("b.txt:0") != std::string::npos);

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"grep.exe", {L"-l", L"x", L"a.txt", L"b.txt"});
  auto r2 = p2.run();

  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_EQ_TEXT(r2.stdout_text, "a.txt\n");
}

TEST(grep, grep_recursive_search) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "d1" / "d2");
  tmp.write("d1/a.txt", "needle\n");
  tmp.write("d1/d2/b.txt", "none\nneedle\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-r", L"needle", L"d1"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("needle") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("a.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b.txt") != std::string::npos);
}

TEST(grep, grep_recursive_alias_R) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "d1");
  tmp.write("d1/a.txt", "needle\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-R", L"needle", L"d1"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("needle") != std::string::npos);
}

TEST(grep, grep_exclude_dir_skips_nested_directory) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "keep" / "skipme");
  tmp.write("keep/a.txt", "needle-keep\n");
  tmp.write("keep/skipme/b.txt", "needle-skip\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-r", L"--exclude-dir", L"skipme", L"needle", L"keep"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("needle-keep") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("needle-skip") == std::string::npos);
}

TEST(grep, grep_exclude_dir_trailing_slash_skips_matching_directory) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "root" / "skipme");
  tmp.write("root/keep.txt", "needle-keep\n");
  tmp.write("root/skipme/skip.txt", "needle-skip\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-r", L"--exclude-dir", L"skipme/", L"needle", L"root"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("needle-keep") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("needle-skip") == std::string::npos);
}

TEST(grep, grep_exclude_dir_skips_command_line_directory) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "skipme");
  tmp.write("skipme/a.txt", "needle\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe",
        {L"-r", L"--exclude-dir", L"skipme", L"needle", L"skipme"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "");
}

TEST(grep, grep_basic_and_extended_regex_semantics) {
  TempDir tmp;
  tmp.write("a.txt", "aaa\na+\n");

  Pipeline basic;
  basic.set_cwd(tmp.wpath());
  basic.add(L"grep.exe", {L"a+", L"a.txt"});
  auto basic_result = basic.run();

  EXPECT_EQ(basic_result.exit_code, 0);
  EXPECT_EQ_TEXT(basic_result.stdout_text, "a+\n");

  Pipeline extended;
  extended.set_cwd(tmp.wpath());
  extended.add(L"grep.exe", {L"-E", L"a+", L"a.txt"});
  auto extended_result = extended.run();

  EXPECT_EQ(extended_result.exit_code, 0);
  EXPECT_EQ_TEXT(extended_result.stdout_text, "aaa\na+\n");
}

TEST(grep, grep_regexp_mode_last_option_wins) {
  TempDir tmp;
  tmp.write("a.txt", "aaa\na+\n");

  Pipeline extended_last;
  extended_last.set_cwd(tmp.wpath());
  extended_last.add(L"grep.exe", {L"-G", L"-E", L"a+", L"a.txt"});
  auto extended_result = extended_last.run();

  EXPECT_EQ(extended_result.exit_code, 0);
  EXPECT_EQ_TEXT(extended_result.stdout_text, "aaa\na+\n");

  Pipeline fixed_last;
  fixed_last.set_cwd(tmp.wpath());
  fixed_last.add(L"grep.exe", {L"-E", L"-F", L"a+", L"a.txt"});
  auto fixed_result = fixed_last.run();

  EXPECT_EQ(fixed_result.exit_code, 0);
  EXPECT_EQ_TEXT(fixed_result.stdout_text, "a+\n");
}

TEST(grep, grep_ignore_case_last_option_wins) {
  TempDir tmp;
  tmp.write("a.txt", "TWO\n");

  Pipeline ignore_last;
  ignore_last.set_cwd(tmp.wpath());
  ignore_last.add(L"grep.exe", {L"--no-ignore-case", L"-i", L"two", L"a.txt"});
  auto ignore_result = ignore_last.run();

  EXPECT_EQ(ignore_result.exit_code, 0);
  EXPECT_EQ_TEXT(ignore_result.stdout_text, "TWO\n");

  Pipeline no_ignore_last;
  no_ignore_last.set_cwd(tmp.wpath());
  no_ignore_last.add(L"grep.exe",
                     {L"-i", L"--no-ignore-case", L"two", L"a.txt"});
  auto no_ignore_result = no_ignore_last.run();

  EXPECT_EQ(no_ignore_result.exit_code, 1);
  EXPECT_EQ_TEXT(no_ignore_result.stdout_text, "");
}

TEST(grep, grep_filename_prefix_last_option_wins) {
  TempDir tmp;
  tmp.write("a.txt", "needle\n");

  Pipeline no_filename_last;
  no_filename_last.set_cwd(tmp.wpath());
  no_filename_last.add(L"grep.exe", {L"-H", L"-h", L"needle", L"a.txt"});
  auto no_filename_result = no_filename_last.run();

  EXPECT_EQ(no_filename_result.exit_code, 0);
  EXPECT_EQ_TEXT(no_filename_result.stdout_text, "needle\n");

  Pipeline filename_last;
  filename_last.set_cwd(tmp.wpath());
  filename_last.add(L"grep.exe", {L"-h", L"-H", L"needle", L"a.txt"});
  auto filename_result = filename_last.run();

  EXPECT_EQ(filename_result.exit_code, 0);
  EXPECT_EQ_TEXT(filename_result.stdout_text, "a.txt:needle\n");
}

TEST(grep, grep_context_last_option_wins_per_side) {
  TempDir tmp;
  tmp.write("a.txt", "before\nneedle\nafter\n");

  Pipeline after_zero;
  after_zero.set_cwd(tmp.wpath());
  after_zero.add(L"grep.exe", {L"-C", L"1", L"-A", L"0", L"needle", L"a.txt"});
  auto after_zero_result = after_zero.run();

  EXPECT_EQ(after_zero_result.exit_code, 0);
  EXPECT_EQ_TEXT(after_zero_result.stdout_text, "before\nneedle\n");
}

TEST(grep, grep_context_lines_use_hyphen_prefix_separator) {
  TempDir tmp;
  tmp.write("a.txt", "before\nneedle\nafter\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-n", L"-C", L"1", L"needle", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1-before\n2:needle\n3-after\n");
}

TEST(grep, grep_context_ranges_merge_without_duplicates_or_separator) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\nneedle one\nmiddle\nneedle two\nomega\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-n", L"-C", L"1", L"needle", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "1-alpha\n2:needle one\n3-middle\n4:needle two\n5-omega\n");
}

TEST(grep, grep_context_ranges_print_group_separator_for_real_gap) {
  TempDir tmp;
  tmp.write("a.txt",
            "alpha\nneedle one\nbeta\ngap\ndelta\nneedle two\nomega\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-n", L"-C", L"1", L"needle", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "1-alpha\n2:needle one\n3-beta\n--\n5-delta\n6:needle "
                 "two\n7-omega\n");
}

TEST(grep, grep_only_matching) {
  TempDir tmp;
  tmp.write("a.txt", "abc123def123\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-o", L"123", L"a.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "123\n123\n");
}

TEST(grep, grep_only_matching_warns_and_ignores_context) {
  TempDir tmp;
  tmp.write("a.txt", "before\nabc123def123\nafter\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-o", L"-C", L"1", L"123", L"a.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "123\n123\n");
  EXPECT_TRUE(r.stderr_text.find("warning") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("context") != std::string::npos);
}

TEST(grep, grep_word_regexp) {
  TempDir tmp;
  tmp.write("a.txt", "cat\nconcatenate\ncat-dog\ncat_dog\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-w", L"cat", L"a.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "cat\ncat-dog\n");
}

TEST(grep, grep_line_regexp) {
  TempDir tmp;
  tmp.write("a.txt", "needle\nneedle in haystack\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-x", L"needle", L"a.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "needle\n");
}

TEST(grep, grep_unsupported_perl_regexp) {
  TempDir tmp;
  tmp.write("a.txt", "x\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-P", L"x", L"a.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 2);
}

TEST(grep, grep_devices_action_accepts_read_and_skip) {
  TempDir tmp;
  tmp.write("a.txt", "needle\n");

  Pipeline read_action;
  read_action.set_cwd(tmp.wpath());
  read_action.add(L"grep.exe", {L"-D", L"read", L"needle", L"a.txt"});
  auto read_result = read_action.run();

  EXPECT_EQ(read_result.exit_code, 0);
  EXPECT_EQ_TEXT(read_result.stdout_text, "needle\n");

  Pipeline skip_action;
  skip_action.set_cwd(tmp.wpath());
  skip_action.add(L"grep.exe", {L"--devices=skip", L"needle", L"a.txt"});
  auto skip_result = skip_action.run();

  EXPECT_EQ(skip_result.exit_code, 0);
  EXPECT_EQ_TEXT(skip_result.stdout_text, "needle\n");
}

TEST(grep, grep_devices_rejects_invalid_action) {
  Pipeline p;
  p.add(L"grep.exe", {L"--devices=bad", L"needle"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(r.stderr_text.find("invalid devices action") !=
              std::string::npos);
}

TEST(grep, grep_wildcard_files) {
  TempDir tmp;
  tmp.write("file1.txt", "hello world\n");
  tmp.write("file2.txt", "hello again\n");
  tmp.write("other.log", "no match here\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"hello", L"*.txt"});

  TEST_LOG_CMD_LIST("grep.exe", L"hello", L"*.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("grep.exe hello *.txt output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("file1.txt:hello world") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file2.txt:hello again") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("other.log") == std::string::npos);
}

TEST(grep, grep_recursive_wildcard_directory_operands_recurse) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "root" / "sub");
  tmp.write("root/sub/a.txt", "needle\n");
  tmp.write("root/top.txt", "not here\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-r", L"needle", L"root\\*"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("sub") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("a.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("needle") != std::string::npos);
}

TEST(grep, grep_pattern_operand_is_literal_not_glob) {
  TempDir tmp;
  tmp.write("hallo", "not an input\n");
  tmp.write("data.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"h[ae]llo", L"data.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "hello\n");
}

TEST(grep, grep_wildcard_pattern) {
  TempDir tmp;
  tmp.write("test.cpp", "#include <iostream>\n");
  tmp.write("test.h", "#pragma once\n");
  tmp.write("test.txt", "just text\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"#include", L"*.cpp"});

  TEST_LOG_CMD_LIST("grep.exe", L"#include", L"*.cpp");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("grep output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("#include <iostream>") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("#pragma once") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("just text") == std::string::npos);
}

TEST(grep, grep_exclude_from_file) {
  TempDir tmp;
  tmp.write("keep.txt", "needle\n");
  tmp.write("keep2.txt", "needle\n");
  tmp.write("skip.log", "needle\n");
  tmp.write("exclude.lst", "skip.*\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe",
        {L"--exclude-from", L"exclude.lst", L"needle", L"*.txt", L"*.log"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("keep.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("keep2.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("skip.log") == std::string::npos);
}

TEST(grep, grep_include_filters_recursive_files) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "src");
  tmp.write("src/a.cpp", "needle-cpp\n");
  tmp.write("src/a.txt", "needle-txt\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-r", L"--include", L"*.cpp", L"needle", L"src"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("needle-cpp") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("needle-txt") == std::string::npos);
}

TEST(grep, grep_include_exclude_last_matching_rule_wins) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "src");
  tmp.write("src/keep.cpp", "needle-keep\n");
  tmp.write("src/skip.cpp", "needle-skip\n");
  tmp.write("src/readme.txt", "needle-readme\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-r", L"--include", L"*.cpp", L"--exclude", L"skip.cpp",
                      L"needle", L"src"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("needle-keep") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("needle-skip") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("needle-readme") == std::string::npos);
}

TEST(grep, grep_exclude_then_include_restores_later_match) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "src");
  tmp.write("src/keep.cpp", "needle-keep\n");
  tmp.write("src/skip.cpp", "needle-skip\n");
  tmp.write("src/readme.txt", "needle-readme\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-r", L"--exclude", L"*.cpp", L"--include", L"keep.cpp",
                      L"needle", L"src"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("needle-keep") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("needle-skip") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("needle-readme") != std::string::npos);
}

TEST(grep, grep_include_exclude_match_command_line_name_suffixes) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "src");
  tmp.write("src/keep.cpp", "needle-keep\n");
  tmp.write("src/skip.cpp", "needle-skip\n");
  tmp.write("src/readme.txt", "needle-readme\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe",
        {L"--include", L"*.cpp", L"--exclude", L"src/skip.cpp", L"needle",
         L"src/keep.cpp", L"src/skip.cpp", L"src/readme.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("needle-keep") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("needle-skip") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("needle-readme") == std::string::npos);
}

TEST(grep, grep_repeated_exclude_from_files_all_apply) {
  TempDir tmp;
  tmp.write("keep.txt", "needle-keep\n");
  tmp.write("skip1.txt", "needle-skip1\n");
  tmp.write("skip2.log", "needle-skip2\n");
  tmp.write("exclude1.lst", "skip1.*\n");
  tmp.write("exclude2.lst", "skip2.*\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"--exclude-from", L"exclude1.lst", L"--exclude-from",
                      L"exclude2.lst", L"needle", L"*.txt", L"*.log"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("needle-keep") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("needle-skip1") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("needle-skip2") == std::string::npos);
}

TEST(grep, grep_repeated_exclude_dir_patterns_all_apply) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "root" / "keep");
  std::filesystem::create_directories(tmp.path / "root" / "skip1");
  std::filesystem::create_directories(tmp.path / "root" / "skip2");
  tmp.write("root/keep/a.txt", "needle-keep\n");
  tmp.write("root/skip1/a.txt", "needle-skip1\n");
  tmp.write("root/skip2/a.txt", "needle-skip2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-r", L"--exclude-dir", L"skip1", L"--exclude-dir",
                      L"skip2", L"needle", L"root"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("needle-keep") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("needle-skip1") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("needle-skip2") == std::string::npos);
}

TEST(grep, grep_binary_files_without_match_skips_binary_content) {
  TempDir tmp;
  tmp.write_bytes("binary.dat", {'a', 'l', 'p', 'h', 'a', '\0', 'n', 'e', 'e',
                                 'd', 'l', 'e', '\n'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-I", L"needle", L"binary.dat"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "");
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

TEST(grep, grep_binary_files_text_searches_binary_content) {
  TempDir tmp;
  tmp.write_bytes("binary.dat", {'a', 'l', 'p', 'h', 'a', '\0', 'n', 'e', 'e',
                                 'd', 'l', 'e', '\n'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"--binary-files=text", L"needle", L"binary.dat"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("needle") != std::string::npos);
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

TEST(grep, grep_binary_files_default_reports_binary_match) {
  TempDir tmp;
  tmp.write_bytes("binary.dat", {'a', 'l', 'p', 'h', 'a', '\0', 'n', 'e', 'e',
                                 'd', 'l', 'e', '\n'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"needle", L"binary.dat"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "");
  EXPECT_TRUE(r.stderr_text.find("binary.dat: binary file matches") !=
              std::string::npos);
}

TEST(grep, grep_binary_files_default_quiet_suppresses_diagnostic) {
  TempDir tmp;
  tmp.write_bytes("binary.dat", {'a', 'l', 'p', 'h', 'a', '\0', 'n', 'e', 'e',
                                 'd', 'l', 'e', '\n'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-q", L"needle", L"binary.dat"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "");
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

TEST(grep, grep_binary_files_default_no_messages_keeps_diagnostic) {
  TempDir tmp;
  tmp.write_bytes("binary.dat", {'a', 'l', 'p', 'h', 'a', '\0', 'n', 'e', 'e',
                                 'd', 'l', 'e', '\n'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-s", L"needle", L"binary.dat"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "");
  EXPECT_TRUE(r.stderr_text.find("binary.dat: binary file matches") !=
              std::string::npos);
}
