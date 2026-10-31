// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(sha256sum, sha256sum_basic_file) {
  TempDir tmp;
  tmp.write("test.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha256sum.exe", {L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  // SHA256 of "hello\n" is known value
  EXPECT_TRUE(r.stdout_text.length() > 64);
}

TEST(sha256sum, sha256sum_missing_input_reports_no_such_file) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha256sum.exe", {L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("sha256sum: missing.txt: No such "
                                 "file or directory") != std::string::npos);
}

TEST(sha256sum, sha256sum_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("indir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha256sum.exe", {L"indir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("sha256sum: indir: Is a directory") !=
              std::string::npos);
}

TEST(sha256sum, sha256sum_stdin) {
  Pipeline p;
  p.set_stdin("hello\n");
  p.add(L"sha256sum.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.length() > 64);
}

TEST(sha256sum, sha256sum_short_zero_alias_uses_nul_terminator) {
  TempDir tmp;
  tmp.write("test.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha256sum.exe", {L"-z", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(
      r.stdout_text,
      std::string(
          "5891b5b522d5df086d0ff0b110fbd9d21bb4fc7163af34d08286a2e846f6be03"
          "  test.txt\0",
          75));
}

TEST(sha256sum, sha256sum_check_valid) {
  TempDir tmp;
  tmp.write("check.txt", "hello\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"sha256sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 64) + "  check.txt";
  tmp.write("check.sha256", hash_line);

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"sha256sum.exe", {L"-c", L"check.sha256"});
  auto r2 = p2.run();

  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_TRUE(r2.stdout_text.find("check.txt: OK") != std::string::npos);
}

TEST(sha256sum, sha256sum_check_invalid) {
  TempDir tmp;
  tmp.write("check.txt", "hello\n");
  tmp.write("check.sha256",
            "0000000000000000000000000000000000000000000000000000000000000000  "
            "check.txt");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha256sum.exe", {L"-c", L"check.sha256"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("check.txt: FAILED") != std::string::npos);
}

TEST(sha256sum, sha256sum_check_status_suppresses_output) {
  TempDir tmp;
  tmp.write("check.txt", "hello\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"sha256sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 64) + "  check.txt";
  tmp.write("check.sha256", hash_line);

  Pipeline good;
  good.set_cwd(tmp.wpath());
  good.add(L"sha256sum.exe", {L"--status", L"-c", L"check.sha256"});
  auto good_result = good.run();

  EXPECT_EQ(good_result.exit_code, 0);
  EXPECT_EQ_TEXT(good_result.stdout_text, "");
  EXPECT_EQ_TEXT(good_result.stderr_text, "");

  tmp.write("check.sha256",
            "0000000000000000000000000000000000000000000000000000000000000000  "
            "check.txt");

  Pipeline bad;
  bad.set_cwd(tmp.wpath());
  bad.add(L"sha256sum.exe", {L"--status", L"-c", L"check.sha256"});
  auto bad_result = bad.run();

  EXPECT_NE(bad_result.exit_code, 0);
  EXPECT_EQ_TEXT(bad_result.stdout_text, "");
  EXPECT_EQ_TEXT(bad_result.stderr_text, "");
}

TEST(sha256sum, sha256sum_check_quiet_suppresses_ok_lines_only) {
  TempDir tmp;
  tmp.write("check.txt", "hello\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"sha256sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 64) + "  check.txt";
  tmp.write("check.sha256", hash_line);

  Pipeline good;
  good.set_cwd(tmp.wpath());
  good.add(L"sha256sum.exe", {L"--quiet", L"-c", L"check.sha256"});
  auto good_result = good.run();

  EXPECT_EQ(good_result.exit_code, 0);
  EXPECT_EQ_TEXT(good_result.stdout_text, "");
  EXPECT_EQ_TEXT(good_result.stderr_text, "");

  tmp.write("check.sha256",
            "0000000000000000000000000000000000000000000000000000000000000000  "
            "check.txt");

  Pipeline bad;
  bad.set_cwd(tmp.wpath());
  bad.add(L"sha256sum.exe", {L"--quiet", L"-c", L"check.sha256"});
  auto bad_result = bad.run();

  EXPECT_NE(bad_result.exit_code, 0);
  EXPECT_TRUE(bad_result.stdout_text.find("check.txt: FAILED") !=
              std::string::npos);
  // [GNU] --quiet suppresses "OK" lines only; the mismatch WARNING summary
  // still goes to stderr (verified against GNU coreutils 9.4).
  EXPECT_TRUE(bad_result.stderr_text.find(
                  "sha256sum: WARNING: 1 computed checksum did NOT match") !=
              std::string::npos);
}

TEST(sha256sum, sha256sum_check_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("checkdir");

  Pipeline check;
  check.set_cwd(tmp.wpath());
  check.add(L"sha256sum.exe", {L"-c", L"checkdir"});
  auto result = check.run();

  EXPECT_EQ(result.exit_code, 1);
  EXPECT_TRUE(result.stderr_text.find("sha256sum: checkdir: read error") !=
              std::string::npos);
}

TEST(sha256sum, sha256sum_check_reports_unreadable_listed_files) {
  TempDir tmp;
  tmp.write("check.sha256",
            "0000000000000000000000000000000000000000000000000000000000000000  "
            "missing.txt\n");

  Pipeline check;
  check.set_cwd(tmp.wpath());
  check.add(L"sha256sum.exe", {L"-c", L"check.sha256"});
  auto result = check.run();

  EXPECT_NE(result.exit_code, 0);
  EXPECT_TRUE(result.stdout_text.find("missing.txt: FAILED open or read") !=
              std::string::npos);
  EXPECT_TRUE(result.stderr_text.find(
                  "sha256sum: missing.txt: No such file or directory") !=
              std::string::npos);
  EXPECT_TRUE(result.stderr_text.find(
                  "sha256sum: WARNING: 1 listed file could not be read") !=
              std::string::npos);
}

TEST(sha256sum, sha256sum_check_ignore_missing_skips_missing_files) {
  TempDir tmp;
  tmp.write("check.sha256",
            "0000000000000000000000000000000000000000000000000000000000000000  "
            "missing.txt\n");

  Pipeline check;
  check.set_cwd(tmp.wpath());
  check.add(L"sha256sum.exe", {L"--ignore-missing", L"-c", L"check.sha256"});
  auto result = check.run();

  EXPECT_NE(result.exit_code, 0);
  EXPECT_EQ_TEXT(result.stdout_text, "");
  EXPECT_TRUE(result.stderr_text.find("no file was verified") !=
              std::string::npos);
}

TEST(sha256sum, sha256sum_check_accepts_binary_marker_lines) {
  TempDir tmp;
  tmp.write("check.txt", "hello\n");
  tmp.write("check.sha256",
            "5891b5b522d5df086d0ff0b110fbd9d21bb4fc7163af34d08286a2e846f6be03 "
            "*check.txt");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha256sum.exe", {L"-c", L"check.sha256"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("check.txt: OK") != std::string::npos);
}

TEST(sha256sum, sha256sum_check_warn_reports_malformed_line_locations) {
  TempDir tmp;
  tmp.write("check.txt", "hello\n");
  tmp.write("check.sha256",
            "not-a-checksum\n"
            "5891b5b522d5df086d0ff0b110fbd9d21bb4fc7163af34d08286a2e846f6be03  "
            "check.txt\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha256sum.exe", {L"--warn", L"-c", L"check.sha256"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("sha256sum: check.sha256: 1: improperly "
                                 "formatted SHA256 checksum line") !=
              std::string::npos);
  EXPECT_TRUE(r.stderr_text.find(
                  "sha256sum: WARNING: 1 line is improperly formatted") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("check.txt: OK") != std::string::npos);
}

TEST(sha256sum, sha256sum_check_without_valid_lines_reports_error) {
  TempDir tmp;
  tmp.write("check.sha256", "not-a-checksum\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha256sum.exe", {L"--warn", L"-c", L"check.sha256"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "");
  EXPECT_TRUE(r.stderr_text.find("sha256sum: check.sha256: no properly "
                                 "formatted checksum lines found") !=
              std::string::npos);
}

TEST(sha256sum, sha256sum_check_strict_rejects_malformed_lines) {
  TempDir tmp;
  tmp.write("check.txt", "hello\n");
  tmp.write("check.sha256",
            "5891b5b522d5df086d0ff0b110fbd9d21bb4fc7163af34d08286a2e846f6be03  "
            "check.txt\n"
            "bad-line\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha256sum.exe", {L"--strict", L"-c", L"check.sha256"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("check.txt: OK") != std::string::npos);
}

// [#1195] stdin must be read in binary mode: the CRT's default text mode
// silently stops at the first 0x1A (Ctrl-Z) byte.  GNU hashes the full
// 10-byte payload for the pipe, the `<` redirect, and the file operand alike.
TEST(sha256sum, sha256sum_stdin_ctrl_z_pipe_hashes_full_payload_issue1195) {
  Pipeline p;
  // "AAAA\032BBBB\n" — a 0x1A byte in the middle of the payload.
  p.set_stdin(
      std::string("AAAA\x1a"
                  "BBBB\n",
                  10));
  p.add(L"sha256sum.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(
      r.stdout_text.find(
          "947cd8c9aeb7e77eaf23c65ce58e442ac3d04f4ab249599efa541b46c239662d") !=
      std::string::npos);  // GNU sha256 of the full 10-byte payload
  // The pre-fix truncated value (sha256("AAAA")) must not appear.
  EXPECT_TRUE(
      r.stdout_text.find(
          "63c1dd951ffedf6f7fd968ad4efa39b8ed584f162f46e715114ee184f8de9201") ==
      std::string::npos);
}

TEST(sha256sum, sha256sum_stdin_nul_pipe_unchanged_issue1195) {
  Pipeline p;
  // "AB\0CD" — NUL bytes were handled correctly before #1195 and must stay.
  p.set_stdin(
      std::string("AB"
                  "\x00"
                  "CD",
                  5));
  p.add(L"sha256sum.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(
      r.stdout_text.find(
          "9bcc7c333a8038b7754d9d3e0d99440e86c2f385c8b2a0d5eaffa94a37607a25") !=
      std::string::npos);  // GNU sha256 of "AB\0CD"
}

TEST(sha256sum, sha256sum_pipe_matches_file_operand_issue1195) {
  TempDir tmp;
  tmp.write("payload.bin", std::string("AAAA\x1a"
                                       "BBBB\n",
                                       10));

  Pipeline file;
  file.set_cwd(tmp.wpath());
  file.add(L"sha256sum.exe", {L"payload.bin"});
  auto file_result = file.run();

  EXPECT_EQ(file_result.exit_code, 0);
  EXPECT_TRUE(
      file_result.stdout_text.find(
          "947cd8c9aeb7e77eaf23c65ce58e442ac3d04f4ab249599efa541b46c239662d") !=
      std::string::npos);  // GNU sha256 of the full 10-byte payload

  Pipeline piped;
  piped.set_stdin(
      std::string("AAAA\x1a"
                  "BBBB\n",
                  10));
  piped.add(L"sha256sum.exe", {});
  auto pipe_result = piped.run();

  EXPECT_EQ(pipe_result.exit_code, 0);
  EXPECT_EQ(file_result.stdout_text.substr(0, 64),
            pipe_result.stdout_text.substr(0, 64));
}

TEST(sha256sum, sha256sum_stdin_plain_text_sanity_issue1195) {
  Pipeline p;
  p.set_stdin("hi\n");
  p.add(L"sha256sum.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(
      r.stdout_text.find(
          "98ea6e4f216f2fb4b69fff9b3a44842c38686ca685f3f55dc48c5d3fb1107be4") !=
      std::string::npos);  // GNU sha256 of "hi\n"
}

// [#1205] the default output marker is a space (text); -b/--binary selects
// the '*' marker, and the last of -b/-t wins, matching GNU.
TEST(sha256sum, sha256sum_marker_text_default_and_last_option_wins_issue1205) {
  Pipeline def;
  def.set_stdin("hello\n");
  def.add(L"sha256sum.exe", {});
  auto rd = def.run();
  EXPECT_EQ(rd.exit_code, 0);
  EXPECT_TRUE(rd.stdout_text.ends_with("  -\n"));

  Pipeline bin;
  bin.set_stdin("hello\n");
  bin.add(L"sha256sum.exe", {L"-b"});
  auto rb = bin.run();
  EXPECT_EQ(rb.exit_code, 0);
  EXPECT_TRUE(rb.stdout_text.ends_with(" *-\n"));

  Pipeline last_bin;
  last_bin.set_stdin("hello\n");
  last_bin.add(L"sha256sum.exe", {L"-t", L"-b"});
  auto rlb = last_bin.run();
  EXPECT_EQ(rlb.stdout_text, rb.stdout_text);

  Pipeline last_text;
  last_text.set_stdin("hello\n");
  last_text.add(L"sha256sum.exe", {L"-b", L"-t"});
  auto rlt = last_text.run();
  EXPECT_EQ(rlt.stdout_text, rd.stdout_text);
}

// [#1462] An empty operand is a file name, not stdin (digest.c fopen
// semantics), matching GNU's "sha256sum: '': No such file or directory".
TEST(sha256sum, sha256sum_empty_operand_is_not_stdin) {
  Pipeline p;
  p.set_stdin("hello\n");
  p.add(L"sha256sum.exe", {L""});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_CONTAINS(r.stderr_text, "No such file or directory");
  EXPECT_TRUE(r.stdout_text.empty());
}
