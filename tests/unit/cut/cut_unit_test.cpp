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
