/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(b2sum, b2sum_basic) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"b2sum.exe", {L"test.txt"});

  TEST_LOG_CMD_LIST("b2sum.exe", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("b2sum output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(b2sum, b2sum_missing_input_reports_no_such_file) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"b2sum.exe", {L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("b2sum: missing.txt: No such "
                                 "file or directory") != std::string::npos);
}

TEST(b2sum, b2sum_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("indir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"b2sum.exe", {L"indir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("b2sum: indir: Is a directory") !=
              std::string::npos);
}

TEST(b2sum, b2sum_stdin) {
  Pipeline p;
  p.set_stdin("hello world\n");
  p.add(L"b2sum.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(b2sum, b2sum_binary) {
  Pipeline p;
  p.set_stdin("hello");
  p.add(L"b2sum.exe", {L"-b"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(b2sum, b2sum_text) {
  Pipeline p;
  p.set_stdin("hello");
  p.add(L"b2sum.exe", {L"-t"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(b2sum, b2sum_tag) {
  Pipeline p;
  p.set_stdin("hello");
  p.add(L"b2sum.exe", {L"--tag"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("BLAKE2") != std::string::npos);
}

TEST(b2sum, b2sum_short_zero_alias_uses_nul_terminator) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"b2sum.exe", {L"-z", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text,
            std::string("e4cfa39a3d37be31c59609e807970799caa68a19bfaa15135f"
                        "165085e01d41a65ba1e1b146aeb6bd0092b49eac214c103ccfa3"
                        "a365954bbbe52f74a2b3620c94  test.txt\0",
                        139));
}

TEST(b2sum, b2sum_quiet) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"b2sum.exe", {L"--quiet", L"test.txt"});
  auto r = p.run();

  // [GNU] --quiet is rejected outside check mode.
  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("meaningful only when verifying checksums") !=
              std::string::npos);
}

TEST(b2sum, b2sum_length) {
  Pipeline p;
  p.set_stdin("hello");
  p.add(L"b2sum.exe", {L"-l", L"128"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.starts_with("46fb7408d4f285228f4af516ea25851b"));
  EXPECT_TRUE(r.stdout_text.find("  -\n") != std::string::npos);
}

TEST(b2sum, b2sum_length_accepts_nonstandard_multiple_of_8) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"b2sum.exe", {L"-l", L"136", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.starts_with("2300d4d2a22feac80ac0bdc302611b69ba"));
  EXPECT_EQ(r.stdout_text.size(), 34 + 2 + 8 + 1);
}

TEST(b2sum, b2sum_length_rejects_values_above_512_bits) {
  Pipeline p;
  p.set_stdin("hello");
  p.add(L"b2sum.exe", {L"-l", L"520"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(
      r.stderr_text.find("digest length must be a positive multiple of 8") !=
      std::string::npos);
}

TEST(b2sum, b2sum_check) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  // Get hash
  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"b2sum.exe", {L"test.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  // Create check file
  std::string hash_line = r1.stdout_text.substr(0, 128) + "  test.txt";
  tmp.write("check.b2", hash_line);

  // Verify
  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"b2sum.exe", {L"-c", L"check.b2"});
  auto r2 = p2.run();

  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_TRUE(r2.stdout_text.find("test.txt: OK") != std::string::npos);
}

TEST(b2sum, b2sum_check_invalid) {
  TempDir tmp;
  tmp.write("test.txt", "hello");
  tmp.write("check.b2",
            "0000000000000000000000000000000000000000000000000000000000000000"
            "0000000000000000000000000000000000000000000000000000000000000000"
            "  test.txt");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"b2sum.exe", {L"-c", L"check.b2"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("test.txt: FAILED") != std::string::npos);
}

TEST(b2sum, b2sum_check_status_suppresses_output) {
  TempDir tmp;
  tmp.write("check.txt", "hello\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"b2sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 128) + "  check.txt";
  tmp.write("check.b2", hash_line);

  Pipeline good;
  good.set_cwd(tmp.wpath());
  good.add(L"b2sum.exe", {L"--status", L"-c", L"check.b2"});
  auto good_result = good.run();

  EXPECT_EQ(good_result.exit_code, 0);
  EXPECT_EQ_TEXT(good_result.stdout_text, "");
  EXPECT_EQ_TEXT(good_result.stderr_text, "");

  tmp.write("check.b2",
            "0000000000000000000000000000000000000000000000000000000000000000"
            "0000000000000000000000000000000000000000000000000000000000000000"
            "  check.txt");

  Pipeline bad;
  bad.set_cwd(tmp.wpath());
  bad.add(L"b2sum.exe", {L"--status", L"-c", L"check.b2"});
  auto bad_result = bad.run();

  EXPECT_NE(bad_result.exit_code, 0);
  EXPECT_EQ_TEXT(bad_result.stdout_text, "");
  EXPECT_EQ_TEXT(bad_result.stderr_text, "");
}

TEST(b2sum, b2sum_check_quiet_suppresses_ok_lines_only) {
  TempDir tmp;
  tmp.write("check.txt", "hello\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"b2sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 128) + "  check.txt";
  tmp.write("check.b2", hash_line);

  Pipeline good;
  good.set_cwd(tmp.wpath());
  good.add(L"b2sum.exe", {L"--quiet", L"-c", L"check.b2"});
  auto good_result = good.run();

  EXPECT_EQ(good_result.exit_code, 0);
  EXPECT_EQ_TEXT(good_result.stdout_text, "");
  EXPECT_EQ_TEXT(good_result.stderr_text, "");

  tmp.write("check.b2",
            "0000000000000000000000000000000000000000000000000000000000000000"
            "0000000000000000000000000000000000000000000000000000000000000000"
            "  check.txt");

  Pipeline bad;
  bad.set_cwd(tmp.wpath());
  bad.add(L"b2sum.exe", {L"--quiet", L"-c", L"check.b2"});
  auto bad_result = bad.run();

  EXPECT_NE(bad_result.exit_code, 0);
  EXPECT_TRUE(bad_result.stdout_text.find("check.txt: FAILED") !=
              std::string::npos);
  // [GNU] --quiet suppresses "OK" lines only; the mismatch WARNING summary
  // still goes to stderr (verified against GNU coreutils 9.4).
  EXPECT_TRUE(bad_result.stderr_text.find(
                  "b2sum: WARNING: 1 computed checksum did NOT match") !=
              std::string::npos);
}

TEST(b2sum, b2sum_check_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("checkdir");

  Pipeline check;
  check.set_cwd(tmp.wpath());
  check.add(L"b2sum.exe", {L"-c", L"checkdir"});
  auto result = check.run();

  EXPECT_EQ(result.exit_code, 1);
  EXPECT_TRUE(result.stderr_text.find("b2sum: checkdir: read error") !=
              std::string::npos);
}

TEST(b2sum, b2sum_check_reports_unreadable_listed_files) {
  TempDir tmp;
  tmp.write("check.b2",
            "0000000000000000000000000000000000000000000000000000000000000000"
            "0000000000000000000000000000000000000000000000000000000000000000"
            "  missing.txt\n");

  Pipeline check;
  check.set_cwd(tmp.wpath());
  check.add(L"b2sum.exe", {L"-c", L"check.b2"});
  auto result = check.run();

  EXPECT_NE(result.exit_code, 0);
  EXPECT_TRUE(result.stdout_text.find("missing.txt: FAILED open or read") !=
              std::string::npos);
  EXPECT_TRUE(result.stderr_text.find(
                  "b2sum: missing.txt: No such file or directory") !=
              std::string::npos);
  EXPECT_TRUE(result.stderr_text.find(
                  "b2sum: WARNING: 1 listed file could not be read") !=
              std::string::npos);
}

TEST(b2sum, b2sum_check_ignore_missing_skips_missing_files) {
  TempDir tmp;
  tmp.write("check.b2",
            "000000000000000000000000000000000000000000000000000000000000000000"
            "00000000000000000000000000000000000000000000000000000000000000  "
            "missing.txt\n");

  Pipeline check;
  check.set_cwd(tmp.wpath());
  check.add(L"b2sum.exe", {L"--ignore-missing", L"-c", L"check.b2"});
  auto result = check.run();

  EXPECT_NE(result.exit_code, 0);
  EXPECT_EQ_TEXT(result.stdout_text, "");
  EXPECT_TRUE(result.stderr_text.find("no file was verified") !=
              std::string::npos);
}

TEST(b2sum, b2sum_check_accepts_binary_marker_lines) {
  TempDir tmp;
  tmp.write("test.txt", "hello");
  tmp.write("check.b2",
            "e4cfa39a3d37be31c59609e807970799caa68a19bfaa15135f165085e01d41a"
            "65ba1e1b146aeb6bd0092b49eac214c103ccfa3a365954bbbe52f74a2b3620c94"
            " *test.txt");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"b2sum.exe", {L"-c", L"check.b2"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("test.txt: OK") != std::string::npos);
}

TEST(b2sum, b2sum_check_warn_reports_malformed_line_locations) {
  TempDir tmp;
  tmp.write("test.txt", "hello");
  tmp.write("check.b2",
            "not-a-checksum\n"
            "e4cfa39a3d37be31c59609e807970799caa68a19bfaa15135f165085e01d41a"
            "65ba1e1b146aeb6bd0092b49eac214c103ccfa3a365954bbbe52f74a2b3620c94"
            "  test.txt\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"b2sum.exe", {L"--warn", L"-c", L"check.b2"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find(
                  "check.b2: 1: improperly formatted BLAKE2b checksum line") !=
              std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("WARNING: 1 line is improperly formatted") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("test.txt: OK") != std::string::npos);
}

TEST(b2sum, b2sum_check_without_valid_lines_reports_error) {
  TempDir tmp;
  tmp.write("check.b2", "not-a-checksum\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"b2sum.exe", {L"--warn", L"-c", L"check.b2"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "");
  EXPECT_TRUE(r.stderr_text.find(
                  "b2sum: check.b2: no properly formatted checksum lines "
                  "found") != std::string::npos);
}

TEST(b2sum, b2sum_check_strict_rejects_malformed_lines) {
  TempDir tmp;
  tmp.write("test.txt", "hello");
  tmp.write("check.b2",
            "e4cfa39a3d37be31c59609e807970799caa68a19bfaa15135f165085e01d41a"
            "65ba1e1b146aeb6bd0092b49eac214c103ccfa3a365954bbbe52f74a2b3620c94"
            "  test.txt\n"
            "bad-line\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"b2sum.exe", {L"--strict", L"-c", L"check.b2"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("test.txt: OK") != std::string::npos);
}

TEST(b2sum, b2sum_wildcard) {
  TempDir tmp;
  tmp.write("a.txt", "hello");
  tmp.write("b.txt", "world");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"b2sum.exe", {L"*.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("a.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b.txt") != std::string::npos);
}

// [#1200] stdin must be read in binary mode: the CRT's default text mode
// silently stops at the first 0x1A (Ctrl-Z) byte — b2sum reads fd 0 through
// the shared helper portable_digest::hash_file_hex.  GNU hashes the full
// 10-byte payload for the pipe, the `<` redirect, and the file operand alike.
TEST(b2sum, b2sum_stdin_ctrl_z_pipe_hashes_full_payload_issue1200) {
  Pipeline p;
  // "AAAA\032BBBB\n" — a 0x1A byte in the middle of the payload.
  p.set_stdin(
      std::string("AAAA\x1a"
                  "BBBB\n",
                  10));
  p.add(L"b2sum.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(
      r.stdout_text.find(
          "79d9a3f0d2c8368f371b84ca8557412f5d4377f7c40d85be8f7c3829608c524086421"
          "12674ca24ce035013ba3408a9ce532ad94432bbff005e5f57a17424bf14") !=
      std::string::npos);  // GNU b2sum (BLAKE2b) of the full 10-byte payload
  // The pre-fix truncated value (b2sum("AAAA")) must not appear.
  EXPECT_TRUE(
      r.stdout_text.find(
          "5448fb61bfeba39eb14a49f72310ab14653b230655b0adccadcb31148602317af3bc"
          "ef80f09f0945f06607465a0e467ab8cf257a3088d25065aded9fc0963271") ==
      std::string::npos);
}

TEST(b2sum, b2sum_stdin_nul_pipe_unchanged_issue1200) {
  Pipeline p;
  // "AB\0CD" — NUL bytes were handled correctly before #1200 and must stay.
  p.set_stdin(
      std::string("AB"
                  "\x00"
                  "CD",
                  5));
  p.add(L"b2sum.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(
      r.stdout_text.find(
          "4ac7dc8c650655b995de91fcdf25afb14c64b2e5a7f64eb08c418469d3876b6cded5"
          "cdd000fa3052e8b84befe877f2ef75405bc25759df1539b8a385452e94e2") !=
      std::string::npos);  // GNU b2sum of "AB\0CD"
}

TEST(b2sum, b2sum_pipe_matches_file_operand_issue1200) {
  TempDir tmp;
  tmp.write("payload.bin", std::string("AAAA\x1a"
                                       "BBBB\n",
                                       10));

  Pipeline file;
  file.set_cwd(tmp.wpath());
  file.add(L"b2sum.exe", {L"payload.bin"});
  auto file_result = file.run();

  EXPECT_EQ(file_result.exit_code, 0);
  EXPECT_TRUE(
      file_result.stdout_text.find(
          "79d9a3f0d2c8368f371b84ca8557412f5d4377f7c40d85be8f7c3829608c524086421"
          "12674ca24ce035013ba3408a9ce532ad94432bbff005e5f57a17424bf14") !=
      std::string::npos);  // GNU b2sum of the full 10-byte payload

  Pipeline piped;
  piped.set_stdin(
      std::string("AAAA\x1a"
                  "BBBB\n",
                  10));
  piped.add(L"b2sum.exe", {});
  auto pipe_result = piped.run();

  EXPECT_EQ(pipe_result.exit_code, 0);
  // Same 128-hex digest must be printed for both input forms.
  EXPECT_EQ(file_result.stdout_text.substr(0, 128),
            pipe_result.stdout_text.substr(0, 128));
}

TEST(b2sum, b2sum_stdin_plain_text_sanity_issue1200) {
  // A legitimately plain-text stdin pipe must produce the bytes of "hi\n".
  Pipeline p;
  p.set_stdin("hi\n");
  p.add(L"b2sum.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(
      r.stdout_text.find(
          "7ea59e7a000ec003846b6607dfd5f9217b681dc1a81b0789b464c3995105d93083f7"
          "f0a86fca01a1bed27e9f9303ae58d01746e3b20443480bea56198e65bfc5") !=
      std::string::npos);  // GNU b2sum of "hi\n"
}

// [#1205] the default output marker is a space (text); -b/--binary selects
// the '*' marker, and the last of -b/-t wins, matching GNU.
TEST(b2sum, b2sum_marker_text_default_and_last_option_wins_issue1205) {
  Pipeline def;
  def.set_stdin("hello\n");
  def.add(L"b2sum.exe", {});
  auto rd = def.run();
  EXPECT_EQ(rd.exit_code, 0);
  EXPECT_TRUE(rd.stdout_text.ends_with("  -\n"));

  Pipeline bin;
  bin.set_stdin("hello\n");
  bin.add(L"b2sum.exe", {L"-b"});
  auto rb = bin.run();
  EXPECT_EQ(rb.exit_code, 0);
  EXPECT_TRUE(rb.stdout_text.ends_with(" *-\n"));

  Pipeline last_bin;
  last_bin.set_stdin("hello\n");
  last_bin.add(L"b2sum.exe", {L"-t", L"-b"});
  auto rlb = last_bin.run();
  EXPECT_EQ(rlb.stdout_text, rb.stdout_text);

  Pipeline last_text;
  last_text.set_stdin("hello\n");
  last_text.add(L"b2sum.exe", {L"-b", L"-t"});
  auto rlt = last_text.run();
  EXPECT_EQ(rlt.stdout_text, rd.stdout_text);
}
