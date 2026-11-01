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

  // [GNU] -l/--length is valid only with --algorithm=blake2b
  // (cksum.c: "--length is only supported with
  // --algorithm=blake2b"); the alias equivalence is exercised there.
  Pipeline short_form;
  short_form.set_cwd(tmp.wpath());
  short_form.add(L"cksum.exe", {L"-a", L"blake2b", L"-l", L"8", L"test.txt"});
  auto short_result = short_form.run();

  Pipeline long_form;
  long_form.set_cwd(tmp.wpath());
  long_form.add(L"cksum.exe",
                 {L"-a", L"blake2b", L"--length=8", L"test.txt"});
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
  // [GNU] The improperly-formatted summary is NOT gated on --warn:
  // digest.c:1300-1307 prints it whenever some line was properly
  // formatted and the mode is not --status.
  EXPECT_TRUE(
      r.stderr_text.find("cksum: WARNING: 1 line is improperly formatted") !=
      std::string::npos);
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
  // [GNU] The --warn per-line diagnostic names the digest type in
  // force (digest.c:1209): "... improperly formatted CRC checksum
  // line" for the default algorithm.
  EXPECT_TRUE(r.stderr_text.find("cksum: check.txt: 1: improperly formatted "
                                 "CRC checksum line") != std::string::npos);
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
  // [GNU] --strict changes only the exit status; the summary still
  // prints (digest.c:1300-1307 is not gated on --strict either).
  EXPECT_TRUE(
      r.stderr_text.find("cksum: WARNING: 1 line is improperly formatted") !=
      std::string::npos);
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

  // [GNU] An --ignore-missing run that verified nothing FAILS with
  // the "no file was verified" note (digest.c:1316-1319, 1330-1335).
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "");
  EXPECT_TRUE(r.stderr_text.find("cksum: check.txt: no file was verified") !=
              std::string::npos);
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
  // [GNU] digest.c:1320: "WARNING: %ju computed checksum did NOT
  // match" -- there is no "of N" form.
  EXPECT_TRUE(r.stderr_text.find(
                  "cksum: WARNING: 1 computed checksum did NOT match") !=
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
                  "cksum: WARNING: 1 computed checksum did NOT match") !=
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
                  "cksum: WARNING: 1 computed checksum did NOT match") !=
              std::string::npos);
}

// [#1195] every cksum stdin route (legacy read_file/read_crc_file and the
// digest path via portable_digest::hash_file_hex) must read fd 0 in binary
// mode: the CRT's default text mode silently stops at the first 0x1A
// (Ctrl-Z) byte, silently corrupting both the checksum and the byte count.
TEST(cksum, cksum_stdin_ctrl_z_pipe_hashes_full_payload_issue1195) {
  Pipeline p;
  // "AAAA\032BBBB\n" — a 0x1A byte in the middle of the payload.
  p.set_stdin(
      std::string("AAAA\x1a"
                  "BBBB\n",
                  10));
  p.add(L"cksum.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // [GNU] CRC of the full 10-byte payload plus the full byte count.
  EXPECT_TRUE(r.stdout_text.find("2873339409") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find(" 10") != std::string::npos);
  // The pre-fix truncated values (crc/length of "AAAA") must not appear.
  EXPECT_TRUE(r.stdout_text.find("1257162693") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find(" 4") == std::string::npos);
}

TEST(cksum, cksum_stdin_nul_pipe_unchanged_issue1195) {
  Pipeline p;
  // "AB\0CD" — NUL bytes were handled correctly before #1195 and must stay.
  p.set_stdin(
      std::string("AB"
                  "\x00"
                  "CD",
                  5));
  p.add(L"cksum.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("3345473462") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find(" 5") != std::string::npos);
}

TEST(cksum, cksum_pipe_matches_file_operand_issue1195) {
  TempDir tmp;
  tmp.write("payload.bin", std::string("AAAA\x1a"
                                       "BBBB\n",
                                       10));

  Pipeline file;
  file.set_cwd(tmp.wpath());
  file.add(L"cksum.exe", {L"payload.bin"});
  auto file_result = file.run();

  EXPECT_EQ(file_result.exit_code, 0);
  EXPECT_TRUE(file_result.stdout_text.find("2873339409 10") !=
              std::string::npos);  // [GNU] full payload CRC + length

  Pipeline piped;
  piped.set_stdin(
      std::string("AAAA\x1a"
                  "BBBB\n",
                  10));
  piped.add(L"cksum.exe", {});
  auto pipe_result = piped.run();

  EXPECT_EQ(pipe_result.exit_code, 0);
  // Same checksum and byte count for the stdin form as for the operand.
  EXPECT_TRUE(pipe_result.stdout_text.find("2873339409 10") !=
              std::string::npos);
}

TEST(cksum, cksum_stdin_digest_algorithm_ctrl_z_issue1195) {
  // The -a/--algorithm route goes through hash_file_hex's std::cin path,
  // also hit by the -c check mode for "-" targets.
  Pipeline p;
  p.set_stdin(
      std::string("AAAA\x1a"
                  "BBBB\n",
                  10));
  p.add(L"cksum.exe", {L"--algorithm", L"md5"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("60f5ffbcfe8a049be21dc9d6c334c82c") !=
              std::string::npos);  // GNU md5 of the full 10-byte payload
}

// [#1214] `--raw` writes the digest with fwrite(3) through a CRT stream that
// is still in text mode, so a 0x0A digest byte came out as 0D 0A and a
// four-byte CRC grew to five bytes. "x"*66 + LF has CRC32 47 8b 0a 06 in GNU
// coreutils 9.4, so the middle byte pins the bug.
TEST(cksum, cksum_raw_preserves_0x0a_digest_byte_issue1214) {
  TempDir tmp;
  tmp.write("raw66", std::string(66, 'x') + "\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cksum.exe", {L"--raw", L"raw66"});

  auto r = p.run();

  TEST_LOG_HEX("cksum --raw bytes", r.stdout_text);

  EXPECT_EXIT_CODE(r, 0);
  EXPECT_TRUE(r.stdout_text.size() == 4);
  EXPECT_BYTES(r.stdout_text, std::string("\x47\x8b\x0a\x06", 4));
}

namespace {
// #1288 fixture: an 8x40 directory chain (composed path past MAX_PATH),
// created through the extended-length API. Empty result: skip the test.
auto make_deep_tree_1288(const TempDir& tmp) -> std::wstring {
  std::wstring cursor = tmp.wpath();
  const std::wstring component(40, L'x');
  for (int i = 0; i < 8; ++i) {
    cursor += L"\\" + component;
    const std::wstring extended =
        cursor.rfind(L"\\\\?\\", 0) == 0 ? cursor : L"\\\\?\\" + cursor;
    if (!CreateDirectoryW(extended.c_str(), nullptr)) {
      const DWORD err = GetLastError();
      if (err != ERROR_ALREADY_EXISTS) return {};
    }
  }
  return cursor;
}
auto extended_1288(const std::wstring& p) -> std::wstring {
  return p.rfind(L"\\\\?\\", 0) == 0 ? p : L"\\\\?\\" + p;
}
}  // namespace

TEST(cksum, issue_1288_ignore_missing_verifies_target_past_max_path) {
  TempDir tmp;
  const std::wstring deep = make_deep_tree_1288(tmp);
  if (deep.empty() || deep.size() <= MAX_PATH) {
    std::cout << "  SKIPPED (deep fixture unavailable)\n";
    return;
  }
  temp_dir_detail::write_all(
      std::filesystem::path(extended_1288(deep)) / L"target.txt", "", 0);
  const std::wstring target = deep + L"\\target.txt";

  Pipeline gen;
  gen.set_cwd(tmp.wpath());
  gen.add(L"cksum.exe", {target});
  auto rg = gen.run();
  ASSERT_EQ(rg.exit_code, 0);
  tmp.write("list.cksum", rg.stdout_text);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cksum.exe", {L"-c", L"--ignore-missing", L"list.cksum"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("target.txt: OK"), std::string::npos);
}

// [#1362] -b/--binary and -t/--text are a last-occurrence-wins pair:
// "-t -b" resolves to binary, so tagged output is legal and the
// untagged marker is '*'.
TEST(cksum, issue1362_cksum_text_binary_last_wins_tagged) {
  TempDir tmp;
  tmp.write("f", "data\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cksum.exe", {L"-a", L"md5", L"-t", L"-b", L"f"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "MD5 (f) = 6137cde4893c59f76f005a8123d8e8e6\n");
}

TEST(cksum, issue1362_cksum_text_binary_last_wins_untagged) {
  TempDir tmp;
  tmp.write("f", "data\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cksum.exe", {L"-a", L"md5", L"--untagged", L"-t", L"-b", L"f"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "6137cde4893c59f76f005a8123d8e8e6 *f\n");
}

TEST(cksum, issue1362_cksum_binary_text_last_wins_untagged) {
  TempDir tmp;
  tmp.write("f", "data\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cksum.exe", {L"-a", L"md5", L"--untagged", L"-b", L"-t", L"f"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "6137cde4893c59f76f005a8123d8e8e6  f\n");
}

// Control: the reverse order resolves to text and keeps conflicting
// with the default tagged output on both implementations.
TEST(cksum, issue1362_cksum_binary_text_tagged_conflict) {
  TempDir tmp;
  tmp.write("f", "data\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cksum.exe", {L"-a", L"md5", L"-b", L"-t", L"f"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "--text mode is only supported with --untagged") !=
              std::string::npos);
}

TEST(cksum, cksum_invented_q_s_short_forms_rejected_issue1411) {
  // [GNU] --quiet/--status are long-only in GNU cksum; -q/-s belong to
  // md5sum & co. (#1411).
  TempDir tmp;
  tmp.write("f", "data\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cksum.exe", {L"-q", L"f"});
  auto r = p.run();

  EXPECT_EXIT_CODE(r, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.find("invalid option -- 'q'") != std::string::npos);

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"cksum.exe", {L"-s", L"f"});
  auto r2 = p2.run();

  EXPECT_EXIT_CODE(r2, 1);
  EXPECT_TRUE(r2.stdout_text.empty());
  EXPECT_TRUE(r2.stderr_text.find("invalid option -- 's'") != std::string::npos);
}

TEST(cksum, cksum_invented_r_reversed_style_rejected_issue1411) {
  // [GNU] cksum has no -r; the untagged form is spelled --untagged (#1411).
  TempDir tmp;
  tmp.write("f", "data\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cksum.exe", {L"-r", L"f"});
  auto r = p.run();

  EXPECT_EXIT_CODE(r, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.find("invalid option -- 'r'") != std::string::npos);
}

TEST(cksum, cksum_quiet_status_untagged_still_legal_issue1411) {
  TempDir tmp;
  tmp.write("f", "data\n");
  tmp.write("check.txt", "6137cde4893c59f76f005a8123d8e8e6  f\n");

  Pipeline good;
  good.set_cwd(tmp.wpath());
  good.add(L"cksum.exe", {L"-a", L"md5", L"--quiet", L"-c", L"check.txt"});
  auto rg = good.run();
  EXPECT_EXIT_CODE(rg, 0);
  EXPECT_TRUE(rg.stdout_text.empty());

  Pipeline quiet_tagged;
  quiet_tagged.set_cwd(tmp.wpath());
  quiet_tagged.add(L"cksum.exe", {L"-a", L"md5", L"--untagged", L"f"});
  auto rq = quiet_tagged.run();
  EXPECT_EXIT_CODE(rq, 0);
  EXPECT_EQ_TEXT(rq.stdout_text,
                 "6137cde4893c59f76f005a8123d8e8e6  f\n");
}
