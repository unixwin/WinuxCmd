/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(sum, sum_basic) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sum.exe", {L"test.txt"});

  TEST_LOG_CMD_LIST("sum.exe", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("sum output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "08403     1 test.txt\n");
}

TEST(sum, sum_sysv_option_uses_byte_sum_and_512_byte_blocks) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sum.exe", {L"-s", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "532 1 test.txt\n");
}

TEST(sum, sum_last_algorithm_option_wins) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sum.exe", {L"-s", L"-r", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "08403     1 test.txt\n");
}

TEST(sum, sum_block_count_depends_on_algorithm) {
  TempDir tmp;
  tmp.write("large.txt", std::string(1025, 'a'));

  Pipeline bsd;
  bsd.set_cwd(tmp.wpath());
  bsd.add(L"sum.exe", {L"large.txt"});
  auto bsd_result = bsd.run();

  Pipeline sysv;
  sysv.set_cwd(tmp.wpath());
  sysv.add(L"sum.exe", {L"--sysv", L"large.txt"});
  auto sysv_result = sysv.run();

  EXPECT_EQ(bsd_result.exit_code, 0);
  EXPECT_EQ(sysv_result.exit_code, 0);
  EXPECT_TRUE(bsd_result.stdout_text.find(" 2 large.txt") != std::string::npos);
  EXPECT_TRUE(sysv_result.stdout_text.find(" 3 large.txt") !=
              std::string::npos);
}

TEST(sum, sum_stdin_has_no_filename) {
  Pipeline p;
  p.set_stdin("hello");
  p.add(L"sum.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "08403     1\n");
}

TEST(sum, sum_explicit_dash_prints_dash_filename) {
  Pipeline p;
  p.set_stdin("hello");
  p.add(L"sum.exe", {L"-"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "08403     1 -\n");
}

TEST(sum, sum_missing_input_reports_no_such_file) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sum.exe", {L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(
      r.stderr_text.find("sum: missing.txt: No such file or directory") !=
      std::string::npos);
}

TEST(sum, sum_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("indir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sum.exe", {L"indir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("sum: indir: Is a directory") !=
              std::string::npos);
}

TEST(sum, sum_continues_past_unreadable_files_and_returns_nonzero) {
  TempDir tmp;
  tmp.write("ok.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sum.exe", {L"missing.txt", L"ok.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(
      r.stderr_text.find("sum: missing.txt: No such file or directory") !=
      std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("08403     1 ok.txt\n") != std::string::npos);
}

// [#1200] calculate_checksum reads fd 0 through the CRT's default text
// mode, where std::istream reads silently stop at the first 0x1A (Ctrl-Z)
// byte, silently corrupting the checksum (the BSD rot-word and the SysV
// byte sum) and the byte count.  GNU sums raw bytes.
TEST(sum, sum_stdin_ctrl_z_pipe_hashes_full_payload_issue1200) {
  Pipeline p;
  // "AAAA\032BBBB\n" — a 0x1A byte in the middle of the payload.
  p.set_stdin(
      std::string("AAAA\x1a"
                  "BBBB\n",
                  10));
  p.add(L"sum.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // [GNU] BSD checksum of the full 10-byte payload.
  EXPECT_TRUE(r.stdout_text.find("38858") != std::string::npos);
  // The pre-fix truncated value (BSD sum of "AAAA") must not appear.
  EXPECT_TRUE(r.stdout_text.find("57465") == std::string::npos);
}

TEST(sum, sum_stdin_sysv_ctrl_z_pipe_hashes_full_payload_issue1200) {
  // The SysV route shares calculate_checksum's same text-mode read.
  Pipeline p;
  p.set_stdin(
      std::string("AAAA\x1a"
                  "BBBB\n",
                  10));
  p.add(L"sum.exe", {L"-s"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // [GNU] SysV byte sum of the full 10-byte payload.
  EXPECT_TRUE(r.stdout_text.find("560") != std::string::npos);
  // The pre-fix truncated value (SysV sum of "AAAA") must not appear.
  EXPECT_TRUE(r.stdout_text.find("260") == std::string::npos);
}

TEST(sum, sum_stdin_nul_pipe_unchanged_issue1200) {
  // "AB\0CD" — NUL bytes were handled correctly before #1200 and must stay.
  Pipeline p;
  p.set_stdin(
      std::string("AB"
                  "\x00"
                  "CD",
                  5));
  p.add(L"sum.exe", {});
  auto bsd = p.run();

  EXPECT_EQ(bsd.exit_code, 0);
  // [GNU] BSD checksum of "AB\0CD".
  EXPECT_TRUE(bsd.stdout_text.find("53361") != std::string::npos);

  Pipeline sysv;
  sysv.set_stdin(
      std::string("AB"
                  "\x00"
                  "CD",
                  5));
  sysv.add(L"sum.exe", {L"-s"});
  auto sysv_result = sysv.run();

  EXPECT_EQ(sysv_result.exit_code, 0);
  // [GNU] SysV byte sum of "AB\0CD".
  EXPECT_TRUE(sysv_result.stdout_text.find("266") != std::string::npos);
}

TEST(sum, sum_pipe_matches_file_operand_issue1200) {
  // The file operand form must stay byte-correct (it never reproduced the
  // bug), and the stdin form must now agree with it on identical bytes.
  TempDir tmp;
  tmp.write("payload.bin", std::string("AAAA\x1a"
                                       "BBBB\n",
                                       10));

  Pipeline file;
  file.set_cwd(tmp.wpath());
  file.add(L"sum.exe", {L"payload.bin"});
  auto file_result = file.run();

  EXPECT_EQ(file_result.exit_code, 0);
  // [GNU] full payload checksum + 1 block (1024-byte BSD blocks).
  EXPECT_TRUE(file_result.stdout_text.find("38858     1") != std::string::npos);

  Pipeline piped;
  piped.set_stdin(
      std::string("AAAA\x1a"
                  "BBBB\n",
                  10));
  piped.add(L"sum.exe", {});
  auto pipe_result = piped.run();

  EXPECT_EQ(pipe_result.exit_code, 0);
  // Same checksum and block count for the stdin form as for the operand.
  EXPECT_TRUE(pipe_result.stdout_text.find("38858     1") != std::string::npos);
}

TEST(sum, sum_stdin_plain_text_sanity_issue1200) {
  // A legitimately plain-text stdin pipe must produce the bytes of "hi\n".
  Pipeline p;
  p.set_stdin("hi\n");
  p.add(L"sum.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // [GNU] BSD checksum of "hi\n".
  EXPECT_TRUE(r.stdout_text.find("32856") != std::string::npos);
}

// #1370: algorithm selection must come from the parsed options (last of
// -r/-s wins, clustered or separate); a raw-argv rescan ignored cluster
// order and reinterpreted operands after `--` as options. Reference
// values from GNU coreutils 9.4: file "f" = "x\n" -> BSD 00070,
// SysV 130; file "-s" = "q\n" -> BSD 32834.
TEST(sum, sum_clustered_flags_last_wins_issue1370) {
  TempDir tmp;
  tmp.write("f", "x\n");

  Pipeline sysv;
  sysv.set_cwd(tmp.wpath());
  sysv.add(L"sum.exe", {L"-rs", L"f"});
  auto rs = sysv.run();
  EXPECT_EQ(rs.exit_code, 0);
  EXPECT_EQ_TEXT(rs.stdout_text, "130 1 f\n");

  Pipeline bsd;
  bsd.set_cwd(tmp.wpath());
  bsd.add(L"sum.exe", {L"-sr", L"f"});
  auto sr = bsd.run();
  EXPECT_EQ(sr.exit_code, 0);
  EXPECT_EQ_TEXT(sr.stdout_text, "00070     1 f\n");
}

TEST(sum, sum_operand_after_double_dash_is_not_an_option_issue1370) {
  TempDir tmp;
  tmp.write("-s", "q\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sum.exe", {L"--", L"-s"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "32834     1 -s\n");
}

TEST(sum, sum_long_options_last_wins_issue1370) {
  TempDir tmp;
  tmp.write("f", "x\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sum.exe", {L"--sysv", L"--bsd", L"f"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "00070     1 f\n");
}
