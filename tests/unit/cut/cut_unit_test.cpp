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
  EXPECT_TRUE(
      r.stderr_text.find("cut: cannot open 'indir' for reading: Is a directory") !=
      std::string::npos);
}

TEST(cut, cut_newline_mode_trims_trailing_cr_from_crlf_records) {
  TempDir tmp;
  tmp.write_bytes("a.txt", {'a', ':', 'b', '\r', '\n', 'c', ':', 'd', '\r', '\n'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cut.exe", {L"-d", L":", L"-f", L"2", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "b\nd\n");
}
