/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

// [GNU] The harness runs more with stdin redirected (a pipe), which
// is the decisive environment in util-linux more.c: main() first
// consumes the redirected stdin preamble, then FORCES print_banner
// and clears first_file (more.c:2110-2123), so every following file
// operand is announced with the ':' separator banner
// (display_file, more.c:1918-1934), and with stdout redirected the
// file body goes through copy_file() -- a raw byte copy
// (more.c:1881-1888, 1936-1937). The expectations in this file pin
// that contract: banner + raw content.
TEST(more, more_basic_non_tty_passthrough) {
  TempDir tmp;
  tmp.write("test.txt", "line1\nline2\nline3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"more.exe", {L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "::::::::::::::\ntest.txt\n::::::::::::::\n"
                 "line1\nline2\nline3\n");
}

TEST(more, more_squeeze_blank_non_tty) {
  TempDir tmp;
  tmp.write("test.txt", "line1\n\n\n\nline2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"more.exe", {L"-s", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // [GNU] -s squeeze lives in screen()'s line output (more.c:1830);
  // the non-tty copy_file() path copies bytes raw, blanks unsqueezed.
  EXPECT_EQ_TEXT(r.stdout_text,
                 "::::::::::::::\ntest.txt\n::::::::::::::\n"
                 "line1\n\n\n\nline2\n");
}

TEST(more, more_clean_print_short_option_does_not_consume_file_operand) {
  TempDir tmp;
  tmp.write("test.txt", "one\ntwo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"more.exe", {L"-p", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "::::::::::::::\ntest.txt\n::::::::::::::\n"
                 "one\ntwo\n");
}

TEST(more, more_clean_print_long_option_does_not_consume_file_operand) {
  TempDir tmp;
  tmp.write("test.txt", "one\ntwo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"more.exe", {L"--clean-print", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "::::::::::::::\ntest.txt\n::::::::::::::\n"
                 "one\ntwo\n");
}

TEST(more, more_numeric_window_option_is_accepted) {
  TempDir tmp;
  tmp.write("test.txt", "line1\nline2\nline3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"more.exe", {L"-5", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "::::::::::::::\ntest.txt\n::::::::::::::\n"
                 "line1\nline2\nline3\n");
}

TEST(more, more_long_lines_option_is_accepted) {
  TempDir tmp;
  tmp.write("test.txt", "line1\nline2\nline3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"more.exe", {L"--lines", L"5", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "::::::::::::::\ntest.txt\n::::::::::::::\n"
                 "line1\nline2\nline3\n");
}

TEST(more, more_plus_line_starts_output_at_line_number) {
  TempDir tmp;
  tmp.write("test.txt", "one\ntwo\nthree\nfour\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"more.exe", {L"+3", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // [GNU] +NUM positioning belongs to the FIRST displayed input
  // (display_file's first_file preamble, more.c:1899-1907). With
  // stdin redirected that input is the stdin preamble, and first_file
  // is cleared before file operands run (more.c:2121-2123), so the
  // file operand is copied whole.
  EXPECT_EQ_TEXT(r.stdout_text,
                 "::::::::::::::\ntest.txt\n::::::::::::::\n"
                 "one\ntwo\nthree\nfour\n");
}

TEST(more, more_plus_pattern_starts_output_at_match) {
  TempDir tmp;
  tmp.write("test.txt", "alpha\nbeta\nneedle here\nomega\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"more.exe", {L"+/needle", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // [GNU] +/PATTERN positioning is consumed by the first displayed
  // input exactly like +NUM (see more_plus_line): with stdin
  // redirected the file operand is copied whole.
  EXPECT_EQ_TEXT(r.stdout_text,
                 "::::::::::::::\ntest.txt\n::::::::::::::\n"
                 "alpha\nbeta\nneedle here\nomega\n");
}

TEST(more, more_plain_and_exit_on_eof_options_are_accepted) {
  TempDir tmp;
  tmp.write("test.txt", "plain\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"more.exe", {L"--plain", L"--exit-on-eof", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "::::::::::::::\ntest.txt\n::::::::::::::\n"
                 "plain\n");
}

TEST(more, more_nonexistent_file) {
  Pipeline p;
  p.add(L"more.exe", {L"nonexistent_file_xyz.txt"});
  auto r = p.run();

  // [GNU] checkf() only warns "cannot open %s" and returns
  // (more.c:459); more_exit() then _exit(EXIT_SUCCESS)
  // unconditionally (more.c:789). A missing file is NOT an error
  // exit in util-linux more.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("cannot open nonexistent_file_xyz.txt") !=
              std::string::npos);
}

// [#1212] piped stdin is a byte stream: fd 0 must not drop data at 0x1A.
TEST(more, more_stdin_bytes_issue1212) {
  const std::string payload(
      "AAAA\x1a"
      "BBBB\n",
      10);
  const std::string expected(
      "AAAA\x1a"
      "BBBB\n",
      10);
  Pipeline p;
  p.set_stdin(payload);
  p.add(L"more.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, expected);
}
