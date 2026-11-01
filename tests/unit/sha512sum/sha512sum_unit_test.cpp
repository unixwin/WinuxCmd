/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(sha512sum, sha512sum_basic) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha512sum.exe", {L"test.txt"});

  TEST_LOG_CMD_LIST("sha512sum.exe", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("sha512sum output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(sha512sum, sha512sum_missing_input_reports_no_such_file) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha512sum.exe", {L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("sha512sum: missing.txt: No such "
                                 "file or directory") != std::string::npos);
}

TEST(sha512sum, sha512sum_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("indir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha512sum.exe", {L"indir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("sha512sum: indir: Is a directory") !=
              std::string::npos);
}

TEST(sha512sum, sha512sum_stdin) {
  Pipeline p;
  p.set_stdin("hello world\n");
  p.add(L"sha512sum.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(sha512sum, sha512sum_binary) {
  Pipeline p;
  p.set_stdin("hello");
  p.add(L"sha512sum.exe", {L"-b"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(sha512sum, sha512sum_text) {
  Pipeline p;
  p.set_stdin("hello");
  p.add(L"sha512sum.exe", {L"-t"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(sha512sum, sha512sum_tag) {
  Pipeline p;
  p.set_stdin("hello");
  p.add(L"sha512sum.exe", {L"--tag"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("SHA512") != std::string::npos);
}

TEST(sha512sum, sha512sum_quiet) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha512sum.exe", {L"--quiet", L"test.txt"});
  auto r = p.run();

  // [GNU] --quiet is rejected outside check mode.
  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("meaningful only when verifying checksums") !=
              std::string::npos);
}

TEST(sha512sum, sha512sum_check) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  // Get hash
  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"sha512sum.exe", {L"test.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  // Create check file
  std::string hash_line = r1.stdout_text.substr(0, 128) + "  test.txt";
  tmp.write("check.sha512", hash_line);

  // Verify
  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"sha512sum.exe", {L"-c", L"check.sha512"});
  auto r2 = p2.run();

  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_TRUE(r2.stdout_text.find("test.txt: OK") != std::string::npos);
}

TEST(sha512sum, sha512sum_check_invalid) {
  TempDir tmp;
  tmp.write("test.txt", "hello");
  tmp.write("check.sha512",
            "000000000000000000000000000000000000000000000000000000000000000000"
            "00000000000000000000000000000000000000000000000000000000000000  "
            "test.txt");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha512sum.exe", {L"-c", L"check.sha512"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("test.txt: FAILED") != std::string::npos);
}

TEST(sha512sum, sha512sum_check_status_suppresses_output) {
  TempDir tmp;
  tmp.write("check.txt", "hello\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"sha512sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 128) + "  check.txt";
  tmp.write("check.sha512", hash_line);

  Pipeline good;
  good.set_cwd(tmp.wpath());
  good.add(L"sha512sum.exe", {L"--status", L"-c", L"check.sha512"});
  auto good_result = good.run();

  EXPECT_EQ(good_result.exit_code, 0);
  EXPECT_EQ_TEXT(good_result.stdout_text, "");
  EXPECT_EQ_TEXT(good_result.stderr_text, "");

  tmp.write("check.sha512",
            "000000000000000000000000000000000000000000000000000000000000000000"
            "00000000000000000000000000000000000000000000000000000000000000  "
            "check.txt");

  Pipeline bad;
  bad.set_cwd(tmp.wpath());
  bad.add(L"sha512sum.exe", {L"--status", L"-c", L"check.sha512"});
  auto bad_result = bad.run();

  EXPECT_NE(bad_result.exit_code, 0);
  EXPECT_EQ_TEXT(bad_result.stdout_text, "");
  EXPECT_EQ_TEXT(bad_result.stderr_text, "");
}

TEST(sha512sum, sha512sum_check_quiet_suppresses_ok_lines_only) {
  TempDir tmp;
  tmp.write("check.txt", "hello\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"sha512sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 128) + "  check.txt";
  tmp.write("check.sha512", hash_line);

  Pipeline good;
  good.set_cwd(tmp.wpath());
  good.add(L"sha512sum.exe", {L"--quiet", L"-c", L"check.sha512"});
  auto good_result = good.run();

  EXPECT_EQ(good_result.exit_code, 0);
  EXPECT_EQ_TEXT(good_result.stdout_text, "");
  EXPECT_EQ_TEXT(good_result.stderr_text, "");

  tmp.write("check.sha512",
            "000000000000000000000000000000000000000000000000000000000000000000"
            "00000000000000000000000000000000000000000000000000000000000000  "
            "check.txt");

  Pipeline bad;
  bad.set_cwd(tmp.wpath());
  bad.add(L"sha512sum.exe", {L"--quiet", L"-c", L"check.sha512"});
  auto bad_result = bad.run();

  EXPECT_NE(bad_result.exit_code, 0);
  EXPECT_TRUE(bad_result.stdout_text.find("check.txt: FAILED") !=
              std::string::npos);
  // [GNU] --quiet suppresses "OK" lines only; the mismatch WARNING summary
  // still goes to stderr (verified against GNU coreutils 9.4).
  EXPECT_TRUE(bad_result.stderr_text.find(
                  "sha512sum: WARNING: 1 computed checksum did NOT match") !=
              std::string::npos);
}

TEST(sha512sum, sha512sum_check_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("checkdir");

  Pipeline check;
  check.set_cwd(tmp.wpath());
  check.add(L"sha512sum.exe", {L"-c", L"checkdir"});
  auto result = check.run();

  EXPECT_EQ(result.exit_code, 1);
  EXPECT_TRUE(result.stderr_text.find("sha512sum: checkdir: read error") !=
              std::string::npos);
}

TEST(sha512sum, sha512sum_check_reports_unreadable_listed_files) {
  TempDir tmp;
  tmp.write("check.sha512",
            "000000000000000000000000000000000000000000000000000000000000000000"
            "00000000000000000000000000000000000000000000000000000000000000  "
            "missing.txt\n");

  Pipeline check;
  check.set_cwd(tmp.wpath());
  check.add(L"sha512sum.exe", {L"-c", L"check.sha512"});
  auto result = check.run();

  EXPECT_NE(result.exit_code, 0);
  EXPECT_TRUE(result.stdout_text.find("missing.txt: FAILED open or read") !=
              std::string::npos);
  EXPECT_TRUE(result.stderr_text.find(
                  "sha512sum: missing.txt: No such file or directory") !=
              std::string::npos);
  EXPECT_TRUE(result.stderr_text.find(
                  "sha512sum: WARNING: 1 listed file could not be read") !=
              std::string::npos);
}

TEST(sha512sum, sha512sum_check_ignore_missing_skips_missing_files) {
  TempDir tmp;
  tmp.write("check.sha512",
            "000000000000000000000000000000000000000000000000000000000000000000"
            "00000000000000000000000000000000000000000000000000000000000000  "
            "missing.txt\n");

  Pipeline check;
  check.set_cwd(tmp.wpath());
  check.add(L"sha512sum.exe", {L"--ignore-missing", L"-c", L"check.sha512"});
  auto result = check.run();

  EXPECT_NE(result.exit_code, 0);
  EXPECT_EQ_TEXT(result.stdout_text, "");
  EXPECT_TRUE(result.stderr_text.find("no file was verified") !=
              std::string::npos);
}

TEST(sha512sum, sha512sum_check_accepts_binary_marker_lines) {
  TempDir tmp;
  tmp.write("check.txt", "hello\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"sha512sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 128) + " *check.txt";
  tmp.write("check.sha512", hash_line);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha512sum.exe", {L"-c", L"check.sha512"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("check.txt: OK") != std::string::npos);
}

TEST(sha512sum, sha512sum_check_warn_reports_malformed_line_locations) {
  TempDir tmp;
  tmp.write("check.txt", "hello\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"sha512sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 128) + "  check.txt";
  tmp.write("check.sha512", "not-a-checksum\n" + hash_line + "\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha512sum.exe", {L"--warn", L"-c", L"check.sha512"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("sha512sum: check.sha512: 1: improperly "
                                 "formatted SHA512 checksum line") !=
              std::string::npos);
  EXPECT_TRUE(r.stderr_text.find(
                  "sha512sum: WARNING: 1 line is improperly formatted") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("check.txt: OK") != std::string::npos);
}

TEST(sha512sum, sha512sum_check_without_valid_lines_reports_error) {
  TempDir tmp;
  tmp.write("check.sha512", "not-a-checksum\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha512sum.exe", {L"--warn", L"-c", L"check.sha512"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "");
  EXPECT_TRUE(r.stderr_text.find("sha512sum: check.sha512: no properly "
                                 "formatted checksum lines found") !=
              std::string::npos);
}

TEST(sha512sum, sha512sum_check_strict_rejects_malformed_lines) {
  TempDir tmp;
  tmp.write("check.txt", "hello\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"sha512sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 128) + "  check.txt";
  tmp.write("check.sha512", hash_line + "\nbad-line\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha512sum.exe", {L"--strict", L"-c", L"check.sha512"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("check.txt: OK") != std::string::npos);
}

TEST(sha512sum, sha512sum_wildcard) {
  TempDir tmp;
  tmp.write("a.txt", "hello");
  tmp.write("b.txt", "world");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha512sum.exe", {L"*.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("a.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b.txt") != std::string::npos);
}

TEST(sha512sum, sha512sum_short_zero_alias_uses_nul_terminator) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha512sum.exe", {L"-z", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text,
            std::string("9b71d224bd62f3785d96d46ad3ea3d73319bfbc2890caadae2"
                        "dff72519673ca72323c3d99ba5c11d7c7acc6e14b8c5da0c46"
                        "63475c2e5c3adef46f73bcdec043  test.txt\0",
                        139));
}

// [#1200] stdin must be read in binary mode: the CRT's default text mode
// silently stops at the first 0x1A (Ctrl-Z) byte — sha512sum has its own
// CryptoAPI fread(stdin) path, not shared with hash_file_hex.  GNU hashes
// the full 10-byte payload for the pipe, the `<` redirect, and the file
// operand alike.
TEST(sha512sum, sha512sum_stdin_ctrl_z_pipe_hashes_full_payload_issue1200) {
  Pipeline p;
  // "AAAA\032BBBB\n" — a 0x1A byte in the middle of the payload.
  p.set_stdin(
      std::string("AAAA\x1a"
                  "BBBB\n",
                  10));
  p.add(L"sha512sum.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(
      r.stdout_text.find(
          "16d6cea306afa32e67877b65e6d005f9f238ae85f15359165697b75d8d682358e888"
          "acec532856591ce894f4079d625bc3a981070b5f4ae879254b253a6febab") !=
      std::string::npos);  // GNU sha512 of the full 10-byte payload
  // The pre-fix truncated value (sha512("AAAA")) must not appear.
  EXPECT_TRUE(
      r.stdout_text.find(
          "53b74be8b295b733fdfafbd7d2a22b1686733740de7fdc592b26cf3e1874cfce1581"
          "70ce9230e24696331a61829244e5d9f48abdacc9ffa8c4cb498724844cf8") ==
      std::string::npos);
}

TEST(sha512sum, sha512sum_stdin_nul_pipe_unchanged_issue1200) {
  Pipeline p;
  // "AB\0CD" — NUL bytes were handled correctly before #1200 and must stay.
  p.set_stdin(
      std::string("AB"
                  "\x00"
                  "CD",
                  5));
  p.add(L"sha512sum.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(
      r.stdout_text.find(
          "8a53305a945338d8c1cba8184a805870a2c8a296d783cc5023d0c196800f6fefc2d8"
          "1c4e2e6254ab9af4f1e0a7232cae45a41cab1ebef1d8c3466827076daaa6") !=
      std::string::npos);  // GNU sha512 of "AB\0CD"
}

TEST(sha512sum, sha512sum_pipe_matches_file_operand_issue1200) {
  TempDir tmp;
  tmp.write("payload.bin", std::string("AAAA\x1a"
                                       "BBBB\n",
                                       10));

  Pipeline file;
  file.set_cwd(tmp.wpath());
  file.add(L"sha512sum.exe", {L"payload.bin"});
  auto file_result = file.run();

  EXPECT_EQ(file_result.exit_code, 0);
  EXPECT_TRUE(
      file_result.stdout_text.find(
          "16d6cea306afa32e67877b65e6d005f9f238ae85f15359165697b75d8d682358e888"
          "acec532856591ce894f4079d625bc3a981070b5f4ae879254b253a6febab") !=
      std::string::npos);  // GNU sha512 of the full 10-byte payload

  Pipeline piped;
  piped.set_stdin(
      std::string("AAAA\x1a"
                  "BBBB\n",
                  10));
  piped.add(L"sha512sum.exe", {});
  auto pipe_result = piped.run();

  EXPECT_EQ(pipe_result.exit_code, 0);
  // Same 128-hex digest must be printed for both input forms.
  EXPECT_EQ(file_result.stdout_text.substr(0, 128),
            pipe_result.stdout_text.substr(0, 128));
}

TEST(sha512sum, sha512sum_stdin_plain_text_sanity_issue1200) {
  // A legitimately plain-text stdin pipe must produce the bytes of "hi\n".
  Pipeline p;
  p.set_stdin("hi\n");
  p.add(L"sha512sum.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(
      r.stdout_text.find(
          "d78abb0542736865f94704521609c230dac03a2f369d043ac212d6933b91410e0639"
          "9e37f9c5cc88436a31737330c1c8eccb2c2f9f374d62f716432a32d50fac") !=
      std::string::npos);  // GNU sha512 of "hi\n"
}

// #1127-P4: the engine moved from CryptoAPI to CNG (BCrypt) with a
// 64 KiB feed (was 8 KiB). Digests must match the published vectors on
// inputs that cross several feed blocks: the FIPS 1,000,000 x 'a'
// example and a 200,003-byte mixed pattern (not a block multiple), on
// the file and stdin routes alike.
TEST(sha512sum, sha512sum_cng_large_inputs_issue1127) {
  TempDir tmp;
  const std::string million_a(1000000, 'a');
  tmp.write("a.bin", million_a);
  std::string mixed;
  mixed.reserve(200003);
  for (int i = 0; i < 200003; ++i) {
    mixed.push_back(static_cast<char>((i * 37 + 11) & 0xFF));
  }
  tmp.write("mixed.bin", mixed);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha512sum.exe", {L"a.bin", L"mixed.bin"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(
      r.stdout_text,
      "e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973ebde0f"
      "f244877ea60a4cb0432ce577c31beb009c5c2c49aa2e4eadb217ad8cc09b"
      "  a.bin\n"
      "b1a2d294bb371d56f24fa004e76515c9185fe308523caa8a07126b43a6b6913bbc029"
      "077d40263342e05eacf2b11eb576fa0acf45ba2f904a2efb0acbd783a85"
      "  mixed.bin\n");

  Pipeline p2;
  p2.set_stdin(million_a);
  p2.add(L"sha512sum.exe", {});
  auto r2 = p2.run();
  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_EQ_TEXT(r2.stdout_text,
                 "e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973e"
                 "bde0ff244877ea60a4cb0432ce577c31beb009c5c2c49aa2e4eadb217ad8c"
                 "c09b  -\n");
}
