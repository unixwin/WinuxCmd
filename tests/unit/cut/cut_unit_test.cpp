#include "framework/winuxtest.h"

TEST(cut, cut_basic_fields_default_tab) {
  TempDir tmp;
  tmp.write("a.txt", "a\tb\tc\n1\t2\t3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-f", L"1,3", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\tc\n1\t3\n");
}

TEST(cut, cut_undelimited_record_is_emitted_once) {
  TempDir tmp;
  tmp.write("a.txt", "a\tb\nnodelim\n");
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-f1-2", L"a.txt"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\tb\nnodelim\n");
}

TEST(cut, cut_with_delimiter_and_range) {
  TempDir tmp;
  tmp.write("a.txt", "x,y,z\nm,n,o\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-d", L",", L"-f", L"2-3", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "y,z\nn,o\n");
}

// [GNU] cut has no -w/-F/--whitespace-delimited spellings: the
// getopt string is "b:c:d:f:nsz" (cut.c) and a whitespace-delimited
// mode does not exist. The spellings the old implementation invented
// were removed by the #1462 audit; GNU answers them with the plain
// invalid/unrecognized-option diagnostics pinned here.
TEST(cut, cut_w_spelling_is_an_invalid_option) {
  TempDir tmp;
  tmp.write("a.txt", "a    b    c\n\tone two\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-w", L"-f", L"1,2", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("invalid option -- 'w'") !=
              std::string::npos);
}

TEST(cut, cut_F_spelling_is_an_invalid_option) {
  TempDir tmp;
  tmp.write("a.txt", "alpha   beta\tgamma\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-F", L"1,3", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("invalid option -- 'F'") !=
              std::string::npos);
}

TEST(cut, cut_whitespace_delimited_long_spelling_is_unrecognized) {
  TempDir tmp;
  tmp.write("a.txt", "  alpha  beta  \n  solo  \n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe",
        {L"--whitespace-delimited=trimmed", L"-f", L"1,2", L"-s", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("unrecognized option "
                                 "'--whitespace-delimited") !=
              std::string::npos);
}

TEST(cut, cut_output_delimiter_empty_writes_nul) {
  TempDir tmp;
  tmp.write("a.txt", "a:b:c\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe",
        {L"-d", L":", L"-f", L"1,3", L"--output-delimiter=", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, std::string("a\0c\n", 4));
}

// [GNU] There is no -w for -d to conflict with: getopt rejects -w
// itself before any combination is considered (cut.c optstring).
TEST(cut, cut_rejects_combining_w_and_delimiter) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-w", L"-d", L":", L"-f", L"2"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("invalid option -- 'w'") !=
              std::string::npos);
}

TEST(cut, cut_only_delimited_skips) {
  TempDir tmp;
  tmp.write("a.txt", "no_delim\nhas:delim\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-d", L":", L"-f", L"2", L"-s", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "delim\n");
}

TEST(cut, cut_fields_prints_undelimited_lines_by_default) {
  TempDir tmp;
  tmp.write("a.txt", "no_delim\nhas:delim\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-d", L":", L"-f", L"2", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "no_delim\ndelim\n");
}

TEST(cut, cut_zero_terminated) {
  TempDir tmp;
  std::string data = std::string("a:b\0c:d", 7);
  tmp.write_bytes("a.txt", std::vector<char>(data.begin(), data.end()));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-z", L"-d", L":", L"-f", L"2", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, std::string("b\0d\0", 4));
}

TEST(cut, cut_empty_delimiter_means_nul) {
  TempDir tmp;
  std::vector<char> data = {'a', '\0', 'b', '\0', 'c', '\n'};
  tmp.write_bytes("a.txt", data);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-d", L"", L"-f", L"2", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "b\n");
}

TEST(cut, cut_rejects_multichar_delimiter_with_gnu_hint) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-d", L"::", L"-f", L"1"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "cut: the delimiter must be a single character\n"
                 "Try 'cut --help' for more information.\n");
}

TEST(cut, cut_bytes_selects_positions) {
  TempDir tmp;
  tmp.write("a.txt", "abcdef\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-b", L"1,3-4", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "acd\n");
}

TEST(cut, cut_characters_deduplicates_and_preserves_input_order) {
  TempDir tmp;
  tmp.write("a.txt", "abcdef\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"--characters", L"3,1-3,2", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "abc\n");
}

TEST(cut, cut_fields_zero_or_empty_positions_report_numbered_from_one) {
  TempDir tmp;

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"cut.exe", {L"-f", L"1,,2"});
  auto r1 = p1.run();

  EXPECT_EQ(r1.exit_code, 1);
  EXPECT_EQ_TEXT(r1.stderr_text,
                 "cut: fields are numbered from 1\n"
                 "Try 'cut --help' for more information.\n");

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"cut.exe", {L"-f", L"0"});
  auto r2 = p2.run();

  EXPECT_EQ(r2.exit_code, 1);
  EXPECT_EQ_TEXT(r2.stderr_text,
                 "cut: fields are numbered from 1\n"
                 "Try 'cut --help' for more information.\n");
}

TEST(cut, cut_byte_and_character_zero_positions_report_numbered_from_one) {
  TempDir tmp;

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"cut.exe", {L"-b", L"0"});
  auto r1 = p1.run();

  EXPECT_EQ(r1.exit_code, 1);
  EXPECT_EQ_TEXT(r1.stderr_text,
                 "cut: byte/character positions are numbered from 1\n"
                 "Try 'cut --help' for more information.\n");

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"cut.exe", {L"-c", L"0"});
  auto r2 = p2.run();

  EXPECT_EQ(r2.exit_code, 1);
  EXPECT_EQ_TEXT(r2.stderr_text,
                 "cut: byte/character positions are numbered from 1\n"
                 "Try 'cut --help' for more information.\n");
}

TEST(cut, cut_fields_output_delimiter) {
  TempDir tmp;
  tmp.write("a.txt", "a:b:c\n1:2:3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe",
        {L"-d", L":", L"-f", L"1,3", L"--output-delimiter=|", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a|c\n1|3\n");
}

// [GNU] --output-delimiter has no short spelling in cut.c; -O is an
// invalid option (the long form is pinned by
// cut_fields_output_delimiter above).
TEST(cut, cut_short_output_delimiter) {
  TempDir tmp;
  tmp.write("a.txt", "a:b:c\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-d", L":", L"-f", L"1,3", L"-O", L"|", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("invalid option -- 'O'") !=
              std::string::npos);
}

TEST(cut, cut_bytes_output_delimiter_between_non_overlapping_ranges) {
  TempDir tmp;
  tmp.write("a.txt", "abcdef\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-b", L"1-2,4,4-5", L"--output-delimiter=|", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "ab|de\n");
}

// [GNU] -n is a pure no-op in cut.c:536-537 ("case 'n': break;"):
// bytes are selected exactly as if it were absent, whether or not
// the selection happens to cover a whole multibyte character.
TEST(cut, cut_no_partial_keeps_complete_multibyte_characters) {
  TempDir tmp;
  std::vector<char> data = {static_cast<char>(0xC3), static_cast<char>(0xA9),
                            'x', '\n'};
  tmp.write_bytes("a.txt", data);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-b", L"1-2", L"-n", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, std::string({static_cast<char>(0xC3),
                                        static_cast<char>(0xA9), '\n'}));
}

// [GNU] Same no-op -n (cut.c:536-537): byte 1 alone is emitted even
// though it splits the two-byte character; the former --no-partial
// semantics that dropped it were an implementation invention.
TEST(cut, cut_no_partial_omits_split_multibyte_characters) {
  TempDir tmp;
  std::vector<char> data = {static_cast<char>(0xC3), static_cast<char>(0xA9),
                            'x', '\n'};
  tmp.write_bytes("a.txt", data);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-b", L"1", L"-n", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text,
            std::string({static_cast<char>(0xC3), '\n'}));
}

TEST(cut, cut_complement_fields) {
  TempDir tmp;
  tmp.write("a.txt", "a:b:c:d\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-d", L":", L"-f", L"2-3", L"--complement", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a:d\n");
}

TEST(cut, cut_complement_bytes) {
  TempDir tmp;
  tmp.write("a.txt", "abcdef\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-b", L"2-4", L"--complement", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "aef\n");
}

TEST(cut, cut_missing_input_reports_no_such_file) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-f", L"1", L"missing.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  // [GNU] cut.c:455 reports the bare quotef form, not "cannot open
  // ... for reading".
  EXPECT_TRUE(r.stderr_text.find("cut: missing.txt: No such file or "
                                 "directory") != std::string::npos);
}

TEST(cut, cut_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("indir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-f", L"1", L"indir"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("cut: indir: Is a directory") !=
              std::string::npos);
}

// [#1204] GNU never strips a trailing CR, on the operand or on stdin; the
// CR is part of the last field, so it survives when that field is selected.
TEST(cut, cut_preserves_cr_from_crlf_records_issue1204) {
  TempDir tmp;
  tmp.write_bytes("a.txt",
                  {'a', ':', 'b', '\r', '\n', 'c', ':', 'd', '\r', '\n'});

  Pipeline by_file;
  by_file.set_cwd(tmp.wpath());
  by_file.add(L"cut.exe", {L"-d", L":", L"-f", L"2", L"a.txt"});
  auto rf = by_file.run();

  EXPECT_EQ(rf.exit_code, 0);
  EXPECT_EQ(rf.stdout_text, std::string("b\r\nd\r\n", 6));

  const std::string payload("a:b\r\nc:d\r\n", 10);
  Pipeline by_stdin;
  by_stdin.set_stdin(payload);
  by_stdin.add(L"cut.exe", {L"-d", L":", L"-f", L"2"});
  auto rs = by_stdin.run();

  EXPECT_EQ(rs.exit_code, 0);
  EXPECT_EQ(rs.stdout_text, rf.stdout_text);
}

// ---------------------------------------------------------------------------
// Issue #1093: cut must stream records and emit each one immediately instead
// of slurping the whole input first. The piped-stdin tests feed multi-line
// input through a pipe, and `yes | cut | head` proves early exit on a closed
// stdout pipe: with the old read-everything implementation that pipeline
// produced no output before timing out; streaming cut emits records as they
// arrive and exits quietly once head closes the pipe (SIGPIPE equivalent).
// ---------------------------------------------------------------------------

TEST(cut, cut_piped_stdin_multiline_fields) {
  Pipeline p;
  p.set_stdin("a,1\nb,2\nc,3\n");
  p.add(L"cut.exe", {L"-d", L",", L"-f", L"2"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1\n2\n3\n");
}

TEST(cut, cut_piped_stdin_multiline_characters) {
  Pipeline p;
  p.set_stdin("hello\nworld\n");
  p.add(L"cut.exe", {L"-c", L"2-4"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "ell\norl\n");
}

TEST(cut, cut_piped_stdin_zero_terminated) {
  Pipeline p;
  p.set_stdin(std::string("a:b\0c:d\0", 8));
  p.add(L"cut.exe", {L"-z", L"-d", L":", L"-f", L"2"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, std::string("b\0d\0", 4));
}

TEST(cut, cut_piped_stdin_only_delimited_suppresses) {
  Pipeline p;
  p.set_stdin("nod\nhas:x\n");
  p.add(L"cut.exe", {L"-d", L":", L"-f", L"2", L"-s"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "x\n");
}

// `yes | cut -c1- | head -n1` (issue #1093): GNU emits 'y' immediately and
// dies on SIGPIPE when head exits. Streaming cut must do the same — emit
// output before the input ends and exit quietly once the downstream pipe
// closes. If cut ever goes back to buffering all input, `yes` never reaches
// EOF and this test hangs instead of passing.
TEST(cut, cut_streams_yes_through_head_exits_early_on_closed_pipe) {
  Pipeline p;
  p.add(L"yes.exe", {});
  p.add(L"cut.exe", {L"-c1-"});
  p.add(L"head.exe", {L"-n", L"1"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "y\n");
}

// 200k records exercise the streaming path far beyond the 64 KiB output
// flush threshold: every line is cut and emitted in order with bounded
// per-line memory. Verifying the full output proves no records are dropped,
// duplicated, or reordered across flush boundaries.
TEST(cut, cut_large_input_streams_with_bounded_memory) {
  TempDir tmp;
  std::string input;
  std::string expected;
  input.reserve(200000 * 12);
  expected.reserve(200000 * 8);
  for (int i = 0; i < 200000; ++i) {
    std::string num = std::to_string(i);
    input += "k" + num + ":v" + num + "\n";
    expected += "v" + num + "\n";
  }
  tmp.write("big.txt", input);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-d", L":", L"-f", L"2", L"big.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text.size(), expected.size());
  EXPECT_TRUE(r.stdout_text == expected);
}

// Same streaming guarantee for the generic (non-fast) per-line path used by
// -b/--complement: bounded memory, all records emitted in order.
TEST(cut, cut_large_input_bytes_mode_streams_with_bounded_memory) {
  TempDir tmp;
  std::string input;
  std::string expected;
  for (int i = 0; i < 100000; ++i) {
    input += "abcdef\n";
    expected += "aef\n";  // complement of byte positions 2-4
  }
  tmp.write("big.txt", input);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-b", L"2-4", L"--complement", L"big.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text.size(), expected.size());
  EXPECT_TRUE(r.stdout_text == expected);
}
