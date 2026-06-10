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
 *  - File: xargs_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(xargs, xargs_basic) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {});
  p.set_stdin("file1\nfile2\nfile3\n");

  TEST_LOG_CMD_LIST("xargs.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("xargs output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("file1") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file2") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file3") != std::string::npos);
}

TEST(xargs, xargs_max_args) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-n", L"2"});
  p.set_stdin("a\nb\nc\nd\ne\n");

  TEST_LOG_CMD_LIST("xargs.exe", L"-n", L"2");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("xargs -n output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should execute echo multiple times, each with 2 args
  EXPECT_TRUE(r.stdout_text.find("a b") != std::string::npos ||
              r.stdout_text.find("a") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("c d") != std::string::npos ||
              r.stdout_text.find("c") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("e") != std::string::npos);
}

TEST(xargs, xargs_default_echo_has_no_trailing_space) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {});
  p.set_stdin("a b c\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a b c\n");
}

TEST(xargs, xargs_default_echo_max_args_has_no_trailing_space) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-n", L"2"});
  p.set_stdin("a b c\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a b\nc\n");
}

TEST(xargs, xargs_no_run_if_empty) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-r"});
  p.set_stdin("");

  TEST_LOG_CMD_LIST("xargs.exe", L"-r");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("xargs -r output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(xargs, xargs_no_run_if_empty_still_runs_for_whitespace_only_input) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-r", L"cmd.exe", L"/C", L"echo", L"ran"});
  p.set_stdin("   \r\n\t \r\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("ran") != std::string::npos);
}

TEST(xargs, xargs_default_echo) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {});
  p.set_stdin("hello\nworld\n");

  TEST_LOG_CMD_LIST("xargs.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("xargs default echo output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("hello") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("world") != std::string::npos);
}

TEST(xargs, xargs_verbose) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-t"});
  p.set_stdin("test\n");

  TEST_LOG_CMD_LIST("xargs.exe", L"-t");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("xargs -t output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("test") != std::string::npos);
  // With -t, should also see the command in stderr
  EXPECT_TRUE(r.stderr_text.find("echo") != std::string::npos);
}

TEST(xargs, xargs_interactive_yes_runs_command) {
  TempDir tmp;
  tmp.write("args.txt", "alpha\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-p", L"-a", L"args.txt", L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("y\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("alpha") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("cmd.exe") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("?...") != std::string::npos);
}

TEST(xargs, xargs_interactive_no_skips_command) {
  TempDir tmp;
  tmp.write("args.txt", "alpha\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe",
        {L"--interactive", L"-a", L"args.txt", L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("n\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("alpha") == std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("cmd.exe") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("?...") != std::string::npos);
}

TEST(xargs, xargs_null_input_preserves_spaces_with_replacement) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe",
        {L"-0", L"-I", L"{}", L"cmd.exe", L"/C", L"echo", L"[{}]"});
  p.set_stdin(std::string("two words\0next\0", 15));

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("[two words]") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("[next]") != std::string::npos);
}

TEST(xargs, xargs_replacement_uses_one_input_line_per_command) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-I", L"{}", L"cmd.exe", L"/C", L"echo", L"[{}]"});
  p.set_stdin("two words\nnext\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("[two words]") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("[next]") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("[two words next]") == std::string::npos);
}

TEST(xargs, xargs_replacement_trailing_blank_continues_logical_line) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-I", L"{}", L"cmd.exe", L"/C", L"echo", L"[{}]"});
  p.set_stdin("alpha \nbeta\ngamma\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("[alpha beta]") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("[gamma]") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("[alpha]") == std::string::npos);
}

TEST(xargs, xargs_default_echo_replacement_uses_one_input_line_per_command) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-I", L"{}"});
  p.set_stdin("two words\nnext\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "two words\nnext\n");
}

TEST(xargs, xargs_replacement_ignores_blank_input_lines) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-I", L"{}", L"cmd.exe", L"/C", L"echo", L"[{}]"});
  p.set_stdin("alpha\n\n \t \nbeta\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("[alpha]") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("[beta]") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("[]") == std::string::npos);
}

TEST(xargs, xargs_deprecated_replace_defaults_to_braces) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-i", L"cmd.exe", L"/C", L"echo", L"[{}]"});
  p.set_stdin("one\ntwo\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("[one]") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("[two]") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("[one two]") == std::string::npos);
}

TEST(xargs, xargs_long_replace_defaults_to_braces) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"--replace", L"cmd.exe", L"/C", L"echo", L"[{}]"});
  p.set_stdin("one\ntwo\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("[one]") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("[two]") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("[one two]") == std::string::npos);
}

TEST(xargs, xargs_default_parser_respects_quotes) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("\"two words\" plain\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("two words plain") != std::string::npos);
}

TEST(xargs, xargs_default_parser_respects_backslash_escaping) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("one\\ two\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("one two") != std::string::npos);
}

TEST(xargs, xargs_default_parser_preserves_trailing_backslash_at_eof) {
  TempDir tmp;
  tmp.write("input.txt", "alpha\\");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-a", L"input.txt", L"cmd.exe", L"/C", L"echo"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_TRUE(r.stdout_text.find("alpha\\") != std::string::npos);
}

TEST(xargs, xargs_default_parser_rejects_unmatched_double_quote_with_gnu_hint) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("\"unterminated\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(
      r.stderr_text.find("xargs: unmatched double quote; by default quotes "
                         "are special to xargs unless you use the -0 option") !=
      std::string::npos);
}

TEST(xargs, xargs_default_parser_rejects_unmatched_single_quote_with_gnu_hint) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("'unterminated\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(
      r.stderr_text.find("xargs: unmatched single quote; by default quotes "
                         "are special to xargs unless you use the -0 option") !=
      std::string::npos);
}

TEST(xargs, xargs_input_items_are_literal_not_globs) {
  TempDir tmp;
  tmp.write("one.txt", "1\n");
  tmp.write("two.txt", "2\n");
  tmp.write("a.txt", "a\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("*.txt a[ab].txt\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("*.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("a[ab].txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("one.txt two.txt") == std::string::npos);
}

TEST(xargs, xargs_command_template_args_expand_wildcards) {
  TempDir tmp;
  tmp.write("one.txt", "1\n");
  tmp.write("two.txt", "2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"cmd.exe", L"/C", L"echo", L"*.txt"});
  p.set_stdin("stdinarg\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("one.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("two.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("stdinarg") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("*.txt") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("one.txt two.txt stdinarg") !=
              std::string::npos);
}

TEST(xargs, xargs_command_template_args_expand_char_class_wildcards) {
  TempDir tmp;
  tmp.write("aa.txt", "1\n");
  tmp.write("ab.txt", "2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"cmd.exe", L"/C", L"echo", L"a[ab].txt"});
  p.set_stdin("stdinarg\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("aa.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("ab.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("stdinarg") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("a[ab].txt") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("aa.txt ab.txt stdinarg") !=
              std::string::npos);
}

TEST(xargs, xargs_command_template_args_expand_parent_segment_star_wildcards) {
  TempDir tmp;
  tmp.write("dir1\\one.txt", "1\n");
  tmp.write("dir2\\two.txt", "2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"cmd.exe", L"/C", L"echo", L"dir*\\*.txt"});
  p.set_stdin("stdinarg\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("dir1\\one.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("dir2\\two.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("stdinarg") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("dir*\\*.txt") == std::string::npos);
}

TEST(xargs, xargs_command_template_args_expand_parent_segment_question_wildcards) {
  TempDir tmp;
  tmp.write("dir1\\one.txt", "1\n");
  tmp.write("dir2\\two.txt", "2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"cmd.exe", L"/C", L"echo", L"dir?\\*.txt"});
  p.set_stdin("stdinarg\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("dir1\\one.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("dir2\\two.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("stdinarg") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("dir?\\*.txt") == std::string::npos);
}

TEST(xargs, xargs_command_template_args_keep_literal_when_wildcard_has_no_match) {
  TempDir tmp;
  tmp.write("one.txt", "1\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"cmd.exe", L"/C", L"echo", L"missing*.txt"});
  p.set_stdin("stdinarg\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("missing*.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("stdinarg") != std::string::npos);
}

TEST(xargs, xargs_custom_delimiter) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-d", L",", L"-n", L"1", L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("alpha,beta,gamma");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("alpha") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("beta") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("gamma") != std::string::npos);
}

TEST(xargs, xargs_rejects_empty_short_delimiter) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe",
        {L"-d", L"", L"-n", L"1", L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("alpha,beta");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("xargs: delimiter must not be empty") !=
              std::string::npos);
}

TEST(xargs, xargs_rejects_empty_long_delimiter) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe",
        {L"--delimiter=", L"-n", L"1", L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("alpha,beta");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("xargs: delimiter must not be empty") !=
              std::string::npos);
}

TEST(xargs, xargs_rejects_incomplete_hex_delimiter_escape) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe",
        {L"-d", L"\\x", L"-n", L"1", L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("alpha");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("xargs: invalid hex delimiter escape") !=
              std::string::npos);
}

TEST(xargs, xargs_rejects_unknown_named_delimiter_escape) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe",
        {L"-d", L"\\q", L"-n", L"1", L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("alpha");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("xargs: invalid delimiter escape") !=
              std::string::npos);
}

TEST(xargs, xargs_delimiter_escape_keeps_spaces_literal) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-d", L"\\n", L"-n", L"1", L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("two words\nnext\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("two words") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("next") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("two words next") == std::string::npos);
}

TEST(xargs, xargs_delimiter_preserves_empty_items_between_separators) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe",
        {L"-d", L"\\n", L"-I", L"{}", L"cmd.exe", L"/C", L"echo", L"[{}]"});
  p.set_stdin("alpha\n\nbeta\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("[alpha]") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("[]") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("[beta]") != std::string::npos);
}

TEST(xargs, xargs_null_mode_preserves_empty_items_between_separators) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe",
        {L"-0", L"-I", L"{}", L"cmd.exe", L"/C", L"echo", L"[{}]"});
  p.set_stdin(std::string("alpha\0\0beta\0", 12));

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("[alpha]") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("[]") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("[beta]") != std::string::npos);
}

TEST(xargs, xargs_logical_eof_is_ignored_in_delimiter_mode) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe",
        {L"-d", L"\\n", L"-E", L"STOP", L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("alpha\nSTOP\nbeta\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("alpha") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("STOP") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("beta") != std::string::npos);
}

TEST(xargs, xargs_logical_eof_is_ignored_in_null_mode) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe",
        {L"-0", L"-E", L"STOP", L"cmd.exe", L"/C", L"echo"});
  p.set_stdin(std::string("alpha\0STOP\0beta\0", 16));

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("alpha") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("STOP") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("beta") != std::string::npos);
}

TEST(xargs, xargs_max_procs_option_is_accepted) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-P", L"2", L"-n", L"1", L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("alpha\nbeta\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("alpha") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("beta") != std::string::npos);
}

TEST(xargs, xargs_max_procs_zero_is_accepted) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-P", L"0", L"-n", L"1", L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("alpha\nbeta\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("alpha") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("beta") != std::string::npos);
}

TEST(xargs, xargs_max_procs_reuses_any_completed_slot_instead_of_fifo_waiting) {
  TempDir tmp;
  tmp.write("runner.cmd",
            "@echo off\r\n"
            "if \"%1\"==\"slow\" (\r\n"
            "  echo slow-start>>order.txt\r\n"
            "  ping -n 5 127.0.0.1 >nul\r\n"
            "  echo slow-end>>order.txt\r\n"
            "  exit /B 0\r\n"
            ")\r\n"
            "if \"%1\"==\"fast\" (\r\n"
            "  echo fast-start>>order.txt\r\n"
            "  ping -n 2 127.0.0.1 >nul\r\n"
            "  echo fast-end>>order.txt\r\n"
            "  exit /B 0\r\n"
            ")\r\n"
            "echo third-start>>order.txt\r\n"
            "ping -n 2 127.0.0.1 >nul\r\n"
            "echo third-end>>order.txt\r\n"
            "exit /B 0\r\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-P", L"2", L"-n", L"1", L"cmd.exe", L"/C",
                       L"runner.cmd"});
  p.set_stdin("slow\nfast\nthird\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);

  std::ifstream order_file(tmp.path / "order.txt");
  EXPECT_TRUE(order_file.good());
  if (!order_file.good()) return;
  std::string order((std::istreambuf_iterator<char>(order_file)),
                    std::istreambuf_iterator<char>());

  size_t slow_end = order.find("slow-end");
  size_t third_start = order.find("third-start");
  EXPECT_TRUE(slow_end != std::string::npos);
  EXPECT_TRUE(third_start != std::string::npos);
  EXPECT_LT(third_start, slow_end);
}

TEST(xargs, xargs_process_slot_var_is_exported_to_children) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"--process-slot-var", L"XSLOT", L"-n", L"1", L"cmd.exe",
                       L"/C", L"echo", L"%XSLOT%"});
  p.set_stdin("alpha\nbeta\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("0 alpha") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("0 beta") != std::string::npos);
}

TEST(xargs, xargs_arg_file_reads_from_file_instead_of_stdin) {
  TempDir tmp;
  tmp.write("args.txt", "file_arg\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-a", L"args.txt", L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("stdin_arg\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("file_arg") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("stdin_arg") == std::string::npos);
}

TEST(xargs, xargs_missing_arg_file_reports_gnu_shaped_diagnostic) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-a", L"missing.txt", L"cmd.exe", L"/C", L"echo"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "xargs: Cannot open input file 'missing.txt': No such file "
                 "or directory\n");
}

TEST(xargs, xargs_max_lines_groups_by_input_lines) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-L", L"2", L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("a b\nc d\ne f\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("a b c d") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("e f") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("a b c d e f") == std::string::npos);
}

TEST(xargs, xargs_conflict_max_lines_then_max_args_last_wins) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-L", L"2", L"-n", L"3", L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("a\nb\nc\nd\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "xargs: warning: options --max-lines and --max-args/-n are mutually "
      "exclusive, ignoring previous --max-lines value\n");
  EXPECT_TRUE(r.stdout_text.find("a b c") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("d") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("a b\r\nc d") == std::string::npos);
}

TEST(xargs, xargs_conflict_max_args_then_max_lines_last_wins) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-n", L"3", L"-L", L"2", L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("a\nb\nc\nd\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "xargs: warning: options --max-args and --max-lines/-L/-l are mutually "
      "exclusive, ignoring previous --max-args value\n");
  EXPECT_TRUE(r.stdout_text.find("a b") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("c d") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("a b c") == std::string::npos);
}

TEST(xargs, xargs_conflict_replace_then_max_args_last_wins) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe",
        {L"-I", L"{}", L"-n", L"2", L"cmd.exe", L"/C", L"echo", L"[{}]"});
  p.set_stdin("a\nb\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "xargs: warning: options --replace/-I/-i and --max-args/-n are "
      "mutually exclusive, ignoring previous --replace value\n");
  EXPECT_TRUE(r.stdout_text.find("[{}] a b") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("[a]") == std::string::npos);
}

TEST(xargs, xargs_conflict_replace_then_max_args_one_is_silent) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe",
        {L"-I", L"{}", L"-n", L"1", L"cmd.exe", L"/C", L"echo", L"[{}]"});
  p.set_stdin("a\nb\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("mutually exclusive") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("[a]") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("[b]") != std::string::npos);
}

TEST(xargs, xargs_conflict_max_lines_then_replace_last_wins) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe",
        {L"-L", L"2", L"-I", L"{}", L"cmd.exe", L"/C", L"echo", L"[{}]"});
  p.set_stdin("a\nb\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "xargs: warning: options --max-lines and --replace/-I/-i are mutually "
      "exclusive, ignoring previous --max-lines value\n");
  EXPECT_TRUE(r.stdout_text.find("[a]") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("[b]") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("[a b]") == std::string::npos);
}

TEST(xargs, xargs_conflict_replace_then_max_lines_last_wins) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe",
        {L"-I", L"{}", L"-L", L"2", L"cmd.exe", L"/C", L"echo", L"[{}]"});
  p.set_stdin("a\nb\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "xargs: warning: options --replace/-I/-i and --max-lines/-L/-l are "
      "mutually exclusive, ignoring previous --replace value\n");
  EXPECT_TRUE(r.stdout_text.find("[{}] a b") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("[a]") == std::string::npos);
}

TEST(xargs, xargs_max_lines_treats_trailing_blank_as_continuation) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-L", L"1", L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("alpha \nbeta\ngamma\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("alpha beta") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("gamma") != std::string::npos);
}

TEST(xargs, xargs_show_limits_reports_command_buffer) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"--show-limits", L"-r"});
  p.set_stdin("");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("Maximum length of command") !=
              std::string::npos);
}

TEST(xargs, xargs_max_chars_above_windows_limit_is_clamped_with_warning) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"--show-limits", L"-r", L"-s", L"40000"});
  p.set_stdin("");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("value for -s option is too large") !=
              std::string::npos);
  EXPECT_TRUE(
      r.stderr_text.find("Size of command buffer we are actually using: 32767") !=
      std::string::npos);
}

TEST(xargs, xargs_eof_stops_reading_at_marker_line) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-E", L"STOP", L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("one\nSTOP\ntwo\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("one") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("two") == std::string::npos);
}

TEST(xargs, xargs_deprecated_eof_alias_stops_reading_at_marker_line) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-eSTOP", L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("one\nSTOP\ntwo\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("one") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("two") == std::string::npos);
}

TEST(xargs, xargs_deprecated_max_lines_alias_groups_input_lines) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-l1", L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("alpha beta\ngamma delta\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("alpha beta") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("gamma delta") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("alpha beta gamma delta") ==
              std::string::npos);
}

TEST(xargs, xargs_deprecated_max_lines_alias_defaults_to_one) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-l", L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("alpha beta\ngamma delta\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("alpha beta") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("gamma delta") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("alpha beta gamma delta") ==
              std::string::npos);
}

TEST(xargs, xargs_long_max_lines_defaults_to_one_without_consuming_command) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"--max-lines", L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("alpha beta\ngamma delta\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("alpha beta") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("gamma delta") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("alpha beta gamma delta") ==
              std::string::npos);
}

TEST(xargs, xargs_short_max_lines_still_requires_an_argument) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-L", L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("alpha beta\ngamma delta\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("-L") != std::string::npos);
}

TEST(xargs, xargs_long_eof_with_equals_stops_at_marker) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"--eof=STOP", L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("one\nSTOP\ntwo\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("one") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("two") == std::string::npos);
}

TEST(xargs, xargs_long_eof_without_value_does_not_consume_command) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"--eof", L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("one\nSTOP\ntwo\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("one") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("STOP") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("two") != std::string::npos);
}

TEST(xargs, xargs_empty_input_runs_command_without_no_run_if_empty) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"cmd.exe", L"/C", L"echo", L"baseline"});
  p.set_stdin("");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("baseline") != std::string::npos);
}

TEST(xargs, xargs_empty_input_with_no_run_if_empty_skips_command) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-r", L"cmd.exe", L"/C", L"echo", L"baseline"});
  p.set_stdin("");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("baseline") == std::string::npos);
}

TEST(xargs, xargs_max_chars_splits_batches) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-t", L"-s", L"21", L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("one\ntwo\nthree\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("one") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("two") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("three") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("cmd.exe") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("cmd.exe") != r.stderr_text.rfind("cmd.exe"));
}

TEST(xargs, xargs_exit_if_exceeded_rejects_oversized_command_line) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-x", L"-s", L"10", L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("alpha\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("command line length exceeded") !=
              std::string::npos);
}

TEST(xargs, xargs_max_chars_below_command_floor_warns_and_runs) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-s", L"1", L"cmd.exe", L"/C", L"echo"});
  p.set_stdin("alpha\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("value for -s option is too small") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("alpha") != std::string::npos);
}

TEST(xargs, xargs_default_echo_max_chars_splits_batches) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-s", L"8"});
  p.set_stdin("one\ntwo\nthree\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "one\ntwo\nthree\n");
}

TEST(xargs, xargs_default_echo_exit_if_exceeded_rejects_oversized_command_line) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-x", L"-s", L"3"});
  p.set_stdin("alpha\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("command line length exceeded") !=
              std::string::npos);
}

TEST(xargs, xargs_maps_child_failure_to_123) {
  Pipeline p;
  p.add(L"xargs.exe", {L"cmd.exe", L"/C", L"exit", L"/B"});
  p.set_stdin("7\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 123);
}

TEST(xargs, xargs_maps_child_255_to_124) {
  Pipeline p;
  p.add(L"xargs.exe", {L"cmd.exe", L"/C", L"exit", L"/B"});
  p.set_stdin("255\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 124);
}

TEST(xargs, xargs_maps_child_126_to_123) {
  Pipeline p;
  p.add(L"xargs.exe", {L"cmd.exe", L"/C", L"exit", L"/B"});
  p.set_stdin("126\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 123);
}

TEST(xargs, xargs_maps_child_127_to_123) {
  Pipeline p;
  p.add(L"xargs.exe", {L"cmd.exe", L"/C", L"exit", L"/B"});
  p.set_stdin("127\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 123);
}

TEST(xargs, xargs_maps_signal_shaped_child_130_to_125) {
  Pipeline p;
  p.add(L"xargs.exe", {L"cmd.exe", L"/C", L"exit", L"/B"});
  p.set_stdin("130\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 125);
}

TEST(xargs, xargs_maps_signal_shaped_child_143_to_125) {
  Pipeline p;
  p.add(L"xargs.exe", {L"cmd.exe", L"/C", L"exit", L"/B"});
  p.set_stdin("143\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 125);
}

TEST(xargs, xargs_not_found_returns_127) {
  Pipeline p;
  p.add(L"xargs.exe", {L"definitely-not-a-winuxcmd-command.exe"});
  p.set_stdin("");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 127);
  EXPECT_TRUE(r.stderr_text.find("No such file or directory") !=
              std::string::npos);
}

TEST(xargs, xargs_non_executable_command_returns_126) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "not-executable");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L".\\not-executable"});
  p.set_stdin("");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 126);
  EXPECT_TRUE(r.stderr_text.find("Permission denied") != std::string::npos);
}

TEST(xargs, xargs_child_255_stops_further_launches) {
  TempDir tmp;
  tmp.write("runner.cmd",
            "@echo off\r\n"
            "if \"%1\"==\"first\" exit /B 255\r\n"
            "echo second>marker.txt\r\n"
            "exit /B 0\r\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-n", L"1", L"cmd.exe", L"/C", L"runner.cmd"});
  p.set_stdin("first\nsecond\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 124);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "marker.txt"));
}

TEST(xargs, xargs_parallel_child_255_waits_for_running_children_before_exit) {
  TempDir tmp;
  tmp.write("runner.cmd",
            "@echo off\r\n"
            "if \"%1\"==\"fail\" (\r\n"
            "  exit /B 255\r\n"
            ")\r\n"
            "ping -n 3 127.0.0.1 >nul\r\n"
            "echo finished>%~dp0running-done.txt\r\n"
            "exit /B 0\r\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe",
        {L"-P", L"2", L"-n", L"1", L"cmd.exe", L"/C", L"runner.cmd"});
  p.set_stdin("slow\nfail\nlater\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 124);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "running-done.txt"));
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "marker.txt"));
}
