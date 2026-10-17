// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(sha1sum, sha1sum_basic_file) {
  TempDir tmp;
  tmp.write("test.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha1sum.exe", {L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  // SHA1 of "hello\n" is: f572d396fae9206628714fb2ce00f72e94f2258f
  EXPECT_TRUE(r.stdout_text.find("f572d396fae9206628714fb2ce00f72e94f2258f") !=
              std::string::npos);
}

TEST(sha1sum, sha1sum_stdin) {
  Pipeline p;
  p.set_stdin("hello\n");
  p.add(L"sha1sum.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("f572d396fae9206628714fb2ce00f72e94f2258f") !=
              std::string::npos);
}

TEST(sha1sum, sha1sum_empty_file) {
  TempDir tmp;
  tmp.write("empty.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha1sum.exe", {L"empty.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // SHA1 of empty string is: da39a3ee5e6b4b0d3255bfef95601890afd80709
  EXPECT_TRUE(r.stdout_text.find("da39a3ee5e6b4b0d3255bfef95601890afd80709") !=
              std::string::npos);
}

TEST(sha1sum, sha1sum_missing_input_reports_no_such_file) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha1sum.exe", {L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("sha1sum: missing.txt: No such "
                                 "file or directory") != std::string::npos);
}

TEST(sha1sum, sha1sum_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("indir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha1sum.exe", {L"indir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("sha1sum: indir: Is a directory") !=
              std::string::npos);
}

TEST(sha1sum, sha1sum_short_zero_alias_uses_nul_terminator) {
  TempDir tmp;
  tmp.write("test.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha1sum.exe", {L"-z", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(
      r.stdout_text,
      std::string("f572d396fae9206628714fb2ce00f72e94f2258f *test.txt\0", 51));
}

TEST(sha1sum, sha1sum_check_valid) {
  TempDir tmp;
  tmp.write("check.txt", "hello\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"sha1sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 40) + "  check.txt";
  tmp.write("check.sha1", hash_line);

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"sha1sum.exe", {L"-c", L"check.sha1"});
  auto r2 = p2.run();

  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_TRUE(r2.stdout_text.find("check.txt: OK") != std::string::npos);
}

TEST(sha1sum, sha1sum_check_invalid) {
  TempDir tmp;
  tmp.write("check.txt", "hello\n");
  tmp.write("check.sha1",
            "0000000000000000000000000000000000000000  check.txt");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha1sum.exe", {L"-c", L"check.sha1"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("check.txt: FAILED") != std::string::npos);
}

TEST(sha1sum, sha1sum_check_status_suppresses_output) {
  TempDir tmp;
  tmp.write("check.txt", "hello\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"sha1sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 40) + "  check.txt";
  tmp.write("check.sha1", hash_line);

  Pipeline good;
  good.set_cwd(tmp.wpath());
  good.add(L"sha1sum.exe", {L"--status", L"-c", L"check.sha1"});
  auto good_result = good.run();

  EXPECT_EQ(good_result.exit_code, 0);
  EXPECT_EQ_TEXT(good_result.stdout_text, "");
  EXPECT_EQ_TEXT(good_result.stderr_text, "");

  tmp.write("check.sha1",
            "0000000000000000000000000000000000000000  check.txt");

  Pipeline bad;
  bad.set_cwd(tmp.wpath());
  bad.add(L"sha1sum.exe", {L"--status", L"-c", L"check.sha1"});
  auto bad_result = bad.run();

  EXPECT_NE(bad_result.exit_code, 0);
  EXPECT_EQ_TEXT(bad_result.stdout_text, "");
  EXPECT_EQ_TEXT(bad_result.stderr_text, "");
}

TEST(sha1sum, sha1sum_check_quiet_suppresses_ok_lines_only) {
  TempDir tmp;
  tmp.write("check.txt", "hello\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"sha1sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 40) + "  check.txt";
  tmp.write("check.sha1", hash_line);

  Pipeline good;
  good.set_cwd(tmp.wpath());
  good.add(L"sha1sum.exe", {L"--quiet", L"-c", L"check.sha1"});
  auto good_result = good.run();

  EXPECT_EQ(good_result.exit_code, 0);
  EXPECT_EQ_TEXT(good_result.stdout_text, "");
  EXPECT_EQ_TEXT(good_result.stderr_text, "");

  tmp.write("check.sha1",
            "0000000000000000000000000000000000000000  check.txt");

  Pipeline bad;
  bad.set_cwd(tmp.wpath());
  bad.add(L"sha1sum.exe", {L"--quiet", L"-c", L"check.sha1"});
  auto bad_result = bad.run();

  EXPECT_NE(bad_result.exit_code, 0);
  EXPECT_TRUE(bad_result.stdout_text.find("check.txt: FAILED") !=
              std::string::npos);
  // [GNU] --quiet suppresses "OK" lines only; the mismatch WARNING summary
  // still goes to stderr (verified against GNU coreutils 9.4).
  EXPECT_TRUE(bad_result.stderr_text.find(
                  "sha1sum: WARNING: 1 computed checksum did NOT match") !=
              std::string::npos);
}

TEST(sha1sum, sha1sum_check_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("checkdir");

  Pipeline check;
  check.set_cwd(tmp.wpath());
  check.add(L"sha1sum.exe", {L"-c", L"checkdir"});
  auto result = check.run();

  EXPECT_EQ(result.exit_code, 1);
  EXPECT_TRUE(result.stderr_text.find("sha1sum: checkdir: read error") !=
              std::string::npos);
}

TEST(sha1sum, sha1sum_check_reports_unreadable_listed_files) {
  TempDir tmp;
  tmp.write("check.sha1",
            "0000000000000000000000000000000000000000  missing.txt\n");

  Pipeline check;
  check.set_cwd(tmp.wpath());
  check.add(L"sha1sum.exe", {L"-c", L"check.sha1"});
  auto result = check.run();

  EXPECT_NE(result.exit_code, 0);
  EXPECT_TRUE(result.stdout_text.find("missing.txt: FAILED open or read") !=
              std::string::npos);
  EXPECT_TRUE(result.stderr_text.find(
                  "sha1sum: missing.txt: No such file or directory") !=
              std::string::npos);
  EXPECT_TRUE(result.stderr_text.find(
                  "sha1sum: WARNING: 1 listed file could not be read") !=
              std::string::npos);
}

TEST(sha1sum, sha1sum_check_ignore_missing_skips_missing_files) {
  TempDir tmp;
  tmp.write("check.sha1",
            "0000000000000000000000000000000000000000  missing.txt\n");

  Pipeline check;
  check.set_cwd(tmp.wpath());
  check.add(L"sha1sum.exe", {L"--ignore-missing", L"-c", L"check.sha1"});
  auto result = check.run();

  EXPECT_NE(result.exit_code, 0);
  EXPECT_EQ_TEXT(result.stdout_text, "");
  EXPECT_TRUE(result.stderr_text.find("no file was verified") !=
              std::string::npos);
}

TEST(sha1sum, sha1sum_check_accepts_binary_marker_lines) {
  TempDir tmp;
  tmp.write("check.txt", "hello\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"sha1sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 40) + " *check.txt";
  tmp.write("check.sha1", hash_line);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha1sum.exe", {L"-c", L"check.sha1"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("check.txt: OK") != std::string::npos);
}

TEST(sha1sum, sha1sum_check_warn_reports_malformed_line_locations) {
  TempDir tmp;
  tmp.write("check.txt", "hello\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"sha1sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 40) + "  check.txt";
  tmp.write("check.sha1", "not-a-checksum\n" + hash_line + "\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha1sum.exe", {L"--warn", L"-c", L"check.sha1"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(
      r.stderr_text.find(
          "sha1sum: check.sha1: 1: improperly formatted SHA1 checksum line") !=
      std::string::npos);
  EXPECT_TRUE(
      r.stderr_text.find("sha1sum: WARNING: 1 line is improperly formatted") !=
      std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("check.txt: OK") != std::string::npos);
}

TEST(sha1sum, sha1sum_check_without_valid_lines_reports_error) {
  TempDir tmp;
  tmp.write("check.sha1", "not-a-checksum\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha1sum.exe", {L"--warn", L"-c", L"check.sha1"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "");
  EXPECT_TRUE(r.stderr_text.find(
                  "sha1sum: check.sha1: no properly formatted checksum lines "
                  "found") != std::string::npos);
}

TEST(sha1sum, sha1sum_check_strict_rejects_malformed_lines) {
  TempDir tmp;
  tmp.write("check.txt", "hello\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"sha1sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 40) + "  check.txt";
  tmp.write("check.sha1", hash_line + "\nbad-line\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha1sum.exe", {L"--strict", L"-c", L"check.sha1"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("check.txt: OK") != std::string::npos);
}

// [#1195] stdin must be read in binary mode: the CRT's default text mode
// silently stops at the first 0x1A (Ctrl-Z) byte.  GNU hashes the full
// 10-byte payload for the pipe, the `<` redirect, and the file operand alike.
TEST(sha1sum, sha1sum_stdin_ctrl_z_pipe_hashes_full_payload_issue1195) {
  Pipeline p;
  // "AAAA\032BBBB\n" — a 0x1A byte in the middle of the payload.
  p.set_stdin(
      std::string("AAAA\x1a"
                  "BBBB\n",
                  10));
  p.add(L"sha1sum.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("c48bfe07cbde3bed26cc431a6dd97a90d6b7eefd") !=
              std::string::npos);  // GNU sha1 of the full 10-byte payload
  // The pre-fix truncated value (sha1("AAAA")) must not appear.
  EXPECT_TRUE(r.stdout_text.find("e2512172abf8cc9f67fdd49eb6cacf2df71bbad3") ==
              std::string::npos);
}

TEST(sha1sum, sha1sum_stdin_nul_pipe_unchanged_issue1195) {
  Pipeline p;
  // "AB\0CD" — NUL bytes were handled correctly before #1195 and must stay.
  p.set_stdin(
      std::string("AB"
                  "\x00"
                  "CD",
                  5));
  p.add(L"sha1sum.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("5e5ce29714682b63d04aa69501751cc91bbb6179") !=
              std::string::npos);  // GNU sha1 of "AB\0CD"
}

TEST(sha1sum, sha1sum_pipe_matches_file_operand_issue1195) {
  TempDir tmp;
  tmp.write("payload.bin", std::string("AAAA\x1a"
                                       "BBBB\n",
                                       10));

  Pipeline file;
  file.set_cwd(tmp.wpath());
  file.add(L"sha1sum.exe", {L"payload.bin"});
  auto file_result = file.run();

  EXPECT_EQ(file_result.exit_code, 0);
  EXPECT_TRUE(file_result.stdout_text.find(
                  "c48bfe07cbde3bed26cc431a6dd97a90d6b7eefd") !=
              std::string::npos);  // GNU sha1 of the full 10-byte payload

  Pipeline piped;
  piped.set_stdin(
      std::string("AAAA\x1a"
                  "BBBB\n",
                  10));
  piped.add(L"sha1sum.exe", {});
  auto pipe_result = piped.run();

  EXPECT_EQ(pipe_result.exit_code, 0);
  EXPECT_EQ(file_result.stdout_text.substr(0, 40),
            pipe_result.stdout_text.substr(0, 40));
}

TEST(sha1sum, sha1sum_stdin_plain_text_sanity_issue1195) {
  Pipeline p;
  p.set_stdin("hi\n");
  p.add(L"sha1sum.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("55ca6286e3e4f4fba5d0448333fa99fc5a404a73") !=
              std::string::npos);  // GNU sha1 of "hi\n"
}
