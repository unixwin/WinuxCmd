// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(csplit, csplit_basic) {
  TempDir tmp;
  tmp.write("test.txt", "line1\nseparator\nline2\nseparator\nline3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"csplit.exe", {L"test.txt", L"/separator/"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should create split files
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "xx00") ||
              std::filesystem::exists(tmp.path / "xx00.txt"));
}

TEST(csplit, csplit_pattern) {
  TempDir tmp;
  tmp.write("test.txt", "aaa\nbbb\nccc\nddd\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"csplit.exe", {L"test.txt", L"3"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "xx00") ||
              std::filesystem::exists(tmp.path / "xx00.txt"));
  EXPECT_EQ_TEXT(tmp.read("xx00"), "aaa\nbbb\n");
  EXPECT_EQ_TEXT(tmp.read("xx01"), "ccc\nddd\n");
  EXPECT_EQ_TEXT(r.stdout_text, "8\n8\n");
}

TEST(csplit, csplit_regex_repeat_with_offset) {
  TempDir tmp;
  tmp.write("test.txt", "h1\nA\nh2\nB\nh3\nC\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"csplit.exe", {L"test.txt", L"/^h/+1", L"{2}"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("xx00"), "h1\n");
  EXPECT_EQ_TEXT(tmp.read("xx01"), "A\nh2\n");
  EXPECT_EQ_TEXT(tmp.read("xx02"), "B\nh3\n");
  EXPECT_EQ_TEXT(tmp.read("xx03"), "C\n");
  EXPECT_EQ_TEXT(r.stdout_text, "3\n5\n5\n2\n");
}

TEST(csplit, csplit_skip_pattern_discards_segment) {
  TempDir tmp;
  tmp.write("test.txt", "drop\nMARK\nkeep\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"csplit.exe", {L"test.txt", L"%MARK%+1"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("xx00"), "keep\n");
  EXPECT_EQ_TEXT(r.stdout_text, "5\n");
}

TEST(csplit, csplit_suppress_matched_omits_delimiter_line) {
  TempDir tmp;
  tmp.write("test.txt", "before\nMARK\nafter\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"csplit.exe", {L"--suppress-matched", L"test.txt", L"/MARK/"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("xx00"), "before\n");
  EXPECT_EQ_TEXT(tmp.read("xx01"), "after\n");
  EXPECT_EQ_TEXT(r.stdout_text, "7\n6\n");
}

TEST(csplit, csplit_quiet_and_elide_empty_files) {
  TempDir tmp;
  tmp.write("test.txt", "MARK\nbody\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"csplit.exe", {L"-s", L"-z", L"test.txt", L"/MARK/"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "xx01"));
  EXPECT_EQ_TEXT(tmp.read("xx00"), "MARK\nbody\n");
  EXPECT_EQ_TEXT(r.stdout_text, "");
}

TEST(csplit, csplit_wildcard_input_rejects_ambiguous_match) {
  TempDir tmp;
  tmp.write("a.txt", "line1\nseparator\n");
  tmp.write("b.txt", "line2\nseparator\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"csplit.exe", {L"*.txt", L"/separator/"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("exactly one file") != std::string::npos ||
              r.stderr_text.find("exactly one file") != std::string::npos);
}

TEST(csplit, csplit_missing_input_reports_help_hint) {
  Pipeline p;
  p.add(L"csplit.exe", {});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("csplit missing input stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ(r.stderr_text,
            "csplit: missing operand\n"
            "Try 'csplit --help' for more information.\n");
}

TEST(csplit, csplit_missing_pattern_reports_help_hint) {
  TempDir tmp;
  tmp.write("test.txt", "line1\nline2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"csplit.exe", {L"test.txt"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("csplit missing pattern stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ(r.stderr_text,
            "csplit: missing operand after 'test.txt'\n"
            "Try 'csplit --help' for more information.\n");
}

TEST(csplit, csplit_line_numbers_suppress_matched_keeps_final_empty_split) {
  TempDir tmp;
  tmp.write("input.txt", "1\n2\n3\n4\n5\n6\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"csplit.exe", {L"--suppress-matched", L"input.txt", L"2", L"4", L"6"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("csplit suppress matched line-number stdout", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("xx00"), "1\n");
  EXPECT_EQ_TEXT(tmp.read("xx01"), "3\n");
  EXPECT_EQ_TEXT(tmp.read("xx02"), "5\n");
  EXPECT_EQ_TEXT(tmp.read("xx03"), "");
  EXPECT_EQ_TEXT(r.stdout_text, "2\n2\n2\n0\n");
}

TEST(csplit,
     csplit_line_numbers_suppress_matched_elides_final_empty_split_with_z) {
  TempDir tmp;
  tmp.write("input.txt", "1\n2\n3\n4\n5\n6\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"csplit.exe",
        {L"--suppress-matched", L"-z", L"input.txt", L"2", L"4", L"6"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("csplit suppress matched -z stdout", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("xx00"), "1\n");
  EXPECT_EQ_TEXT(tmp.read("xx01"), "3\n");
  EXPECT_EQ_TEXT(tmp.read("xx02"), "5\n");
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "xx03"));
  EXPECT_EQ_TEXT(r.stdout_text, "2\n2\n2\n");
}

TEST(csplit, csplit_negative_offset_crossing_split_start_matches_gnu) {
  TempDir tmp;
  std::string content;
  for (int i = 1; i <= 50; ++i) content += std::to_string(i) + "\n";
  tmp.write("s50.txt", content);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"csplit.exe", {L"s50.txt", L"/3$/-3"});

  auto r = p.run();

  // GNU: csplit: '/3$/-3': line number out of range, status 1, no xx* files.
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("'/3$/-3': line number out of range"),
            std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "xx00"));
}

TEST(csplit, csplit_negative_offset_crossing_second_split_matches_gnu) {
  TempDir tmp;
  std::string content;
  for (int i = 1; i <= 50; ++i) content += std::to_string(i) + "\n";
  tmp.write("s50.txt", content);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"csplit.exe", {L"s50.txt", L"/10$/", L"/12$/-3"});

  auto r = p.run();

  // Sizes of the closed segments are printed before the failure, then the
  // created files are deleted (no -k given).
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "18\n0\n");
  EXPECT_NE(r.stderr_text.find("'/12$/-3': line number out of range"),
            std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "xx00"));
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "xx01"));
}

TEST(csplit, csplit_keep_files_retains_created_files_on_error) {
  TempDir tmp;
  std::string content;
  for (int i = 1; i <= 50; ++i) content += std::to_string(i) + "\n";
  tmp.write("s50.txt", content);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"csplit.exe", {L"-k", L"s50.txt", L"/10$/", L"/12$/-3"});

  auto r = p.run();

  // GNU -k keeps the files created before the failure, including the empty
  // in-progress file of the failing pattern.
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(tmp.read("xx00"), "1\n2\n3\n4\n5\n6\n7\n8\n9\n");
  EXPECT_EQ_TEXT(tmp.read("xx01"), "");
}

TEST(csplit, csplit_match_not_found_message_matches_gnu) {
  TempDir tmp;
  tmp.write("test.txt", "aaa\nbbb\nccc\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"csplit.exe", {L"test.txt", L"/nomatch/"});

  auto r = p.run();

  // GNU: csplit: '/nomatch/': match not found, and the flushed segment file
  // is removed again without -k.
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("'/nomatch/': match not found"),
            std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "xx00"));
}

TEST(csplit, csplit_line_number_out_of_range_message_matches_gnu) {
  TempDir tmp;
  tmp.write("test.txt", "aaa\nbbb\nccc\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"csplit.exe", {L"test.txt", L"100"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("'100': line number out of range"),
            std::string::npos);
}

// [#1212] text mode cut the stream at 0x1A, so /line2/ never matched.
TEST(csplit, csplit_stdin_ctrlz_not_truncated_issue1212) {
  TempDir tmp;
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin(
      std::string("line1\x1a"
                  "line2\nline3\n",
                  18));
  p.add(L"csplit.exe", {L"-f", L"part", L"-", L"/line2/"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  const std::string joined = tmp.read("part00") + tmp.read("part01");
  EXPECT_TRUE(joined.find("line2") != std::string::npos);
}

namespace {

// #1276: the MSYS single-letter drive spelling of a native directory
// ("D:\tmp\x" -> "/d/tmp/x"). The dialect operand must resolve to the
// named directory regardless of the cwd's drive.
auto drive_dialect_dir_spelling(const std::filesystem::path& native)
    -> std::wstring {
  const std::wstring text = native.wstring();
  wchar_t drive = text[0];
  if (drive >= L'A' && drive <= L'Z') {
    drive = static_cast<wchar_t>(drive - L'A' + L'a');
  }
  std::wstring dialect;
  dialect.push_back(L'/');
  dialect.push_back(drive);
  for (size_t i = 2; i < text.size(); ++i) {
    dialect.push_back(text[i] == L'\\' ? L'/' : text[i]);
  }
  return dialect;
}

}  // namespace

// #1276: a "-f /d/..." dialect prefix must drive segment creation through
// the resolved spelling. Before the fix the bare ofstream resolved the raw
// "/d/..." filename against the cwd's drive, so the split files landed on
// the wrong drive (or failed to open) instead of the named directory.
TEST(csplit, csplit_prefix_drive_dialect_writes_named_directory) {
  TempDir tmp;
  TempDir cwd;
  cwd.write("test.txt", "aaa\nbbb\nccc\n");

  Pipeline p;
  p.set_cwd(cwd.wpath());
  p.add(L"csplit.exe", {L"-f", drive_dialect_dir_spelling(tmp.path) + L"/seg-",
                        L"test.txt", L"2"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("seg-00"), "aaa\n");
  EXPECT_EQ_TEXT(tmp.read("seg-01"), "bbb\nccc\n");
}
