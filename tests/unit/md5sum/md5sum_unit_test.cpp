// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(md5sum, md5sum_stdin) {
  Pipeline p;
  p.set_stdin("hello world\n");
  p.add(L"md5sum.exe", {});

  TEST_LOG_CMD_LIST("md5sum.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("md5sum output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(md5sum, md5sum_file) {
  TempDir tmp;
  tmp.write("test.txt", "hello world\n");

  TEST_LOG_FILE_CONTENT("test.txt", "hello world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"md5sum.exe", {L"test.txt"});

  TEST_LOG_CMD_LIST("md5sum.exe", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("md5sum file output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(md5sum, md5sum_binary) {
  Pipeline p;
  p.set_stdin("hello world\n");
  p.add(L"md5sum.exe", {L"-b"});

  TEST_LOG_CMD_LIST("md5sum.exe", L"-b");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("md5sum binary output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(md5sum, md5sum_wildcard) {
  TempDir tmp;
  tmp.write("file1.txt", "hello");
  tmp.write("file2.txt", "world");
  tmp.write("other.log", "log");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"md5sum.exe", {L"*.txt"});

  TEST_LOG_CMD_LIST("md5sum.exe", L"*.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("md5sum wildcard output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("file1.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file2.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("other.log") == std::string::npos);
}

TEST(md5sum, md5sum_text_mode) {
  Pipeline p;
  p.set_stdin("hello world\n");
  p.add(L"md5sum.exe", {L"-t"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Text mode should produce valid hash
  EXPECT_TRUE(r.stdout_text.find("md5sum") == std::string::npos);
}

TEST(md5sum, md5sum_tag) {
  Pipeline p;
  p.set_stdin("hello world\n");
  p.add(L"md5sum.exe", {L"--tag"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // --tag should output in BSD-style format
  EXPECT_TRUE(r.stdout_text.find("MD5") != std::string::npos);
}

TEST(md5sum, md5sum_short_zero_alias_uses_nul_terminator) {
  TempDir tmp;
  tmp.write("test.txt", "hello world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"md5sum.exe", {L"-z", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text,
            std::string("6f5902ac237024bdd0c176cb93063dc4  test.txt\0", 43));
}

TEST(md5sum, md5sum_quiet) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"md5sum.exe", {L"--quiet", L"test.txt"});
  auto r = p.run();

  // [GNU] --quiet is rejected outside check mode.
  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("meaningful only when verifying checksums") !=
              std::string::npos);
}

TEST(md5sum, md5sum_check_valid) {
  TempDir tmp;
  tmp.write("check.txt", "hello world\n");
  // Get the hash first
  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"md5sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  // Write hash to check file
  std::string hash_line = r1.stdout_text.substr(0, 32) + "  check.txt";
  tmp.write("check.md5", hash_line);

  // Verify
  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"md5sum.exe", {L"-c", L"check.md5"});
  auto r2 = p2.run();

  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_TRUE(r2.stdout_text.find("check.txt: OK") != std::string::npos);
}

TEST(md5sum, md5sum_check_invalid) {
  TempDir tmp;
  tmp.write("check.txt", "hello world\n");
  tmp.write("check.md5", "00000000000000000000000000000000  check.txt");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"md5sum.exe", {L"-c", L"check.md5"});
  auto r = p.run();

  // Should fail with mismatched hash
  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("check.txt: FAILED") != std::string::npos);
}

TEST(md5sum, md5sum_strict) {
  TempDir tmp;
  tmp.write("strict.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"md5sum.exe", {L"--strict", L"strict.txt"});
  auto r = p.run();

  // [GNU] --strict is rejected outside check mode.
  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("meaningful only when verifying checksums") !=
              std::string::npos);
}

TEST(md5sum, md5sum_missing_input_reports_no_such_file) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"md5sum.exe", {L"missing.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("md5sum: missing.txt: No such "
                                 "file or directory") != std::string::npos);
}

TEST(md5sum, md5sum_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("indir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"md5sum.exe", {L"indir"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("md5sum: indir: Is a directory") !=
              std::string::npos);
}

TEST(md5sum, md5sum_check_status_suppresses_output) {
  TempDir tmp;
  tmp.write("check.txt", "hello world\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"md5sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 32) + "  check.txt";
  tmp.write("check.md5", hash_line);

  Pipeline good;
  good.set_cwd(tmp.wpath());
  good.add(L"md5sum.exe", {L"--status", L"-c", L"check.md5"});
  auto good_result = good.run();

  EXPECT_EQ(good_result.exit_code, 0);
  EXPECT_EQ_TEXT(good_result.stdout_text, "");
  EXPECT_EQ_TEXT(good_result.stderr_text, "");

  tmp.write("check.md5", "00000000000000000000000000000000  check.txt");

  Pipeline bad;
  bad.set_cwd(tmp.wpath());
  bad.add(L"md5sum.exe", {L"--status", L"-c", L"check.md5"});
  auto bad_result = bad.run();

  EXPECT_NE(bad_result.exit_code, 0);
  EXPECT_EQ_TEXT(bad_result.stdout_text, "");
  EXPECT_EQ_TEXT(bad_result.stderr_text, "");
}

TEST(md5sum, md5sum_check_quiet_suppresses_ok_lines_only) {
  TempDir tmp;
  tmp.write("check.txt", "hello world\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"md5sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 32) + "  check.txt";
  tmp.write("check.md5", hash_line);

  Pipeline good;
  good.set_cwd(tmp.wpath());
  good.add(L"md5sum.exe", {L"--quiet", L"-c", L"check.md5"});
  auto good_result = good.run();

  EXPECT_EQ(good_result.exit_code, 0);
  EXPECT_EQ_TEXT(good_result.stdout_text, "");
  EXPECT_EQ_TEXT(good_result.stderr_text, "");

  tmp.write("check.md5", "00000000000000000000000000000000  check.txt");

  Pipeline bad;
  bad.set_cwd(tmp.wpath());
  bad.add(L"md5sum.exe", {L"--quiet", L"-c", L"check.md5"});
  auto bad_result = bad.run();

  EXPECT_NE(bad_result.exit_code, 0);
  EXPECT_TRUE(bad_result.stdout_text.find("check.txt: FAILED") !=
              std::string::npos);
  // [GNU] --quiet suppresses "OK" lines only; the mismatch WARNING summary
  // still goes to stderr (verified against GNU coreutils 9.4).
  EXPECT_TRUE(bad_result.stderr_text.find(
                  "md5sum: WARNING: 1 computed checksum did NOT match") !=
              std::string::npos);
}

TEST(md5sum, md5sum_check_warn_reports_malformed_line_location_and_summary) {
  TempDir tmp;
  tmp.write("check.txt", "hello world\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"md5sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 32) + "  check.txt";
  tmp.write("check.md5", "bad line\n" + hash_line + "\n");

  Pipeline check;
  check.set_cwd(tmp.wpath());
  check.add(L"md5sum.exe", {L"--warn", L"-c", L"check.md5"});
  auto result = check.run();

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_TRUE(result.stdout_text.find("check.txt: OK") != std::string::npos);
  EXPECT_TRUE(
      result.stderr_text.find(
          "md5sum: check.md5: 1: improperly formatted MD5 checksum line") !=
      std::string::npos);
  EXPECT_TRUE(result.stderr_text.find(
                  "md5sum: WARNING: 1 line is improperly formatted") !=
              std::string::npos);
}

TEST(md5sum, md5sum_check_without_any_valid_lines_fails) {
  TempDir tmp;
  tmp.write("check.md5", "bad line\n");

  Pipeline check;
  check.set_cwd(tmp.wpath());
  check.add(L"md5sum.exe", {L"-c", L"check.md5"});
  auto result = check.run();

  EXPECT_NE(result.exit_code, 0);
  EXPECT_EQ_TEXT(result.stdout_text, "");
  EXPECT_TRUE(result.stderr_text.find("md5sum: check.md5: no properly "
                                      "formatted checksum lines found") !=
              std::string::npos);
}

TEST(md5sum, md5sum_check_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("checkdir");

  Pipeline check;
  check.set_cwd(tmp.wpath());
  check.add(L"md5sum.exe", {L"-c", L"checkdir"});
  auto result = check.run();

  EXPECT_EQ(result.exit_code, 1);
  EXPECT_TRUE(result.stderr_text.find("md5sum: checkdir: read error") !=
              std::string::npos);
}

TEST(md5sum, md5sum_check_accepts_binary_marker_lines) {
  TempDir tmp;
  tmp.write("check.txt", "hello world\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"md5sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 32) + " *check.txt";
  tmp.write("check.md5", hash_line);

  Pipeline check;
  check.set_cwd(tmp.wpath());
  check.add(L"md5sum.exe", {L"-c", L"check.md5"});
  auto result = check.run();

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_TRUE(result.stdout_text.find("check.txt: OK") != std::string::npos);
}

TEST(md5sum, md5sum_check_strict_fails_when_any_line_is_malformed) {
  TempDir tmp;
  tmp.write("check.txt", "hello world\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"md5sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 32) + "  check.txt";
  tmp.write("check.md5", hash_line + "\nnot-a-checksum\n");

  Pipeline check;
  check.set_cwd(tmp.wpath());
  check.add(L"md5sum.exe", {L"--strict", L"--warn", L"-c", L"check.md5"});
  auto result = check.run();

  EXPECT_NE(result.exit_code, 0);
  EXPECT_TRUE(result.stdout_text.find("check.txt: OK") != std::string::npos);
  EXPECT_TRUE(
      result.stderr_text.find(
          "md5sum: check.md5: 2: improperly formatted MD5 checksum line") !=
      std::string::npos);
}

TEST(md5sum, md5sum_check_reports_unreadable_listed_files) {
  TempDir tmp;
  tmp.write("check.md5", "00000000000000000000000000000000  missing.txt\n");

  Pipeline check;
  check.set_cwd(tmp.wpath());
  check.add(L"md5sum.exe", {L"-c", L"check.md5"});
  auto result = check.run();

  EXPECT_NE(result.exit_code, 0);
  EXPECT_TRUE(result.stdout_text.find("missing.txt: FAILED open or read") !=
              std::string::npos);
  EXPECT_TRUE(result.stderr_text.find(
                  "md5sum: missing.txt: No such file or directory") !=
              std::string::npos);
  EXPECT_TRUE(result.stderr_text.find(
                  "md5sum: WARNING: 1 listed file could not be read") !=
              std::string::npos);
}

TEST(md5sum, md5sum_check_ignore_missing_skips_missing_files) {
  TempDir tmp;
  tmp.write("check.md5", "00000000000000000000000000000000  missing.txt\n");

  Pipeline check;
  check.set_cwd(tmp.wpath());
  check.add(L"md5sum.exe", {L"--ignore-missing", L"-c", L"check.md5"});
  auto result = check.run();

  EXPECT_NE(result.exit_code, 0);
  EXPECT_EQ_TEXT(result.stdout_text, "");
  EXPECT_TRUE(result.stderr_text.find("no file was verified") !=
              std::string::npos);
}

// [#1195] stdin must be read in binary mode: the CRT's default text mode
// silently stops at the first 0x1A (Ctrl-Z) byte, so the digest was computed
// over a truncated stream ("AAAA" instead of "AAAA<0x1A>BBBB\n").  GNU hashes
// the full 10-byte payload for the pipe, the `<` redirect, and the file
// operand alike.
TEST(md5sum, md5sum_stdin_ctrl_z_pipe_hashes_full_payload_issue1195) {
  Pipeline p;
  // "AAAA\032BBBB\n" — a 0x1A byte in the middle of the payload.
  p.set_stdin(
      std::string("AAAA\x1a"
                  "BBBB\n",
                  10));
  p.add(L"md5sum.exe", {});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("md5sum ctrl-z pipe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("60f5ffbcfe8a049be21dc9d6c334c82c") !=
              std::string::npos);  // GNU md5 of the full 10-byte payload
  // The pre-fix truncated value (md5("AAAA")) must not appear.
  EXPECT_TRUE(r.stdout_text.find("098890dde069e9abad63f19a0d9e1f32") ==
              std::string::npos);
}

TEST(md5sum, md5sum_stdin_nul_pipe_unchanged_issue1195) {
  Pipeline p;
  // "AB\0CD" — NUL bytes were handled correctly before #1195 and must stay.
  p.set_stdin(
      std::string("AB"
                  "\x00"
                  "CD",
                  5));
  p.add(L"md5sum.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("580dfde355cb3ceb91f01544d9918f9c") !=
              std::string::npos);  // GNU md5 of "AB\0CD"
}

TEST(md5sum, md5sum_pipe_matches_file_operand_issue1195) {
  // The file operand form must stay byte-correct (it never reproduced the
  // bug), and the stdin form must now agree with it on identical bytes.
  TempDir tmp;
  tmp.write("payload.bin", std::string("AAAA\x1a"
                                       "BBBB\n",
                                       10));

  Pipeline file;
  file.set_cwd(tmp.wpath());
  file.add(L"md5sum.exe", {L"payload.bin"});
  auto file_result = file.run();

  EXPECT_EQ(file_result.exit_code, 0);
  EXPECT_TRUE(
      file_result.stdout_text.find("60f5ffbcfe8a049be21dc9d6c334c82c") !=
      std::string::npos);  // GNU md5 of the full 10-byte payload

  Pipeline piped;
  piped.set_stdin(
      std::string("AAAA\x1a"
                  "BBBB\n",
                  10));
  piped.add(L"md5sum.exe", {});
  auto pipe_result = piped.run();

  EXPECT_EQ(pipe_result.exit_code, 0);
  // Same 32-hex digest must be printed for both input forms.
  EXPECT_EQ(file_result.stdout_text.substr(0, 32),
            pipe_result.stdout_text.substr(0, 32));
}

TEST(md5sum, md5sum_stdin_plain_text_sanity_issue1195) {
  // A legitimately plain-text stdin pipe must produce the bytes of "hi\n".
  Pipeline p;
  p.set_stdin("hi\n");
  p.add(L"md5sum.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("764efa883dda1e11db47671c4a3bbd9e") !=
              std::string::npos);  // GNU md5 of "hi\n"
}

// [#1205] the default output marker is a space (text); -b/--binary selects
// the '*' marker, and the last of -b/-t wins, matching GNU.
TEST(md5sum, md5sum_marker_text_default_and_last_option_wins_issue1205) {
  Pipeline def;
  def.set_stdin("hello\n");
  def.add(L"md5sum.exe", {});
  auto rd = def.run();
  EXPECT_EQ(rd.exit_code, 0);
  EXPECT_TRUE(rd.stdout_text.ends_with("  -\n"));

  Pipeline bin;
  bin.set_stdin("hello\n");
  bin.add(L"md5sum.exe", {L"-b"});
  auto rb = bin.run();
  EXPECT_EQ(rb.exit_code, 0);
  EXPECT_TRUE(rb.stdout_text.ends_with(" *-\n"));

  Pipeline last_bin;
  last_bin.set_stdin("hello\n");
  last_bin.add(L"md5sum.exe", {L"-t", L"-b"});
  auto rlb = last_bin.run();
  EXPECT_EQ(rlb.stdout_text, rb.stdout_text);

  Pipeline last_text;
  last_text.set_stdin("hello\n");
  last_text.add(L"md5sum.exe", {L"-b", L"-t"});
  auto rlt = last_text.run();
  EXPECT_EQ(rlt.stdout_text, rd.stdout_text);
}
