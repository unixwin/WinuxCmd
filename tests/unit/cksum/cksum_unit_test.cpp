// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(cksum, cksum_basic) {
  TempDir tmp;
  tmp.write("test.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cksum.exe", {L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should output checksum and byte count
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find("6") !=
              std::string::npos);  // byte count of "hello\n"
}

TEST(cksum, cksum_default_output_is_untagged_gnu_shape) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cksum.exe", {L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "3287646509 5 test.txt\n");
}

TEST(cksum, cksum_tag_opt_in_keeps_bsd_style_format) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cksum.exe", {L"--tag", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // [GNU] output_crc ignores tagged mode: CRC always prints the plain
  // "checksum length file" line, identical to no --tag (cksum.c:357-378).
  EXPECT_EQ_TEXT(r.stdout_text, "3287646509 5 test.txt\n");
}

TEST(cksum, cksum_stdin) {
  Pipeline p;
  p.set_stdin("hello\n");
  p.add(L"cksum.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(cksum, cksum_empty) {
  TempDir tmp;
  tmp.write("empty.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cksum.exe", {L"empty.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("0") != std::string::npos);
}

TEST(cksum, cksum_short_length_alias_matches_long_form) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  Pipeline short_form;
  short_form.set_cwd(tmp.wpath());
  short_form.add(L"cksum.exe", {L"-l", L"8", L"test.txt"});
  auto short_result = short_form.run();

  Pipeline long_form;
  long_form.set_cwd(tmp.wpath());
  long_form.add(L"cksum.exe", {L"--length=8", L"test.txt"});
  auto long_result = long_form.run();

  EXPECT_EQ(short_result.exit_code, 0);
  EXPECT_EQ(long_result.exit_code, 0);
  EXPECT_EQ_TEXT(short_result.stdout_text, long_result.stdout_text);
  EXPECT_EQ_TEXT(short_result.stderr_text, long_result.stderr_text);
}

TEST(cksum, cksum_continues_past_unreadable_files) {
  TempDir tmp;
  tmp.write("good.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cksum.exe", {L"missing.txt", L"good.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(
      r.stderr_text.find("cksum: missing.txt: No such file or directory") !=
      std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("good.txt") != std::string::npos);
}

TEST(cksum, cksum_missing_input_reports_no_such_file) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cksum.exe", {L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(
      r.stderr_text.find("cksum: missing.txt: No such file or directory") !=
      std::string::npos);
}

TEST(cksum, cksum_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("indir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cksum.exe", {L"indir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("cksum: indir: Is a directory") !=
              std::string::npos);
}

TEST(cksum, cksum_check_reports_no_valid_lines_found) {
  TempDir tmp;
  tmp.write("check.txt", "bad-line\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cksum.exe", {L"-c", L"check.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(
      r.stderr_text.find(
          "cksum: check.txt: no properly formatted checksum lines found") !=
      std::string::npos);
}

TEST(cksum, cksum_check_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("checkdir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cksum.exe", {L"-c", L"checkdir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("cksum: checkdir: Is a directory") !=
              std::string::npos);
}

TEST(cksum,
     cksum_check_ignores_malformed_lines_by_default_when_valid_records_exist) {
  TempDir tmp;
  tmp.write("good.txt", "hello");
  tmp.write("check.txt",
            "bad-line\n"
            "3287646509 5 good.txt\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cksum.exe", {L"-c", L"check.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("good.txt: OK") != std::string::npos);
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

TEST(cksum, cksum_check_warn_reports_malformed_line_location_and_summary) {
  TempDir tmp;
  tmp.write("good.txt", "hello");
  tmp.write("check.txt",
            "bad-line\n"
            "3287646509 5 good.txt\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cksum.exe", {L"--warn", L"-c", L"check.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("good.txt: OK") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find(
                  "cksum: check.txt: 1: improperly formatted checksum line") !=
              std::string::npos);
  EXPECT_TRUE(
      r.stderr_text.find("cksum: WARNING: 1 line is improperly formatted") !=
      std::string::npos);
}

TEST(cksum, cksum_check_strict_fails_when_any_line_is_malformed) {
  TempDir tmp;
  tmp.write("good.txt", "hello");
  tmp.write("check.txt",
            "bad-line\n"
            "3287646509 5 good.txt\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cksum.exe", {L"--strict", L"-c", L"check.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.find("good.txt: OK") != std::string::npos);
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

TEST(cksum, cksum_check_reports_unreadable_listed_files) {
  TempDir tmp;
  tmp.write("check.txt", "0 0 missing.txt\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cksum.exe", {L"-c", L"check.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(
      r.stderr_text.find("cksum: missing.txt: No such file or directory") !=
      std::string::npos);
  EXPECT_TRUE(
      r.stderr_text.find("cksum: WARNING: 1 listed file could not be read") !=
      std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("computed checksums did NOT match") ==
              std::string::npos);
}

TEST(cksum, cksum_check_ignore_missing_skips_missing_files) {
  TempDir tmp;
  tmp.write("check.txt", "0 0 missing.txt\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cksum.exe", {L"--ignore-missing", L"-c", L"check.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "");
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

TEST(cksum, cksum_check_mismatch_summary_counts_only_computed_records) {
  TempDir tmp;
  tmp.write("good.txt", "hello");
  tmp.write("check.txt",
            "0 0 missing.txt\n"
            "0 5 good.txt\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cksum.exe", {L"-c", L"check.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(
      r.stderr_text.find("cksum: WARNING: 1 listed file could not be read") !=
      std::string::npos);
  EXPECT_TRUE(r.stderr_text.find(
                  "cksum: WARNING: 1 of 1 computed checksums did NOT match") !=
              std::string::npos);
}

TEST(cksum, cksum_check_validates_size_field_in_untagged_records) {
  TempDir tmp;
  tmp.write("good.txt", "hello");
  tmp.write("check.txt", "3287646509 4 good.txt\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cksum.exe", {L"-c", L"check.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("good.txt: FAILED") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find(
                  "cksum: WARNING: 1 of 1 computed checksums did NOT match") !=
              std::string::npos);
}

TEST(cksum, cksum_check_status_suppresses_output) {
  TempDir tmp;
  tmp.write("good.txt", "hello");
  tmp.write("check.txt", "3287646509 5 good.txt\n");

  Pipeline good;
  good.set_cwd(tmp.wpath());
  good.add(L"cksum.exe", {L"--status", L"-c", L"check.txt"});
  auto good_result = good.run();

  EXPECT_EQ(good_result.exit_code, 0);
  EXPECT_EQ_TEXT(good_result.stdout_text, "");
  EXPECT_EQ_TEXT(good_result.stderr_text, "");

  tmp.write("check.txt", "0 5 good.txt\n");

  Pipeline bad;
  bad.set_cwd(tmp.wpath());
  bad.add(L"cksum.exe", {L"--status", L"-c", L"check.txt"});
  auto bad_result = bad.run();

  EXPECT_NE(bad_result.exit_code, 0);
  EXPECT_EQ_TEXT(bad_result.stdout_text, "");
  EXPECT_EQ_TEXT(bad_result.stderr_text, "");
}

TEST(cksum, cksum_check_quiet_suppresses_ok_lines_only) {
  TempDir tmp;
  tmp.write("good.txt", "hello");
  tmp.write("check.txt", "3287646509 5 good.txt\n");

  Pipeline good;
  good.set_cwd(tmp.wpath());
  good.add(L"cksum.exe", {L"--quiet", L"-c", L"check.txt"});
  auto good_result = good.run();

  EXPECT_EQ(good_result.exit_code, 0);
  EXPECT_EQ_TEXT(good_result.stdout_text, "");
  EXPECT_EQ_TEXT(good_result.stderr_text, "");

  tmp.write("check.txt", "0 5 good.txt\n");

  Pipeline bad;
  bad.set_cwd(tmp.wpath());
  bad.add(L"cksum.exe", {L"--quiet", L"-c", L"check.txt"});
  auto bad_result = bad.run();

  EXPECT_NE(bad_result.exit_code, 0);
  EXPECT_TRUE(bad_result.stdout_text.find("good.txt: FAILED") !=
              std::string::npos);
  EXPECT_TRUE(bad_result.stderr_text.find(
                  "cksum: WARNING: 1 of 1 computed checksums did NOT match") !=
              std::string::npos);
}
