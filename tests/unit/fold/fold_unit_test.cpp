// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(fold, fold_basic) {
  TempDir tmp;
  tmp.write(
      "test.txt",
      "This is a very long line that should be wrapped at a certain width\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"fold.exe", {L"-w", L"20", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Line should be wrapped
  EXPECT_TRUE(r.stdout_text.find("This is a very long") != std::string::npos);
}

TEST(fold, fold_stdin) {
  Pipeline p;
  p.set_stdin("This is a very long line that should be wrapped\n");
  p.add(L"fold.exe", {L"-w", L"20"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(fold, fold_short_line) {
  TempDir tmp;
  tmp.write("test.txt", "short\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"fold.exe", {L"-w", L"20", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "short\n");
}

TEST(fold, fold_preserves_missing_final_newline) {
  Pipeline p;
  p.set_stdin("abcdef");
  p.add(L"fold.exe", {L"-w", L"3"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "abc\ndef");
}

TEST(fold, fold_tabs_count_to_next_tab_stop) {
  Pipeline p;
  p.set_stdin("a\tb\n");
  p.add(L"fold.exe", {L"-w", L"8"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\t\nb\n");
}

TEST(fold, fold_bytes_counts_control_bytes) {
  Pipeline p;
  p.set_stdin("ab\bcd\n");
  p.add(L"fold.exe", {L"-b", L"-w", L"3"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "ab\b\ncd\n");
}

TEST(fold, fold_columns_account_for_backspace) {
  Pipeline p;
  p.set_stdin("ab\bcd\n");
  p.add(L"fold.exe", {L"-w", L"3"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "ab\bcd\n");
}

TEST(fold, fold_characters_option) {
  Pipeline p;
  p.set_stdin("abcd\n");
  p.add(L"fold.exe", {L"-c", L"-w", L"2"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "ab\ncd\n");
}

TEST(fold, fold_characters_option_counts_utf8_codepoints_without_splitting) {
  TempDir tmp;
  tmp.write_bytes("utf8.txt", {static_cast<char>(0xE4), static_cast<char>(0xB8),
                               static_cast<char>(0xAD), 'a',
                               static_cast<char>(0xE6), static_cast<char>(0x96),
                               static_cast<char>(0x87), 'b', '\n'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"fold.exe", {L"-c", L"-w", L"2", L"utf8.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, std::string("\xE4\xB8\xAD"
                                            "a\n"
                                            "\xE6\x96\x87"
                                            "b\n"));
}

TEST(fold, fold_default_mode_uses_display_width_for_utf8_wide_characters) {
  TempDir tmp;
  tmp.write_bytes("utf8.txt", {static_cast<char>(0xE4), static_cast<char>(0xB8),
                               static_cast<char>(0xAD), 'a',
                               static_cast<char>(0xE6), static_cast<char>(0x96),
                               static_cast<char>(0x87), 'b', '\n'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"fold.exe", {L"-w", L"2", L"utf8.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, std::string("\xE4\xB8\xAD"
                                            "\n"
                                            "a\n"
                                            "\xE6\x96\x87"
                                            "\n"
                                            "b\n"));
}

TEST(fold, fold_rejects_trailing_junk_width) {
  Pipeline p;
  p.set_stdin("abcd\n");
  p.add(L"fold.exe", {L"-w", L"2x"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("invalid number of columns: '2x'") !=
              std::string::npos);
}

// GNU accepts the obsolete "-WIDTH" form; uutils #14248.
TEST(fold, fold_obsolete_width_form) {
  Pipeline p;
  p.set_stdin("abcdefghij\n");
  p.add(L"fold.exe", {L"-5"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "abcde\nfghij\n");
}

TEST(fold, fold_obsolete_width_last_one_wins) {
  Pipeline p;
  p.set_stdin("abcdefghij\n");
  p.add(L"fold.exe", {L"-w", L"3", L"-5"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "abcde\nfghij\n");
}

TEST(fold, fold_width_after_terminator_is_a_file_name) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"fold.exe", {L"--", L"-3"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("cannot open '-3'") != std::string::npos);
}

TEST(fold, fold_negative_width_reports_invalid_number_of_columns) {
  Pipeline p;
  p.set_stdin("abcd\n");
  p.add(L"fold.exe", {L"-w", L"-1"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("invalid number of columns: '-1'") !=
              std::string::npos);
}

TEST(fold, fold_zero_width_reports_numerical_result_out_of_range) {
  Pipeline p;
  p.set_stdin("abcd\n");
  p.add(L"fold.exe", {L"-w", L"0"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("Numerical result out of range") !=
              std::string::npos);
}

TEST(fold, fold_spaces_breaks_at_last_blank) {
  Pipeline p;
  p.set_stdin("aa bb cc\n");
  p.add(L"fold.exe", {L"-s", L"-w", L"5"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "aa \nbb cc\n");
}

TEST(fold, fold_newline_mode_trims_trailing_cr_from_crlf_records) {
  TempDir tmp;
  tmp.write_bytes("crlf.txt", {'a', 'b', 'c', 'd', '\r', '\n', 'e', 'f', 'g',
                               'h', '\r', '\n'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"fold.exe", {L"-w", L"10", L"crlf.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "abcd\nefgh\n");
}

TEST(fold, fold_missing_input_reports_no_such_file) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"fold.exe", {L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "fold: cannot open 'missing.txt' for reading: No such file "
                  "or directory") != std::string::npos);
}

TEST(fold, fold_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("indir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"fold.exe", {L"indir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "fold: cannot open 'indir' for reading: Is a directory") !=
              std::string::npos);
}
