// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WinuxCmd
#include "framework/winuxtest.h"

// Expectations byte-matched against GNU diffutils 3.10 (WSL oracle,
// issue #1127 P1).  The display is exactly `diff --side-by-side` output.

TEST(sdiff, sdiff_basic_matches_gnu_width_80_shape) {
  TempDir tmp;
  tmp.write("file1.txt", "same\nleft\n");
  tmp.write("file2.txt", "same\nright\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"-w", L"80", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "same\t\t\t\t\tsame\n"
                 "left\t\t\t\t      |\tright\n");
}

TEST(sdiff, sdiff_suppress_common_matches_gnu_width_80_shape) {
  TempDir tmp;
  tmp.write("a.txt", "same\nleft\n");
  tmp.write("b.txt", "same\nright\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"-s", L"-w", L"80", L"a.txt", L"b.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "left\t\t\t\t      |\tright\n");
}

TEST(sdiff, sdiff_left_column_marks_suppressed_right) {
  TempDir tmp;
  tmp.write("a.txt", "same\nleft\n");
  tmp.write("b.txt", "same\nright\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"-l", L"-w", L"80", L"a.txt", L"b.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  // [GNU] Common lines print once with '(' in the gutter; changed rows
  // still pair with '|'.
  EXPECT_EQ_TEXT(r.stdout_text,
                 "same\t\t\t\t      (\n"
                 "left\t\t\t\t      |\tright\n");
}

TEST(sdiff, sdiff_identical_files_print_common_rows_and_exit_zero) {
  TempDir tmp;
  tmp.write("a.txt", "same\n");
  tmp.write("b.txt", "same\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"-w", L"80", L"a.txt", L"b.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "same\t\t\t\t\tsame\n");

  Pipeline suppressed;
  suppressed.set_cwd(tmp.wpath());
  suppressed.add(L"sdiff.exe", {L"-s", L"-w", L"80", L"a.txt", L"b.txt"});
  auto suppressed_run = suppressed.run();
  EXPECT_EQ(suppressed_run.exit_code, 0);
  EXPECT_TRUE(suppressed_run.stdout_text.empty());
}

TEST(sdiff, sdiff_empty_files) {
  TempDir tmp;
  tmp.write("a.txt", "");
  tmp.write("b.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"a.txt", L"b.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(sdiff, sdiff_insert_only_rows_use_right_arrow) {
  TempDir tmp;
  tmp.write("a.txt", "a\n");
  tmp.write("b.txt", "a\nb\nc\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"-s", L"-w", L"20", L"a.txt", L"b.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "      >\tb\n      >\tc\n");
}

TEST(sdiff, sdiff_ignore_tab_expansion_matches_gnu_comparison) {
  TempDir tmp;
  tmp.write("a.txt", "hello\tworld\n");
  tmp.write("b.txt", "hello   world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"-E", L"a.txt", L"b.txt"});
  auto r = p.run();

  // [GNU -E] Tabs compare as spaces up to the next tab stop, so the lines
  // match and the files are equal.
  EXPECT_EQ(r.exit_code, 0);
}

TEST(sdiff, sdiff_width_difference_returns_one) {
  TempDir tmp;
  tmp.write("a.txt", "hello\n");
  tmp.write("b.txt", "world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"-w", L"120", L"a.txt", L"b.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(sdiff, sdiff_ignore_family_zero_exit_when_lines_match) {
  TempDir tmp;
  tmp.write("a.txt", "Alpha\nend \n");
  tmp.write("b.txt", "ALPHA\nend\n");

  // -i: case differences only.
  Pipeline case_insensitive;
  case_insensitive.set_cwd(tmp.wpath());
  case_insensitive.add(L"sdiff.exe", {L"-i", L"a.txt", L"b.txt"});
  auto case_run = case_insensitive.run();
  EXPECT_EQ(case_run.exit_code, 1);

  // -Z ignores the trailing space, -b its run: "end " == "end".
  Pipeline trailing;
  trailing.set_cwd(tmp.wpath());
  trailing.add(L"sdiff.exe", {L"-Z", L"a.txt", L"b.txt"});
  auto trailing_run = trailing.run();
  EXPECT_EQ(trailing_run.exit_code, 1);

  // -W ignores everything whitespace, so both rows are common and the
  // only difference left is the case on line 1 - still a difference.
  Pipeline all_space;
  all_space.set_cwd(tmp.wpath());
  all_space.add(L"sdiff.exe", {L"-W", L"a.txt", L"b.txt"});
  auto all_space_run = all_space.run();
  EXPECT_EQ(all_space_run.exit_code, 1);

  // Same lines entirely: every ignore mode agrees on zero.
  tmp.write("b.txt", "ALPHA\nend \n");
  Pipeline case_only;
  case_only.set_cwd(tmp.wpath());
  case_only.add(L"sdiff.exe", {L"-i", L"-s", L"a.txt", L"b.txt"});
  auto case_only_run = case_only.run();
  EXPECT_EQ(case_only_run.exit_code, 0);
  EXPECT_TRUE(case_only_run.stdout_text.empty());
}

TEST(sdiff, sdiff_no_trailing_newline_uses_slash_separators) {
  TempDir tmp;
  tmp.write_bytes("a.txt", {'a', '\n', 'l', 'e', 'f', 't'});
  tmp.write("b.txt", "a\nright\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"-w", L"20", L"a.txt", L"b.txt"});
  auto r = p.run();

  // [GNU] When only one side of a changed row is newline-terminated the
  // separator becomes '/' (left continues) or '\\' (right continues).
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "a\ta\n"
                 "left  \\\tright\n");
}

TEST(sdiff, sdiff_output_file_auto_merges_non_interactively) {
  TempDir tmp;
  tmp.write("a.txt", "a\nleft\nkeep\n");
  tmp.write("b.txt", "a\nright\nkeep\nmore\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"-o", L"merged.txt", L"a.txt", L"b.txt"});
  auto r = p.run();

  // Issue #1127 P1: -o merges without prompting.  Overlapping changes
  // keep the left column; one-sided changes take their only side.
  // [GNU #1441] A completed merge of differing inputs exits 1, like
  // the subsidiary diff; the earlier pin of 0 rested on a wrong
  // "completed interactive merge" reading of GNU.
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(tmp.read("merged.txt"), "a\nleft\nkeep\nmore\n");
}

TEST(sdiff, sdiff_output_file_identical_inputs_exit_zero_issue1441) {
  TempDir tmp;
  tmp.write("a.txt", "same\n");
  tmp.write("b.txt", "same\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"-o", L"merged.txt", L"a.txt", L"b.txt"});
  auto r = p.run();

  // [GNU #1441] Identical inputs merge trivially and exit 0.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("merged.txt"), "same\n");
}

TEST(sdiff, sdiff_output_file_takes_right_for_right_only_changes) {
  TempDir tmp;
  tmp.write("a.txt", "a\nb\nc\n");
  tmp.write("b.txt", "a\nb\nc\nD\nE\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"-o", L"merged.txt", L"a.txt", L"b.txt"});
  auto r = p.run();

  // [GNU #1441] Insertion-only differences still make the inputs
  // differ, so the completed merge exits 1.
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(tmp.read("merged.txt"), "a\nb\nc\nD\nE\n");
}

TEST(sdiff, sdiff_output_file_keeps_left_for_conflicting_rows) {
  TempDir tmp;
  tmp.write("a.txt", "a\nB\nc\n");
  tmp.write("b.txt", "a\nb\nc\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"-o", L"merged.txt", L"a.txt", L"b.txt"});
  auto r = p.run();

  // [GNU #1441] A paired change makes the inputs differ, so the
  // completed merge exits 1.
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(tmp.read("merged.txt"), "a\nB\nc\n");
}

TEST(sdiff, sdiff_output_file_interactive_right_choice) {
  TempDir tmp;
  tmp.write("a.txt", "a\n");
  tmp.write("b.txt", "b\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("r\n");
  p.add(L"sdiff.exe", {L"-o", L"merged.txt", L"-w", L"80", L"a.txt", L"b.txt"});
  auto r = p.run();

  // [GNU sdiff.c edit] The hunk is shown, a '%' prompt is printed,
  // and the 'r' command merges the right version.  A completed merge
  // of differing inputs exits 1 (#1441).
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "a\t\t\t\t      |\tb\n%");
  EXPECT_EQ_TEXT(tmp.read("merged.txt"), "b\n");
}

TEST(sdiff, sdiff_output_file_interactive_quit_leaves_partial) {
  TempDir tmp;
  tmp.write("a.txt", "1\nA\n2\nB\n3\n");
  tmp.write("b.txt", "1\nX\n2\nY\n3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("l\nq\n");
  p.add(L"sdiff.exe", {L"-o", L"merged.txt", L"a.txt", L"b.txt"});
  auto r = p.run();

  // [GNU sdiff.c edit] 'q' quits with trouble status; the output
  // file keeps exactly the prefix merged before the quit (the first
  // hunk resolved left, plus the common line that followed it).
  EXPECT_EQ(r.exit_code, 2);
  EXPECT_EQ_TEXT(tmp.read("merged.txt"), "1\nA\n2\n");
}

TEST(sdiff, sdiff_output_file_interactive_unknown_command_helps) {
  TempDir tmp;
  tmp.write("a.txt", "a\n");
  tmp.write("b.txt", "b\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("?\nl\n");
  p.add(L"sdiff.exe", {L"-o", L"merged.txt", L"-w", L"80", L"a.txt", L"b.txt"});
  auto r = p.run();

  // [GNU sdiff.c give_help] An unrecognized command prints the help
  // text to stderr and prompts again; the following 'l' merges the
  // left version.
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "a\t\t\t\t      |\tb\n%%");
  EXPECT_EQ_TEXT(r.stderr_text,
                 "\n"
                 "ed:\tEdit then use both versions, each decorated with a "
                 "header.\n"
                 "eb:\tEdit then use both versions.\n"
                 "el or e1:\tEdit then use the left version.\n"
                 "er or e2:\tEdit then use the right version.\n"
                 "e:\tDiscard both versions then edit a new one.\n"
                 "l or 1:\tUse the left version.\n"
                 "r or 2:\tUse the right version.\n"
                 "s:\tSilently include common lines.\n"
                 "v:\tVerbosely include common lines.\n"
                 "q:\tQuit.\n");
  EXPECT_EQ_TEXT(tmp.read("merged.txt"), "a\n");
}

TEST(sdiff, sdiff_rejects_extra_operand_with_help_hint) {
  Pipeline p;
  p.add(L"sdiff.exe", {L"a.txt", L"b.txt", L"c.txt"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("sdiff extra operand stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ(r.stderr_text,
            "sdiff: extra operand 'c.txt'\n"
            "sdiff: Try 'sdiff --help' for more information.\n");
}

TEST(sdiff, sdiff_missing_all_operands_reports_help_hint) {
  Pipeline p;
  p.add(L"sdiff.exe", {});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("sdiff missing all operands stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ(r.stderr_text,
            "sdiff: missing operand after 'sdiff'\n"
            "sdiff: Try 'sdiff --help' for more information.\n");
}

TEST(sdiff, sdiff_single_operand_reports_help_hint) {
  Pipeline p;
  p.add(L"sdiff.exe", {L"a.txt"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("sdiff single operand stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ(r.stderr_text,
            "sdiff: missing operand after 'a.txt'\n"
            "sdiff: Try 'sdiff --help' for more information.\n");
}

TEST(sdiff, sdiff_missing_input_reports_diff_diagnostic) {
  TempDir tmp;
  tmp.write("b.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"missing.txt", L"b.txt"});
  auto r = p.run();

  // [GNU] Without -o sdiff execs diff, so the diagnostic carries diff's
  // program name and the exit status is trouble.
  EXPECT_EQ(r.exit_code, 2);
  EXPECT_EQ(r.stderr_text, "diff: missing.txt: No such file or directory\n");
}

// [#1213] GNU sdiff documents exit 2 for trouble; a rejected option used to
// exit 1.
TEST(sdiff, sdiff_unknown_option_exits_2_issue1213) {
  Pipeline p;
  p.add(L"sdiff.exe", {L"--zzz-nope"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  EXPECT_EXIT_CODE(r, 2);
  EXPECT_TRUE(r.stderr_text.find("unrecognized option") != std::string::npos);
}

TEST(sdiff, sdiff_ignore_matching_lines_regex_anchor_issue1449) {
  TempDir tmp;
  tmp.write("f1.txt", "keep\nskip-1\nend\n");
  tmp.write("f2.txt", "keep\nskip-2\nend\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"-w", L"80", L"-I", L"^skip-", L"f1.txt", L"f2.txt"});
  auto r = p.run();

  // [GNU #1449] -I takes a regular expression: both changed lines
  // match ^skip-, so the only change is ignored, the pair prints as
  // a common row, and the exit status is 0.  The old literal
  // substring match never found "^skip-" and exited 1.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "keep\t\t\t\t\tkeep\n"
                 "skip-1\t\t\t\t\tskip-2\n"
                 "end\t\t\t\t\tend\n");
}

TEST(sdiff, sdiff_ignore_matching_lines_regex_class_issue1449) {
  TempDir tmp;
  tmp.write("f1.txt", "keep\nskip-1\nend\n");
  tmp.write("f2.txt", "keep\nskip-2\nend\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe",
        {L"-I", L"skip-[12]", L"f1.txt", L"f2.txt"});
  auto r = p.run();

  // [GNU #1449] Character classes work: skip-[12] matches both
  // changed lines, so the change is ignored.
  EXPECT_EQ(r.exit_code, 0);
}

TEST(sdiff, sdiff_ignore_matching_lines_is_bre_issue1449) {
  TempDir tmp;
  tmp.write("f1.txt", "keep\nab-1\nend\n");
  tmp.write("f2.txt", "keep\nab-2\nend\n");

  // [GNU #1449] The -I RE is a basic regular expression: \+ is the
  // one-or-more operator, so ^ab\+ matches both changed lines.
  Pipeline escaped;
  escaped.set_cwd(tmp.wpath());
  escaped.add(L"sdiff.exe", {L"-I", L"^ab\\+", L"f1.txt", L"f2.txt"});
  auto escaped_run = escaped.run();
  EXPECT_EQ(escaped_run.exit_code, 0);

  // A bare + is a literal character in a BRE: ^ab+ matches neither
  // line, so the change stands.
  Pipeline bare;
  bare.set_cwd(tmp.wpath());
  bare.add(L"sdiff.exe", {L"-I", L"^ab+", L"f1.txt", L"f2.txt"});
  auto bare_run = bare.run();
  EXPECT_EQ(bare_run.exit_code, 1);
}

TEST(sdiff, sdiff_ignore_matching_lines_partial_hunk_kept_issue1449) {
  TempDir tmp;
  tmp.write("g1.txt", "top\nmatchA\nplain1\nbot\n");
  tmp.write("g2.txt", "top\nmatchB\nplain2\nbot\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"-w", L"80", L"-I", L"^match", L"g1.txt", L"g2.txt"});
  auto r = p.run();

  // [GNU #1449] A hunk is ignored only when ALL its changed lines
  // match: plain1/plain2 do not match ^match, so the hunk is shown
  // as a change and the exit status is 1.
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "top\t\t\t\t\ttop\n"
                 "matchA\t\t\t\t      |\tmatchB\n"
                 "plain1\t\t\t\t      |\tplain2\n"
                 "bot\t\t\t\t\tbot\n");
}

TEST(sdiff, sdiff_ignore_matching_lines_repeated_flags_issue1449) {
  TempDir tmp;
  tmp.write("k1.txt", "skip-1\nmid\nrealA\n");
  tmp.write("k2.txt", "skip-2\nmid\nrealB\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe",
        {L"-I", L"^skip-", L"-I", L"^real", L"k1.txt", L"k2.txt"});
  auto r = p.run();

  // [GNU #1449] Repeated -I options accumulate: the first hunk
  // matches the first RE, the second hunk the second, so both are
  // ignored.
  EXPECT_EQ(r.exit_code, 0);
}

TEST(sdiff, sdiff_ignore_matching_lines_invalid_regex_issue1449) {
  TempDir tmp;
  tmp.write("f1.txt", "a\n");
  tmp.write("f2.txt", "b\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"-I", L"[", L"f1.txt", L"f2.txt"});
  auto r = p.run();

  // [GNU #1449] An invalid -I RE is trouble (exit 2), diagnosed with
  // diff's wording before the operands are compared.
  EXPECT_EQ(r.exit_code, 2);
  EXPECT_EQ_TEXT(r.stderr_text, "diff: invalid regular expression '['\n");
}
