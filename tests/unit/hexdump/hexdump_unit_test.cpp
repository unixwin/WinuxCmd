/*
 *  Copyright (c) 2026 WinuxCmd
 */
#include "framework/winuxtest.h"

static void write_binary_file(const std::filesystem::path& path,
                              std::string_view data) {
  std::ofstream out(path, std::ios::binary);
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
}

TEST(hexdump, hexdump_canonical_matches_util_linux_shape) {
  TempDir tmp;
  write_binary_file(tmp.path / "test.bin", "0123456789abcdefABCDEFGHIJKLMNOP");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-C", L"-n", L"32", L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  const std::string expected =
      "00000000  30 31 32 33 34 35 36 37  38 39 61 62 63 64 65 66  "
      "|0123456789abcdef|\n"
      "00000010  41 42 43 44 45 46 47 48  49 4a 4b 4c 4d 4e 4f 50  "
      "|ABCDEFGHIJKLMNOP|\n"
      "00000020\n";
  EXPECT_EQ_TEXT(r.stdout_text, expected);
}

TEST(hexdump, hexdump_canonical_skip_and_length_preserve_offsets) {
  TempDir tmp;
  write_binary_file(tmp.path / "test.bin", "0123456789abcdefABCDEFGHIJKLMNOP");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-C", L"-s", L"16", L"-n", L"8", L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  const std::string expected =
      "00000010  41 42 43 44 45 46 47 48                           |ABCDEFGH|\n"
      "00000018\n";
  EXPECT_EQ_TEXT(r.stdout_text, expected);
}

TEST(hexdump, hexdump_default_is_two_byte_hex) {
  TempDir tmp;
  write_binary_file(tmp.path / "test.bin", "0123456789abcdef");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  const std::string expected =
      "0000000 3130 3332 3534 3736 3938 6261 6463 6665\n"
      "0000010\n";
  EXPECT_EQ_TEXT(r.stdout_text, expected);
}

TEST(hexdump, hexdump_hex2_explicit_uses_padded_words) {
  TempDir tmp;
  write_binary_file(tmp.path / "test.bin", "0123456789abcdef");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-x", L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // [GNU] the row's last unit loses its trailing space (the nospace
  // rule, hexdump-parse.c), so a full -x row is 71 columns, not 72.
  const std::string expected =
      "0000000    3130    3332    3534    3736    3938    6261    6463    6665"
      "\n"
      "0000010\n";
  EXPECT_EQ_TEXT(r.stdout_text, expected);
}

TEST(hexdump, hexdump_octal1_prints_final_offset) {
  TempDir tmp;
  write_binary_file(tmp.path / "test.bin", "ABC");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-b", L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  const std::string expected =
      "0000000 101 102 103                                                    "
      "\n"
      "0000003\n";
  EXPECT_EQ_TEXT(r.stdout_text, expected);
}

TEST(hexdump, hexdump_char_escapes_control_chars) {
  TempDir tmp;
  write_binary_file(tmp.path / "test.bin", "A\n\t");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-c", L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  const std::string expected =
      "0000000   A  \\n  \\t                                                   "
      " \n"
      "0000003\n";
  EXPECT_EQ_TEXT(r.stdout_text, expected);
}

TEST(hexdump, hexdump_empty_canonical_is_silent) {
  TempDir tmp;
  write_binary_file(tmp.path / "empty.bin", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-C", L"empty.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(hexdump, hexdump_length) {
  TempDir tmp;
  write_binary_file(tmp.path / "test.bin", "Hello World");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-n", L"5", L"-C", L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  const std::string expected =
      "00000000  48 65 6c 6c 6f                                    |Hello|\n"
      "00000005\n";
  EXPECT_EQ_TEXT(r.stdout_text, expected);
}

TEST(hexdump, hexdump_stdin) {
  Pipeline p;
  p.set_stdin("Hello World");
  p.add(L"hexdump.exe", {L"-C"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("0000000b") != std::string::npos);
}

TEST(hexdump, hexdump_invalid_format_reports_error) {
  TempDir tmp;
  write_binary_file(tmp.path / "test.bin", "abc");
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-e", L"%", L"test.bin"});
  auto r = p.run();
  EXPECT_NE(r.exit_code, 0);
  EXPECT_FALSE(r.stderr_text.empty());
}

TEST(hexdump, hexdump_nonexistent_file) {
  Pipeline p;
  p.add(L"hexdump.exe", {L"nonexistent_file_xyz.bin"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
}

// [#1212] fd 0 is text mode by default, so a pipe stopped at the first 0x1A
// and disagreed with the file operand. Both forms must be byte-identical.
TEST(hexdump, hexdump_stdin_ctrlz_matches_file_operand_issue1212) {
  const std::string payload(
      "AAAA\x1a"
      "BBBB\n",
      10);
  TempDir tmp;
  tmp.write_bytes("p.bin",
                  {'A', 'A', 'A', 'A', 0x1a, 'B', 'B', 'B', 'B', '\n'});

  Pipeline by_file;
  by_file.set_cwd(tmp.wpath());
  by_file.add(L"hexdump.exe", {L"-C", L"-v", L"p.bin"});
  auto rf = by_file.run();

  Pipeline by_stdin;
  by_stdin.set_stdin(payload);
  by_stdin.add(L"hexdump.exe", {L"-C", L"-v"});
  auto rs = by_stdin.run();

  EXPECT_EQ(rs.exit_code, 0);
  EXPECT_FALSE(rf.stdout_text.empty());
  EXPECT_EQ(rs.stdout_text, rf.stdout_text);
}

// #1373: the -e/-f format engine is the core of hexdump; -e was
// rejected and -f silently ignored. Repeated lines squeeze to "*",
// 2-byte modes print a trailing odd byte, and multiple files form one
// continuous stream. Expected outputs follow util-linux hexdump.
TEST(hexdump, hexdump_e_format_string_issue1373) {
  TempDir tmp;
  write_binary_file(tmp.path / "test.bin", "abcde");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-e", L"1/1 \"%02x\\n\"", L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "61\n62\n63\n64\n65\n");
}

TEST(hexdump, hexdump_f_format_file_issue1373) {
  TempDir tmp;
  write_binary_file(tmp.path / "test.bin", "abcde");
  write_binary_file(tmp.path / "fmt.txt", "1/1 \"%02x\\n\"\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-f", L"fmt.txt", L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "61\n62\n63\n64\n65\n");
}

TEST(hexdump, hexdump_repeated_lines_squeezed_issue1373) {
  TempDir tmp;
  write_binary_file(tmp.path / "rep.bin", std::string(48, 'a'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"rep.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "0000000 6161 6161 6161 6161 6161 6161 6161 6161\n"
                 "*\n"
                 "0000030\n");
}

TEST(hexdump, hexdump_v_disables_squeeze_issue1373) {
  TempDir tmp;
  write_binary_file(tmp.path / "rep.bin", std::string(48, 'a'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-v", L"rep.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "0000000 6161 6161 6161 6161 6161 6161 6161 6161\n"
                 "0000010 6161 6161 6161 6161 6161 6161 6161 6161\n"
                 "0000020 6161 6161 6161 6161 6161 6161 6161 6161\n"
                 "0000030\n");
}

TEST(hexdump, hexdump_two_byte_modes_print_trailing_odd_byte_issue1373) {
  TempDir tmp;
  write_binary_file(tmp.path / "test.bin", "abcde");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-x", L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // [GNU] a partial block is blank-padded to the full 71-column row
  // (bpad, hexdump-display.c): the five missing units print as blanks.
  EXPECT_EQ_TEXT(r.stdout_text,
                 std::string("0000000    6261    6463    0065") +
                     std::string(40, ' ') + "\n"
                     "0000005\n");
}

TEST(hexdump, hexdump_multiple_files_form_one_stream_issue1373) {
  TempDir tmp;
  write_binary_file(tmp.path / "f1.bin", std::string(16, 'a'));
  write_binary_file(tmp.path / "f2.bin", std::string(16, 'b'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"f1.bin", L"f2.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "0000000 6161 6161 6161 6161 6161 6161 6161 6161\n"
                 "0000010 6262 6262 6262 6262 6262 6262 6262 6262\n"
                 "0000020\n");
}

// [GNU] the -o unit is " %06o " (8 wide, hexdump.c), so a full -o row
// is 71 columns like -x/-d; it used to render 9-wide units (80 cols).
TEST(hexdump, hexdump_octal2_row_width_matches_gnu) {
  TempDir tmp;
  write_binary_file(tmp.path / "test.bin", "0123456789abcdef");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-o", L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "0000000  030460  031462  032464  033466  034470  061141  "
                 "062143  063145\n"
                 "0000010\n");
}

// [GNU] %_c is the escaped-character conversion of the oracle's own -c
// format string; "%3_c" right-aligns the escape in a 3-wide field.
TEST(hexdump, hexdump_e_escaped_char_conversion) {
  TempDir tmp;
  write_binary_file(tmp.path / "test.bin", "a\nb");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-e", L"3/1 \"%3_c\"", L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "  a \\n  b");
}

// [GNU] %s writes raw bytes and stops at the first NUL; it does not
// mask non-printing bytes with '.'.
TEST(hexdump, hexdump_e_string_stops_at_nul) {
  TempDir tmp;
  tmp.write_bytes("test.bin", {'h', 'i', 0, 'x', 'y'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-e", L"1/5 \"%s\"", L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "hi");
}

// [GNU] %c writes the byte itself, control bytes included.
TEST(hexdump, hexdump_e_char_writes_raw_byte) {
  TempDir tmp;
  tmp.write_bytes("test.bin", {0x01, 'A'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-e", L"2/1 \"%c\"", L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, std::string("\x01" "A"));
}

// [GNU] -n/-s accept strtosize values: base-0 numbers and size
// suffixes (hexdump.c parse_args via strtosize_or_err).
TEST(hexdump, hexdump_length_accepts_hex_and_suffix) {
  TempDir tmp;
  write_binary_file(tmp.path / "test.bin", "Hello World");

  Pipeline hex;
  hex.set_cwd(tmp.wpath());
  hex.add(L"hexdump.exe", {L"-n", L"0x5", L"-C", L"test.bin"});
  auto rh = hex.run();
  EXPECT_EQ(rh.exit_code, 0);
  EXPECT_EQ_TEXT(rh.stdout_text,
                 "00000000  48 65 6c 6c 6f                                    "
                 "|Hello|\n"
                 "00000005\n");

  Pipeline suffix;
  suffix.set_cwd(tmp.wpath());
  suffix.add(L"hexdump.exe", {L"-n", L"1K", L"-C", L"test.bin"});
  auto rs = suffix.run();
  EXPECT_EQ(rs.exit_code, 0);
  EXPECT_TRUE(rs.stdout_text.find("|Hello World|") != std::string::npos);
}

TEST(hexdump, hexdump_skip_accepts_octal) {
  TempDir tmp;
  write_binary_file(tmp.path / "test.bin", "Hello World");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-s", L"010", L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 std::string("0000008 6c72 0064") + std::string(25, ' ') +
                     "\n000000b\n");
}

TEST(hexdump, hexdump_bad_length_reports_parse_failure) {
  TempDir tmp;
  write_binary_file(tmp.path / "test.bin", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-n", L"bogus", L"test.bin"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("failed to parse length: 'bogus'") !=
              std::string::npos);
}

// #1374: in WINUX_SHELL_GLOB=native mode hexdump expands its file
// operands through the shared glob path like cat does.
TEST(hexdump, hexdump_native_glob_expands_operands_issue1374) {
  TempDir tmp;
  write_binary_file(tmp.path / "in.bin", "only\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"WINUX_SHELL_GLOB", L"native");
  p.add(L"hexdump.exe", {L"in.*"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // [GNU] the default mode's partial row is blank-padded to 47 columns
  // as well (bpad blanks for the five missing units).
  EXPECT_EQ_TEXT(r.stdout_text,
                 std::string("0000000 6e6f 796c 000a") + std::string(25, ' ') +
                     "\n0000005\n");
}
