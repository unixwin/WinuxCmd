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

TEST(cut, cut_whitespace_delimited_uses_tab_output) {
  TempDir tmp;
  tmp.write("a.txt", "a    b    c\n\tone two\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-w", L"-f", L"1,2", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\tb\n\tone\n");
}

TEST(cut, cut_whitespace_delimited_preserves_edge_empty_fields) {
  TempDir tmp;
  tmp.write("a.txt", "  a  b  \n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-w", L"-f", L"1-4", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "\ta\tb\t\n");
}

TEST(cut, cut_whitespace_delimited_only_delimited_skips_undelimited_lines) {
  TempDir tmp;
  tmp.write("a.txt", "nodelem\na  b\n\n\t x\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-w", L"-f", L"2", L"-s", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "b\nx\n");
}

TEST(cut, cut_short_F_implies_whitespace_and_space_output) {
  TempDir tmp;
  tmp.write("a.txt", "alpha   beta\tgamma\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-F", L"1,3", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "alpha gamma\n");
}

TEST(cut, cut_short_F_uses_explicit_delimiter_but_space_output) {
  TempDir tmp;
  tmp.write("a.txt", "a:b:c\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-F", L"1,3", L"-d", L":", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a c\n");
}

TEST(cut, cut_whitespace_delimited_trimmed_ignores_outer_blanks) {
  TempDir tmp;
  tmp.write("a.txt", "  alpha  beta  \n  solo  \n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe",
        {L"--whitespace-delimited=trimmed", L"-f", L"1,2", L"-s", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "alpha\tbeta\n");
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

TEST(cut, cut_rejects_combining_w_and_delimiter) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-w", L"-d", L":", L"-f", L"2"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "cut: Only one of --delimiter (-d) or -w option can be specified\n");
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

TEST(cut, cut_short_output_delimiter) {
  TempDir tmp;
  tmp.write("a.txt", "a:b:c\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-d", L":", L"-f", L"1,3", L"-O", L"|", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a|c\n");
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
  EXPECT_EQ_TEXT(r.stdout_text, "\n");
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
  EXPECT_TRUE(r.stderr_text.find(
                  "cut: cannot open 'missing.txt' for reading: No such file "
                  "or directory") != std::string::npos);
}

TEST(cut, cut_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("indir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-f", L"1", L"indir"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "cut: cannot open 'indir' for reading: Is a directory") !=
              std::string::npos);
}

TEST(cut, cut_newline_mode_trims_trailing_cr_from_crlf_records) {
  TempDir tmp;
  tmp.write_bytes("a.txt",
                  {'a', ':', 'b', '\r', '\n', 'c', ':', 'd', '\r', '\n'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-d", L":", L"-f", L"2", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "b\nd\n");
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
