#include "framework/winuxtest.h"

TEST(sed, substitute_basic) {
  TempDir tmp;
  tmp.write("a.txt", "foo bar\nfoo baz\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"s/foo/qux/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "qux bar\nqux baz\n");
}

TEST(sed, substitute_global_and_print_flag) {
  TempDir tmp;
  tmp.write("a.txt", "foo foo\nnone\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-n", L"s/foo/bar/gp", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "bar bar\n");
}

TEST(sed, append_insert_change) {
  TempDir tmp;
  tmp.write("a.txt", "line\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe",
        {L"-e", L"i before", L"-e", L"c middle", L"-e", L"a after", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "before\nmiddle\n");
}

TEST(sed, print_command_prints_immediately_and_default_still_prints) {
  TempDir tmp;
  tmp.write("a.txt", "one\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"p", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "one\none\n");
}

TEST(sed, print_command_preserves_command_order) {
  TempDir tmp;
  tmp.write("a.txt", "one\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-n", L"p;s/one/two/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "one\n");
}

TEST(sed, print_command_outputs_empty_pattern_space) {
  TempDir tmp;
  tmp.write("a.txt", "\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-n", L"p", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "\n");
}

TEST(sed, print_command_terminates_unchomped_input_line) {
  TempDir tmp;
  tmp.write("a.txt", "one");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-n", L"p", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "one\n");
}

TEST(sed, substitution_print_flag_prints_in_addition_to_default_output) {
  TempDir tmp;
  tmp.write("a.txt", "foo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"s/foo/bar/p", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "bar\nbar\n");
}

TEST(sed, numeric_same_line_range_does_not_leak_past_end) {
  TempDir tmp;
  tmp.write("a.txt", "one\ntwo\nthree\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-n", L"2,2p", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "two\n");
}

TEST(sed, change_range_outputs_replacement_once_at_range_end) {
  TempDir tmp;
  tmp.write("a.txt", "one\ntwo\nthree\nfour\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"2,3c\\MID", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "one\nMID\nfour\n");
}

TEST(sed, text_commands_consume_backslash_syntax) {
  TempDir tmp;
  tmp.write("a.txt", "one\ntwo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe",
        {L"-e", L"1i\\TOP", L"-e", L"1a\\MID", L"-e", L"2c\\LAST", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "TOP\none\nMID\nLAST\n");
}

TEST(sed, text_command_reads_next_expression_line_after_backslash) {
  TempDir tmp;
  tmp.write("a.txt", "one\ntwo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-e", L"1a\\\nAPP", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "one\nAPP\ntwo\n");
}

TEST(sed, text_command_reads_next_script_file_line_after_backslash) {
  TempDir tmp;
  tmp.write("a.txt", "one\ntwo\n");
  tmp.write("script.sed", "1a\\\nAPP\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-f", L"script.sed", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "one\nAPP\ntwo\n");
}

TEST(sed, script_operand_is_literal_not_glob) {
  TempDir tmp;
  tmp.write("hallo", "not an input\n");
  tmp.write("a.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"s/h[ae]llo/bye/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "bye\n");
}

TEST(sed, script_file_and_quiet) {
  TempDir tmp;
  tmp.write("a.txt", "foo\n");
  tmp.write("script.sed", "s/foo/bar/\np\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-n", L"-f", L"script.sed", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "bar\n");
}

TEST(sed, multiple_expressions_apply_in_order) {
  TempDir tmp;
  tmp.write("a.txt", "foo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-e", L"s/foo/bar/", L"-e", L"s/bar/baz/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "baz\n");
}

TEST(sed, multiple_long_expressions_apply_in_order) {
  TempDir tmp;
  tmp.write("a.txt", "foo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe",
        {L"--expression=s/foo/bar/", L"--expression", L"s/bar/baz/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "baz\n");
}

TEST(sed, multiple_script_files_apply_in_order) {
  TempDir tmp;
  tmp.write("a.txt", "foo\n");
  tmp.write("first.sed", "s/foo/bar/\n");
  tmp.write("second.sed", "s/bar/baz/\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-f", L"first.sed", L"-f", L"second.sed", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "baz\n");
}

TEST(sed, expression_and_file_order_is_preserved) {
  TempDir tmp;
  tmp.write("a.txt", "foo\n");
  tmp.write("file.sed", "s/foo/bar/\n");

  Pipeline file_then_expr;
  file_then_expr.set_cwd(tmp.wpath());
  file_then_expr.add(L"sed.exe",
                     {L"-f", L"file.sed", L"-e", L"s/bar/baz/", L"a.txt"});
  auto file_then_expr_result = file_then_expr.run();

  EXPECT_EQ(file_then_expr_result.exit_code, 0);
  EXPECT_EQ_TEXT(file_then_expr_result.stdout_text, "baz\n");

  Pipeline expr_then_file;
  expr_then_file.set_cwd(tmp.wpath());
  expr_then_file.add(L"sed.exe",
                     {L"-e", L"s/bar/baz/", L"-f", L"file.sed", L"a.txt"});
  auto expr_then_file_result = expr_then_file.run();

  EXPECT_EQ(expr_then_file_result.exit_code, 0);
  EXPECT_EQ_TEXT(expr_then_file_result.stdout_text, "bar\n");
}

TEST(sed, in_place_edit) {
  TempDir tmp;
  tmp.write("a.txt", "foo\nbar\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-i", L"s/foo/baz/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "");

  auto content = tmp.read("a.txt");
  EXPECT_EQ_TEXT(content, "baz\nbar\n");
}

TEST(sed, in_place_edit_with_short_backup_suffix) {
  TempDir tmp;
  tmp.write("a.txt", "foo\nbar\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-i.bak", L"s/foo/baz/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "");
  EXPECT_EQ_TEXT(tmp.read("a.txt"), "baz\nbar\n");
  EXPECT_EQ_TEXT(tmp.read("a.txt.bak"), "foo\nbar\n");
}

TEST(sed, in_place_edit_with_long_backup_suffix) {
  TempDir tmp;
  tmp.write("a.txt", "foo\nbar\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"--in-place=.orig", L"s/foo/baz/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "");
  EXPECT_EQ_TEXT(tmp.read("a.txt"), "baz\nbar\n");
  EXPECT_EQ_TEXT(tmp.read("a.txt.orig"), "foo\nbar\n");
}

TEST(sed, in_place_backup_suffix_star_expands_to_filename) {
  TempDir tmp;
  tmp.write("a.txt", "foo\nbar\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-i*.bak", L"s/foo/baz/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "");
  EXPECT_EQ_TEXT(tmp.read("a.txt"), "baz\nbar\n");
  EXPECT_EQ_TEXT(tmp.read("a.txt.bak"), "foo\nbar\n");
}

TEST(sed, in_place_short_option_does_not_consume_next_argument_as_suffix) {
  TempDir tmp;
  tmp.write("a.txt", "foo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-i", L".bak", L"s/foo/baz/", L"a.txt"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("a.txt"), "foo\n");
}

TEST(sed, extended_regex_option) {
  TempDir tmp;
  tmp.write("a.txt", "a1\nb2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-E", L"s/([a-z])([[:digit:]])/X\\2/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "X1\nX2\n");
}

TEST(sed, extended_regex_backslash_d_is_literal_d) {
  TempDir tmp;
  tmp.write("a.txt", "a1\nad\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-E", L"s/([a-z])(\\d)/X\\2/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a1\nXd\n");
}

TEST(sed, substitution_replacement_ampersand_expands_whole_match) {
  TempDir tmp;
  tmp.write("a.txt", "id=123\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"s/[0-9][0-9]*/<&>/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "id=<123>\n");
}

TEST(sed, substitution_basic_backrefs_expand_in_replacement) {
  TempDir tmp;
  tmp.write("a.txt", "abc-42\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe",
        {L"s/\\([a-z][a-z]*\\)-\\([0-9][0-9]*\\)/\\2:\\1/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "42:abc\n");
}

TEST(sed, substitution_replacement_backslash_n_expands_to_newline) {
  TempDir tmp;
  tmp.write("a.txt", "foo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"s/foo/first\\nsecond/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "first\nsecond\n");
}

TEST(sed, extended_substitution_preserves_greedy_capture_before_tail) {
  TempDir tmp;
  tmp.write("a.txt", "000 alpha needle_123 x\n");
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe",
        {L"-n", L"-E", L"s/.*(needle)_([0-9]+).*/\\2-\\1/p", L"a.txt"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "123-needle\n");
}

TEST(sed, substitution_pattern_backreference_matches_prior_group) {
  TempDir tmp;
  tmp.write("a.txt", "book\nabcd\nnoon\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"s/\\([a-z]\\)\\1/X/g", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "bXk\nabcd\nnXn\n");
}

TEST(sed, substitution_escaped_ampersand_and_backslash_are_literal) {
  TempDir tmp;
  tmp.write("a.txt", "foo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"s/foo/\\&\\\\/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "&\\\n");
}

TEST(sed, substitution_numeric_occurrence_replaces_only_nth_match) {
  TempDir tmp;
  tmp.write("a.txt", "foo foo foo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"s/foo/bar/2", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "foo bar foo\n");
}

TEST(sed, substitution_numeric_occurrence_with_global_replaces_from_nth) {
  TempDir tmp;
  tmp.write("a.txt", "foo foo foo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"s/foo/bar/2g", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "foo bar bar\n");
}

TEST(sed, literal_substitution_preserves_no_final_newline) {
  TempDir tmp;
  tmp.write("a.txt", "foo foo");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"s/foo/bar/g", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "bar bar");
}

TEST(sed, basic_regex_plus_remains_literal) {
  TempDir tmp;
  tmp.write("a.txt", "a+\naaa\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"s/a+/X/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "X\naaa\n");
}

TEST(sed, substitution_ignore_case_flag) {
  TempDir tmp;
  tmp.write("a.txt", "FOO foo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"s/foo/bar/gI", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "bar bar\n");
}

TEST(sed, line_range_substitution) {
  TempDir tmp;
  tmp.write("a.txt", "one\ntwo\nthree\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"1,2s/o/O/g", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "One\ntwO\nthree\n");
}

TEST(sed, regex_range_delete) {
  TempDir tmp;
  tmp.write("a.txt", "aaa\nbbb\nccc\nddd\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"/bbb/,/ccc/d", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "aaa\nddd\n");
}

TEST(sed, zero_line_regex_range_starts_before_first_record) {
  TempDir tmp;
  tmp.write("a.txt", "foo\nbar\nfoo\n");
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"0,/foo/s/foo/XX/", L"a.txt"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "XX\nbar\nfoo\n");
}
TEST(sed, zero_line_regex_range_extends_until_first_match) {
  TempDir tmp;
  tmp.write("a.txt", "bar\nfoo\nfoo\n");
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"0,/foo/s/foo/XX/", L"a.txt"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "bar\nXX\nfoo\n");
}
TEST(sed, address_negation_applies_command_to_non_matching_lines) {
  TempDir tmp;
  tmp.write("a.txt", "foo\nfoo\nfoo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"2!s/foo/bar/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "bar\nfoo\nbar\n");
}

TEST(sed, address_range_negation_applies_command_outside_range) {
  TempDir tmp;
  tmp.write("a.txt", "one\ntwo\nthree\nfour\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"2,3!d", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "two\nthree\n");
}

TEST(sed, step_address_prints_matching_lines) {
  TempDir tmp;
  tmp.write("a.txt", "one\ntwo\nthree\nfour\nfive\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-n", L"1~2p", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "one\nthree\nfive\n");
}

TEST(sed, zero_step_address_starts_at_step_interval) {
  TempDir tmp;
  tmp.write("a.txt", "one\ntwo\nthree\nfour\nfive\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-n", L"0~2p", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "two\nfour\n");
}

TEST(sed, regex_address_ignore_case_modifier) {
  TempDir tmp;
  tmp.write("a.txt", "FOO\nbar\nfoo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-n", L"/foo/Ip", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "FOO\nfoo\n");
}

TEST(sed, regex_address_allows_alternate_delimiter) {
  TempDir tmp;
  tmp.write("a.txt", "foo\nbar\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-n", L"\\%foo%p", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "foo\n");
}

TEST(sed, regex_address_alternate_delimiter_keeps_semicolon_in_pattern) {
  TempDir tmp;
  tmp.write("a.txt", "a;b\nab\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-n", L"\\%a;b%p", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a;b\n");
}

TEST(sed, relative_range_address_prints_n_following_lines) {
  TempDir tmp;
  tmp.write("a.txt", "one\ntwo\nthree\nfour\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-n", L"2,+1p", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "two\nthree\n");
}

TEST(sed, modulo_range_address_ends_at_next_multiple) {
  TempDir tmp;
  tmp.write("a.txt", "one\ntwo\nthree\nfour\nfive\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-n", L"2,~2p", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "two\nthree\nfour\n");
}

TEST(sed, y_command_translate) {
  TempDir tmp;
  tmp.write("a.txt", "abc\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"y/abc/xyz/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "xyz\n");
}

TEST(sed, last_line_address) {
  TempDir tmp;
  tmp.write("a.txt", "one\ntwo\nthree\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"$s/o/O/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "one\ntwo\nthree\n");
}

TEST(sed, semicolon_multiple_commands) {
  TempDir tmp;
  tmp.write("a.txt", "ab\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"s/a/A/;s/b/B/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "AB\n");
}

TEST(sed, comments_are_ignored_after_command_separator) {
  TempDir tmp;
  tmp.write("a.txt", "foo\nbar\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"s/o/O/;# comment", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "fOo\nbar\n");
}

TEST(sed, magic_n_comment_suppresses_default_output) {
  TempDir tmp;
  tmp.write("a.txt", "one\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"#n\np", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "one\n");
}

TEST(sed, addressed_substitution_allows_semicolon_in_replacement) {
  TempDir tmp;
  tmp.write("a.txt", "a\nb\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"1s/a/;/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, ";\nb\n");
}

TEST(sed, empty_substitution_regex_reuses_previous_address_regex) {
  TempDir tmp;
  tmp.write("a.txt", "foo\nbar\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-n", L"/foo/s//baz/p", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "baz\n");
}

TEST(sed, empty_substitution_regex_reuses_case_insensitive_address_regex) {
  TempDir tmp;
  tmp.write("a.txt", "FOO\nfoo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-n", L"/foo/Is//X/p", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "X\nX\n");
}

TEST(sed, empty_substitution_regex_requires_previous_regex) {
  TempDir tmp;
  tmp.write("a.txt", "foo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-n", L"s//X/p", L"a.txt"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
}

TEST(sed, empty_substitution_regex_rejects_new_modifiers) {
  TempDir tmp;
  tmp.write("a.txt", "FOO\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-n", L"-e", L"s/foo/bar/", L"-e", L"s//X/Ip", L"a.txt"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
}

TEST(sed, quit_command) {
  TempDir tmp;
  tmp.write("a.txt", "one\ntwo\nthree\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"2q", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "one\ntwo\n");
}

TEST(sed, quit_command_uses_exit_code) {
  TempDir tmp;
  tmp.write("a.txt", "one\ntwo\nthree\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"2q42", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 42);
  EXPECT_EQ_TEXT(r.stdout_text, "one\ntwo\n");
}

TEST(sed, quit_command_wraps_large_exit_code) {
  TempDir tmp;
  tmp.write("a.txt", "one\ntwo\nthree\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"2q300", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 44);
  EXPECT_EQ_TEXT(r.stdout_text, "one\ntwo\n");
}

TEST(sed, silent_quit_does_not_default_print_or_flush_append_queue) {
  TempDir tmp;
  tmp.write("a.txt", "one\ntwo\nthree\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-e", L"2aAPP", L"-e", L"2Q7", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 7);
  EXPECT_EQ_TEXT(r.stdout_text, "one\n");
}

TEST(sed, line_number_command_prints_current_input_line_number) {
  TempDir tmp;
  tmp.write("a.txt", "one\ntwo\nthree\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-n", L"2=", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "2\n");
}

TEST(sed, filename_command_prints_current_input_file_name) {
  TempDir tmp;
  tmp.write("a.txt", "one\n");
  tmp.write("b.txt", "two\n");
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-n", L"F", L"a.txt", L"b.txt"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a.txt\nb.txt\n");
}
TEST(sed, filename_command_prints_dash_for_stdin) {
  Pipeline p;
  p.set_stdin("one\n");
  p.add(L"sed.exe", {L"-n", L"F"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "-\n");
}
TEST(sed, print_command_rejects_extra_characters) {
  TempDir tmp;
  tmp.write("a.txt", "one\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"p extra", L"a.txt"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
}

TEST(sed, list_command_escapes_nonprinting_characters) {
  TempDir tmp;
  tmp.write("a.txt", "a\tb\r\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-n", L"l", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\\tb\\r$\n");
}

TEST(sed, line_length_option_wraps_list_command_output) {
  TempDir tmp;
  tmp.write("a.txt", "abcd\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-n", L"-l", L"3", L"l", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "ab\\\ncd$\n");
}

TEST(sed, list_command_accepts_inline_line_length) {
  TempDir tmp;
  tmp.write("a.txt", "abcdef\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-n", L"l 3", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "ab\\\ncd\\\nef$\n");
}

TEST(sed, multiline_commands_append_print_and_delete_first_segment) {
  TempDir tmp;
  tmp.write("a.txt", "a\nb\nc\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-n", L"N;=;P;D", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "2\na\n3\nb\n");
}

TEST(sed, next_command_reads_next_input_record) {
  TempDir tmp;
  tmp.write("a.txt", "a\nb\nc\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"n;s/b/B/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\nB\nc\n");
}

TEST(sed, hold_space_commands_copy_append_get_and_exchange) {
  TempDir tmp;
  tmp.write("a.txt", "one\ntwo\nthree\n");

  Pipeline aggregate;
  aggregate.set_cwd(tmp.wpath());
  aggregate.add(L"sed.exe", {L"-n", L"1h;2H;$g;$p", L"a.txt"});
  auto aggregate_result = aggregate.run();

  EXPECT_EQ(aggregate_result.exit_code, 0);
  EXPECT_EQ_TEXT(aggregate_result.stdout_text, "one\ntwo\n");

  Pipeline exchange;
  exchange.set_cwd(tmp.wpath());
  exchange.add(L"sed.exe", {L"-n", L"1h;2x;2p", L"a.txt"});
  auto exchange_result = exchange.run();

  EXPECT_EQ(exchange_result.exit_code, 0);
  EXPECT_EQ_TEXT(exchange_result.stdout_text, "one\n");
}

TEST(sed, branch_commands_resolve_labels_and_track_substitutions) {
  TempDir tmp;
  tmp.write("a.txt", "foo\nbar\n");

  Pipeline branch_on_substitution;
  branch_on_substitution.set_cwd(tmp.wpath());
  branch_on_substitution.add(L"sed.exe",
                             {L"s/foo/bar/;t done;s/bar/baz/;:done", L"a.txt"});
  auto branch_result = branch_on_substitution.run();

  EXPECT_EQ(branch_result.exit_code, 0);
  EXPECT_EQ_TEXT(branch_result.stdout_text, "bar\nbaz\n");

  Pipeline branch_on_no_substitution;
  branch_on_no_substitution.set_cwd(tmp.wpath());
  branch_on_no_substitution.add(
      L"sed.exe", {L"-n", L"s/foo/foo/;T miss;p;:miss", L"a.txt"});
  auto no_substitution_result = branch_on_no_substitution.run();

  EXPECT_EQ(no_substitution_result.exit_code, 0);
  EXPECT_EQ_TEXT(no_substitution_result.stdout_text, "foo\n");
}

TEST(sed, read_file_command_appends_file_contents_after_cycle) {
  TempDir tmp;
  tmp.write("a.txt", "a\nb\n");
  tmp.write("extra.txt", "X\nY");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"1r extra.txt", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\nX\nYb\n");
}

TEST(sed, write_file_commands_write_pattern_and_first_segment) {
  TempDir tmp;
  tmp.write("a.txt", "a\nb\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-n", L"-e", L"N", L"-e", L"w all.txt", L"-e",
                     L"W first.txt", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "");
  EXPECT_EQ_TEXT(tmp.read("all.txt"), "a\nb\n");
  EXPECT_EQ_TEXT(tmp.read("first.txt"), "a\n");
}

TEST(sed, substitution_write_flag_writes_replaced_pattern_space) {
  TempDir tmp;
  tmp.write("a.txt", "foo\nbar\nfoo\n");
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"s/foo/baz/w hits.txt", L"a.txt"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "baz\nbar\nbaz\n");
  EXPECT_EQ_TEXT(tmp.read("hits.txt"), "baz\nbaz\n");
}

TEST(sed, read_file_line_command_consumes_external_file_across_cycles) {
  TempDir tmp;
  tmp.write("a.txt", "a\nb\nc\n");
  tmp.write("extra.txt", "X\nY");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"R extra.txt", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\nX\nb\nYc\n");
}

TEST(sed, clear_pattern_command_preserves_record_termination) {
  TempDir tmp;
  tmp.write("a.txt", "abc\ndef\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"z", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "\n\n");
}

TEST(sed, separate_files_resets_addresses) {
  TempDir tmp;
  tmp.write("a.txt", "one\n");
  tmp.write("b.txt", "two\n");

  Pipeline combined;
  combined.set_cwd(tmp.wpath());
  combined.add(L"sed.exe", {L"-n", L"$p", L"a.txt", L"b.txt"});
  auto combined_result = combined.run();

  EXPECT_EQ(combined_result.exit_code, 0);
  EXPECT_EQ_TEXT(combined_result.stdout_text, "two\n");

  Pipeline separate;
  separate.set_cwd(tmp.wpath());
  separate.add(L"sed.exe", {L"-n", L"-s", L"$p", L"a.txt", L"b.txt"});
  auto separate_result = separate.run();

  EXPECT_EQ(separate_result.exit_code, 0);
  EXPECT_EQ_TEXT(separate_result.stdout_text, "one\ntwo\n");
}

TEST(sed, null_data_uses_nul_delimiter) {
  TempDir tmp;
  tmp.write_bytes("a.bin", {'f', 'o', 'o', '\0', 'b', 'a', 'r', '\0'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-z", L"s/foo/baz/", L"a.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text.size(), 8u);
  EXPECT_EQ(r.stdout_text, std::string("baz\0bar\0", 8));
}

TEST(sed, zero_terminated_long_option_uses_nul_delimiter) {
  TempDir tmp;
  tmp.write_bytes("a.bin", {'f', 'o', 'o', '\0', 'b', 'a', 'r', '\0'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"--zero-terminated", L"s/bar/qux/", L"a.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text.size(), 8u);
  EXPECT_EQ(r.stdout_text, std::string("foo\0qux\0", 8));
}

TEST(sed, zero_terminated_text_commands_use_nul_delimiter) {
  TempDir tmp;
  tmp.write_bytes("a.bin", {'f', 'o', 'o', '\0'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-z", L"-e", L"1i\\PRE", L"-e", L"1a\\POST", L"a.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text.size(), 13u);
  EXPECT_EQ(r.stdout_text, std::string("PRE\0foo\0POST\0", 13));
}

TEST(sed, unbuffered_and_binary_options_are_accepted) {
  TempDir tmp;
  tmp.write("a.txt", "foo\r\nbar\r\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-u", L"--binary", L"s/foo/baz/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "baz\r\nbar\r\n");
}

TEST(sed, wildcard_files) {
  TempDir tmp;
  tmp.write("file1.txt", "hello world\n");
  tmp.write("file2.txt", "hello there\n");
  tmp.write("other.log", "hello log\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"s/hello/bye/", L"*.txt"});

  TEST_LOG_CMD_LIST("sed.exe", L"s/hello/bye/", L"*.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("sed output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("bye world") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("bye there") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("bye log") == std::string::npos);
}

TEST(sed, wildcard_question_mark) {
  TempDir tmp;
  tmp.write("a1.txt", "foo bar\n");
  tmp.write("a2.txt", "foo baz\n");
  tmp.write("a10.txt", "foo qux\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"s/foo/REPLACED/", L"a?.txt"});

  TEST_LOG_CMD_LIST("sed.exe", L"s/foo/REPLACED/", L"a?.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("sed output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("REPLACED bar") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("REPLACED baz") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("REPLACED qux") == std::string::npos);
}

TEST(sed, sandbox_allows_non_file_scripts) {
  TempDir tmp;
  tmp.write("a.txt", "foo\nbar\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"--sandbox", L"s/foo/baz/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "baz\nbar\n");
}

TEST(sed, sandbox_rejects_file_commands) {
  TempDir tmp;
  tmp.write("a.txt", "foo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"--sandbox", L"w out.txt", L"a.txt"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_CONTAINS(r.stderr_text, "--sandbox rejects scripts");
}

TEST(sed, sandbox_rejects_substitution_write_flag) {
  TempDir tmp;
  tmp.write("a.txt", "foo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"--sandbox", L"s/foo/bar/w out.txt", L"a.txt"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_CONTAINS(r.stderr_text, "--sandbox rejects scripts");
}

TEST(sed, posix_rejects_gnu_step_addresses) {
  TempDir tmp;
  tmp.write("a.txt", "one\ntwo\nthree\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"--posix", L"1~2p", L"a.txt"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_CONTAINS(r.stderr_text, "POSIX sed rejects GNU step addresses");
}

TEST(sed, posix_rejects_gnu_range_extensions) {
  TempDir tmp;
  tmp.write("a.txt", "one\ntwo\nthree\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"--posix", L"1,+1p", L"a.txt"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_CONTAINS(r.stderr_text, "POSIX sed rejects GNU range extensions");
}

TEST(sed, posix_rejects_gnu_substitution_modifier) {
  TempDir tmp;
  tmp.write("a.txt", "FOO\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"--posix", L"s/foo/bar/I", L"a.txt"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_CONTAINS(r.stderr_text,
                  "POSIX sed rejects GNU substitution modifiers");
}

TEST(sed, brace_group_allows_semicolon_after_close) {
  // [GNU] A ';' may follow '}' and start another command on the same line:
  // sed '1{p};2{d}' (WinuxCmd#992).
  TempDir tmp;
  tmp.write("a.txt", "a\nb\nc\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"1{p};2{d}", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\na\nc\n");
}

TEST(sed, brace_group_spans_expression_options) {
  // [GNU] All -e/-f sources are concatenated with newlines into one script
  // before compiling, so a group may open in one -e and close in another
  // (WinuxCmd#992).
  TempDir tmp;
  tmp.write("a.txt", "a\nb\nc\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-n", L"-e", L"1{", L"-e", L"p", L"-e", L"}", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\n");
}

TEST(sed, empty_expression_script_treats_positional_as_input) {
  // [GNU] sed -e '' FILE reads FILE as input, not as the script.
  TempDir tmp;
  tmp.write("a.txt", "x\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-e", L"", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "x\n");
}

// ======================================================
// [GNU] compatibility regression tests
// ======================================================

TEST(sed, gnu_compat_input_open_failure_exits_2) {
  // [GNU] failed input open is EXIT_BAD_INPUT (2), not 1
  // (utils.h:22-25, execute.c:1710-1712).
  TempDir tmp;
  tmp.write("a.txt", "foo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"s/foo/bar/", L"missing.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_CONTAINS(r.stderr_text, "can't read missing.txt");
}

TEST(sed, gnu_compat_write_file_failure_exits_2) {
  // [GNU] w/W/s///w target that cannot be opened is a bad-input failure (2).
  TempDir tmp;
  tmp.write("a.txt", "foo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"w no_such_dir/out.txt", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 2);
}

TEST(sed, gnu_compat_subst_write_flag_failure_exits_2) {
  TempDir tmp;
  tmp.write("a.txt", "foo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"s/foo/bar/w no_such_dir/hits.txt", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 2);
}

TEST(sed, gnu_compat_usage_error_still_exits_1) {
  TempDir tmp;
  tmp.write("a.txt", "foo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"p extra", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
}

TEST(sed, gnu_compat_replacement_uppercase_until_E) {
  // [GNU] \U upper-cases until \E or end of replacement
  // (sed.h:67-77, compile.c:783-800).
  TempDir tmp;
  tmp.write("a.txt", "hello world\n");

  Pipeline upper;
  upper.set_cwd(tmp.wpath());
  upper.add(L"sed.exe", {L"s/hello world/\\U&/", L"a.txt"});
  auto upper_result = upper.run();

  EXPECT_EQ(upper_result.exit_code, 0);
  EXPECT_EQ_TEXT(upper_result.stdout_text, "HELLO WORLD\n");

  Pipeline stop_at_e;
  stop_at_e.set_cwd(tmp.wpath());
  stop_at_e.add(L"sed.exe", {L"s/world/\\Uwo\\Erld/", L"a.txt"});
  auto stop_result = stop_at_e.run();

  EXPECT_EQ(stop_result.exit_code, 0);
  EXPECT_EQ_TEXT(stop_result.stdout_text, "hello WOrld\n");
}

TEST(sed, gnu_compat_replacement_lowercase_and_next_char) {
  // [GNU] \L lower-cases the rest, \u upper-cases only the next character.
  TempDir tmp;
  tmp.write("a.txt", "ABC DEF\n");

  Pipeline lower;
  lower.set_cwd(tmp.wpath());
  lower.add(L"sed.exe", {L"s/ABC DEF/\\L&/", L"a.txt"});
  auto lower_result = lower.run();

  EXPECT_EQ(lower_result.exit_code, 0);
  EXPECT_EQ_TEXT(lower_result.stdout_text, "abc def\n");

  Pipeline next_char;
  next_char.set_cwd(tmp.wpath());
  next_char.add(L"sed.exe", {L"s/ABC/\\uabc/", L"a.txt"});
  auto next_result = next_char.run();

  EXPECT_EQ(next_result.exit_code, 0);
  EXPECT_EQ_TEXT(next_result.stdout_text, "Abc DEF\n");
}

TEST(sed, gnu_compat_case_conversion_applies_to_backrefs) {
  TempDir tmp;
  tmp.write("a.txt", "abc-DEF\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe",
        {L"-E", L"s/([a-z]+)-([A-Z]+)/\\U\\1 \\E\\L\\2/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "ABC def\n");
}

TEST(sed, gnu_compat_unknown_replacement_escape_keeps_backslash) {
  // [GNU] unknown escapes keep the backslash (compile.c:560-565).
  TempDir tmp;
  tmp.write("a.txt", "foo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"s/foo/\\q/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "\\q\n");
}

TEST(sed, gnu_compat_y_command_processes_escapes) {
  // [GNU] y/// lists process \n, \t, \\ and escaped delimiters before
  // the translate map is built (compile.c:1415-1446).
  TempDir tmp;
  tmp.write("a.txt", "abc\n");
  tmp.write("slash.txt", "a\\b\n");

  Pipeline tab;
  tab.set_cwd(tmp.wpath());
  tab.add(L"sed.exe", {L"y/b/\\t/", L"a.txt"});
  auto tab_result = tab.run();

  EXPECT_EQ(tab_result.exit_code, 0);
  EXPECT_EQ_TEXT(tab_result.stdout_text, "a\tc\n");

  Pipeline backslash;
  backslash.set_cwd(tmp.wpath());
  backslash.add(L"sed.exe", {L"y/\\\\/q/", L"slash.txt"});
  auto backslash_result = backslash.run();

  EXPECT_EQ(backslash_result.exit_code, 0);
  EXPECT_EQ_TEXT(backslash_result.stdout_text, "aqb\n");
}

TEST(sed, gnu_compat_read_command_reports_unreadable_file) {
  // [GNU] r on an unreadable file prints "sed: can't read FILE: reason" and
  // continues (execute.c:1510-1523, execute.c:565).
  TempDir tmp;
  tmp.write("a.txt", "foo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"r no_such_file.txt", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stderr_text, "can't read no_such_file.txt");
  EXPECT_EQ_TEXT(r.stdout_text, "foo\n");
}

TEST(sed, gnu_compat_read_line_command_reports_unreadable_file) {
  TempDir tmp;
  tmp.write("a.txt", "foo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"R no_such_file.txt", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stderr_text, "can't read no_such_file.txt");
  EXPECT_EQ_TEXT(r.stdout_text, "foo\n");
}

TEST(sed, gnu_compat_in_place_without_input_files_exits_4) {
  // [GNU] sed -i with stdin: PANIC "no input files", exit 4
  // (execute.c:1672, utils.h:26).
  Pipeline p;
  p.set_stdin("foo\n");
  p.add(L"sed.exe", {L"-i", L"s/foo/bar/"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 4);
  EXPECT_CONTAINS(r.stderr_text, "no input files");
}

TEST(sed, gnu_compat_subst_multiline_flag_matches_at_newlines) {
  // [GNU] s///m and s///M: $ also matches before an embedded newline.
  TempDir tmp;
  tmp.write("a.txt", "a\nb\nc\n");

  Pipeline dollar;
  dollar.set_cwd(tmp.wpath());
  dollar.add(L"sed.exe", {L"N;s/b$/X/m", L"a.txt"});
  auto dollar_result = dollar.run();

  EXPECT_EQ(dollar_result.exit_code, 0);
  EXPECT_EQ_TEXT(dollar_result.stdout_text, "a\nX\nc\n");

  Pipeline big_m;
  big_m.set_cwd(tmp.wpath());
  big_m.add(L"sed.exe", {L"N;s/^b$/X/M", L"a.txt"});
  auto big_m_result = big_m.run();

  EXPECT_EQ(big_m_result.exit_code, 0);
  EXPECT_EQ_TEXT(big_m_result.stdout_text, "a\nX\nc\n");
}

TEST(sed, gnu_compat_subst_multiline_flag_absent_keeps_string_anchors) {
  TempDir tmp;
  tmp.write("a.txt", "a\nb\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"N;s/^b$/X/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\nb\n");
}

TEST(sed, gnu_compat_subst_e_flag_is_rejected_with_clear_message) {
  TempDir tmp;
  tmp.write("a.txt", "foo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"s/foo/bar/e", L"a.txt"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_CONTAINS(r.stderr_text, "'e' flag");
}

TEST(sed, gnu_compat_debug_and_follow_symlinks_are_accepted) {
  TempDir tmp;
  tmp.write("a.txt", "foo\n");

  Pipeline debug;
  debug.set_cwd(tmp.wpath());
  debug.add(L"sed.exe", {L"--debug", L"s/foo/bar/", L"a.txt"});
  auto debug_result = debug.run();

  EXPECT_EQ(debug_result.exit_code, 0);
  EXPECT_EQ_TEXT(debug_result.stdout_text, "bar\n");

  Pipeline follow;
  follow.set_cwd(tmp.wpath());
  follow.add(L"sed.exe",
             {L"--follow-symlinks", L"-i", L"s/foo/bar/", L"a.txt"});
  auto follow_result = follow.run();

  EXPECT_EQ(follow_result.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("a.txt"), "bar\n");
}
