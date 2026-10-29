// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(cmp, cmp_same_files) {
  TempDir tmp;
  tmp.write("file1.txt", "hello\n");
  tmp.write("file2.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(cmp, cmp_different_files) {
  TempDir tmp;
  tmp.write("file1.txt", "hello\n");
  tmp.write("file2.txt", "world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  // [GNU] the plain message counts in characters, not bytes (#1443).
  EXPECT_EQ_TEXT(r.stdout_text, "file1.txt file2.txt differ: char 1, line 1\n");
}

TEST(cmp, cmp_print_bytes_uses_byte_wording_and_glyphs) {
  TempDir tmp;
  tmp.write("file1.txt", "hello\n");
  tmp.write("file2.txt", "world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"-b", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  // [GNU] -b keeps the "byte" wording and appends each differing byte
  // as a width-3 octal value plus its printable glyph (#1443).
  EXPECT_EQ_TEXT(r.stdout_text,
                 "file1.txt file2.txt differ: byte 1, line 1 is 150 h 167 w\n");
}

TEST(cmp, cmp_print_bytes_control_and_high_byte_glyphs) {
  TempDir tmp;
  tmp.write_bytes("c1.bin", {'A', 0x01, 'Z', '\n'});
  tmp.write_bytes("c2.bin", {'A', 0x02, 'Z', '\n'});
  tmp.write_bytes("d1.bin", {static_cast<char>(0x80), '\n'});
  tmp.write_bytes("d2.bin", {static_cast<char>(0xff), '\n'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"-b", L"c1.bin", L"c2.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  // [GNU] control bytes render in caret notation (#1443).
  EXPECT_EQ_TEXT(r.stdout_text,
                 "c1.bin c2.bin differ: byte 2, line 1 is   1 ^A   2 ^B\n");

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"cmp.exe", {L"-b", L"d1.bin", L"d2.bin"});
  auto r2 = p2.run();

  EXPECT_EQ(r2.exit_code, 1);
  // [GNU] bytes >= 0200 render as M- over the low seven bits (#1443).
  EXPECT_EQ_TEXT(r2.stdout_text,
                 "d1.bin d2.bin differ: byte 1, line 1 is 200 M-^@ 377 M-^?\n");
}

TEST(cmp, cmp_quiet_mode) {
  TempDir tmp;
  tmp.write("file1.txt", "hello\n");
  tmp.write("file2.txt", "world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"-s", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(cmp, cmp_last_output_mode_option_wins_to_verbose) {
  TempDir tmp;
  tmp.write("file1.txt", "hello\n");
  tmp.write("file2.txt", "world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"-s", L"-l", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(cmp, cmp_last_output_mode_option_wins_to_quiet) {
  TempDir tmp;
  tmp.write("file1.txt", "hello\n");
  tmp.write("file2.txt", "world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"-l", L"--quiet", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(cmp, cmp_positional_skip1_skips_both_files) {
  TempDir tmp;
  tmp.write("file1.txt", "xhello\n");
  tmp.write("file2.txt", "yhello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"file1.txt", L"file2.txt", L"1"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(cmp, cmp_positional_skip1_and_skip2_skip_independently) {
  TempDir tmp;
  tmp.write("file1.txt", "xxhello\n");
  tmp.write("file2.txt", "yhello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"file1.txt", L"file2.txt", L"2", L"1"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(cmp, cmp_ignore_initial_colon_spec_skips_independently) {
  TempDir tmp;
  tmp.write("file1.txt", "xxhello\n");
  tmp.write("file2.txt", "yhello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"-i", L"2:1", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(cmp, cmp_default_output_counts_byte_from_comparison_start_after_skip) {
  TempDir tmp;
  tmp.write("file1.txt", "xxa\n");
  tmp.write("file2.txt", "yyb\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"file1.txt", L"file2.txt", L"2", L"2"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "file1.txt file2.txt differ: char 1, line 1\n");
}

TEST(cmp, cmp_verbose_lists_all_differing_bytes) {
  TempDir tmp;
  tmp.write("file1.txt", "abx\ndf\n");
  tmp.write("file2.txt", "aby\neg\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"-l", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "3 170 171\n5 144 145\n6 146 147\n");
}

TEST(cmp, cmp_skip_beyond_shorter_file_reports_eof_without_underflow) {
  TempDir tmp;
  tmp.write("file1.txt", "abc");
  tmp.write("file2.txt", "abcdef");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"file1.txt", L"file2.txt", L"4", L"0"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  // [GNU] a skip past the shorter file's end yields the "which is empty"
  // form of the EOF diagnostic (cmp.c) — on stderr, not stdout (#1347).
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text, "cmp: EOF on file1.txt which is empty\n");
}

TEST(cmp, cmp_wildcard_expansion_does_not_reinterpret_extra_file_as_skip) {
  TempDir tmp;
  tmp.write("a.txt", "a");
  tmp.write("b.txt", "b");
  tmp.write("c.txt", "c");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"*.txt", L"c.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.find("extra operand") != std::string::npos);
}

TEST(cmp, cmp_missing_operands_report_help_hint) {
  Pipeline p;
  p.add(L"cmp.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "cmp: missing operand\nTry 'cmp --help' for more information.\n");
}

TEST(cmp, cmp_single_operand_reports_help_hint) {
  Pipeline p;
  p.add(L"cmp.exe", {L"a"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "cmp: missing operand after 'a'\nTry 'cmp --help' for more "
                 "information.\n");
}

TEST(cmp, cmp_extra_operand_reports_help_hint) {
  Pipeline p;
  p.add(L"cmp.exe", {L"a", L"b", L"1", L"2", L"extra"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "cmp: extra operand 'extra'\nTry 'cmp --help' for more information.\n");
}

// [#1212] `cmp - FILE` compared an empty text-mode stdin and reported EOF.
TEST(cmp, cmp_stdin_ctrlz_matches_file_issue1212) {
  const std::string payload(
      "AAAA\x1a"
      "BBBB\n",
      10);
  TempDir tmp;
  tmp.write_bytes("p.bin",
                  {'A', 'A', 'A', 'A', 0x1a, 'B', 'B', 'B', 'B', '\n'});
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin(payload);
  p.add(L"cmp.exe", {L"-", L"p.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
}

// [#1213] GNU cmp uses 2 for a usage/option error (1 means "differ"), so an
// unrecognized option must not exit 1.
TEST(cmp, cmp_unknown_option_exits_2_issue1213) {
  Pipeline p;
  p.add(L"cmp.exe", {L"--zzz-nope"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  EXPECT_EXIT_CODE(r, 2);
  EXPECT_TRUE(r.stderr_text.find("unrecognized option") != std::string::npos);
}

// [#1347] Byte counts take GNU cmp's suffix grammar: -n 1K is accepted
// (K = 1024), 1KB is 1000, and the historic c/w/b suffixes are rejected.
TEST(cmp, issue1347_cmp_bytes_suffix_accepted) {
  TempDir tmp;
  tmp.write("file1.txt", "hello\n");
  tmp.write("file2.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"-n", L"1K", L"file1.txt", L"file2.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(cmp, issue1347_cmp_bytes_suffix_kb_is_1000_k_is_1024) {
  TempDir tmp;
  // Two 2000-byte files differing only at byte 1006 (1-based).
  std::vector<char> a(2000, 0), b(2000, 0);
  a[1005] = 'x';
  b[1005] = 'y';
  tmp.write_bytes("a.bin", a);
  tmp.write_bytes("b.bin", b);

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"cmp.exe", {L"-n", L"1KB", L"a.bin", L"b.bin"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);  // 1KB = 1000 < 1006: difference not reached

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"cmp.exe", {L"-n", L"1K", L"a.bin", L"b.bin"});
  auto r2 = p2.run();
  EXPECT_EQ(r2.exit_code, 1);  // 1K = 1024 > 1006: difference reported
}

TEST(cmp, issue1347_cmp_bytes_suffix_c_and_b_rejected) {
  TempDir tmp;
  tmp.write("file1.txt", "a");
  tmp.write("file2.txt", "a");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"cmp.exe", {L"-n", L"1b", L"file1.txt", L"file2.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 2);
  EXPECT_TRUE(r1.stderr_text.find("invalid --bytes value '1b'") !=
              std::string::npos);

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"cmp.exe", {L"-n", L"1c", L"file1.txt", L"file2.txt"});
  auto r2 = p2.run();
  EXPECT_EQ(r2.exit_code, 2);
}

TEST(cmp, issue1347_cmp_silent_long_option) {
  TempDir tmp;
  tmp.write("file1.txt", "hello\n");
  tmp.write("file2.txt", "world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"--silent", L"file1.txt", L"file2.txt"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"cmp.exe", {L"--silent", L"missing", L"missing"});
  auto r2 = p2.run();
  EXPECT_EQ(r2.exit_code, 2);
  EXPECT_TRUE(r2.stdout_text.empty());
  EXPECT_TRUE(r2.stderr_text.empty());
}

// [#1347] Open failures carry exactly one "cmp: " prefix and the full
// reason text.
TEST(cmp, issue1347_cmp_missing_file_message) {
  TempDir tmp;
  tmp.write("file1.txt", "a");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"missing", L"file1.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "cmp: missing: No such file or directory\n");
}

TEST(cmp, issue1347_cmp_directory_message) {
  TempDir tmp;
  tmp.write("file1.txt", "a");
  std::filesystem::create_directory(tmp.path / "somedir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"somedir", L"file1.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_EQ_TEXT(r.stderr_text, "cmp: somedir: Is a directory\n");
}

// [#1347] EOF reports go to stderr; the line form is ", line N" when the
// shorter side ends at a line boundary and ", in line N" mid-line.
TEST(cmp, issue1347_cmp_eof_line_boundary_on_stderr) {
  TempDir tmp;
  tmp.write("f1", "a\n");
  tmp.write("f2", "a\nX\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"f1", L"f2"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text, "cmp: EOF on f1 after byte 2, line 1\n");
}

TEST(cmp, issue1347_cmp_eof_in_line_on_stderr) {
  TempDir tmp;
  tmp.write("f1", "a\nb\n");
  tmp.write("f2", "a\nb");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"f1", L"f2"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "cmp: EOF on f2 after byte 3, in line 2\n");

  TempDir tmp2;
  tmp2.write("g1", "ab");
  tmp2.write("g2", "abc");
  Pipeline p2;
  p2.set_cwd(tmp2.wpath());
  p2.add(L"cmp.exe", {L"g1", L"g2"});
  auto r2 = p2.run();
  EXPECT_EQ(r2.exit_code, 1);
  EXPECT_EQ_TEXT(r2.stderr_text,
                 "cmp: EOF on g1 after byte 2, in line 1\n");
}

TEST(cmp, issue1347_cmp_verbose_eof_form) {
  TempDir tmp;
  tmp.write("f1", "a\n");
  tmp.write("f2", "a\nX\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cmp.exe", {L"-l", L"f1", L"f2"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text, "cmp: EOF on f1 after byte 2\n");
}

TEST(cmp, issue1347_cmp_eof_on_stdin) {
  TempDir tmp;
  tmp.write("f2", "a\nX\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("");
  p.add(L"cmp.exe", {L"-", L"f2"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text, "cmp: EOF on - which is empty\n");
}
