// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

#include <algorithm>

// The w tests below are deliberately session-count independent: the WTS
// session table behind the rows differs per machine, so every assertion
// reads only faces that the header/footer machinery fixes regardless of
// how many (or how few) login sessions exist.

// The uptime/header block is unconditional for a default w: exit 0 and
// both the uptime line ("load average") and the column heading ("USER").
TEST(w, w_basic) {
  Pipeline p;
  p.add(L"w.exe", {});

  TEST_LOG_CMD_LIST("w.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("w output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_CONTAINS(r.stdout_text, "load average");
  EXPECT_CONTAINS(r.stdout_text, "USER");
  EXPECT_CONTAINS(r.stdout_text, "TTY");
  EXPECT_CONTAINS(r.stdout_text, "FROM");
  EXPECT_CONTAINS(r.stdout_text, "LOGIN@");
  EXPECT_CONTAINS(r.stdout_text, "WHAT");
}

// [GNU] procps w.c: -h/--no-header SUPPRESSES the uptime+heading block.
TEST(w, w_h_suppresses_header) {
  Pipeline p;
  p.add(L"w.exe", {L"-h"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("w -h output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NOT_CONTAINS(r.stdout_text, "load average");
  EXPECT_NOT_CONTAINS(r.stdout_text, "LOGIN@");
}

TEST(w, w_no_header_long_spelling) {
  Pipeline p;
  p.add(L"w.exe", {L"--no-header"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NOT_CONTAINS(r.stdout_text, "load average");
  EXPECT_NOT_CONTAINS(r.stdout_text, "LOGIN@");
}

// [GNU] procps w.c:805: the short header tail is "   IDLE WHAT" and the
// LOGIN@/JCPU/PCPU columns disappear.
TEST(w, w_short_format_header) {
  Pipeline p;
  p.add(L"w.exe", {L"-s"});

  auto r = p.run();

  TEST_LOG("w -s output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "load average");
  EXPECT_CONTAINS(r.stdout_text, "IDLE WHAT");
  EXPECT_NOT_CONTAINS(r.stdout_text, "LOGIN@");
  EXPECT_NOT_CONTAINS(r.stdout_text, "JCPU");
}

// [GNU] procps w.c: the FROM field is printed by default; -f toggles it
// off (w.cpp 'f' toggles the from field).
TEST(w, w_f_toggles_from_off) {
  Pipeline p;
  p.add(L"w.exe", {L"-f"});

  auto r = p.run();

  TEST_LOG("w -f output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NOT_CONTAINS(r.stdout_text, "FROM");
}

// [GNU] procps w.c: -i forces the from field on alongside the IP display.
TEST(w, w_i_enables_from) {
  Pipeline p;
  p.add(L"w.exe", {L"-i"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "FROM");
}

// [GNU] procps w.c: -p and -o are accepted boolean modifiers.
TEST(w, w_pids_and_old_style_accepted) {
  Pipeline p;
  p.add(L"w.exe", {L"-p", L"-o"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

// A [user] operand filters the listing: a name that cannot match any
// session leaves exactly the two header lines and still exits 0
// [GNU] (procps w exits 0 for an empty listing).
TEST(w, w_user_filter_no_match_yields_header_only) {
  Pipeline p;
  p.add(L"w.exe", {L"zz_no_such_winux_user_zz"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("w filtered output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  const int lines = static_cast<int>(
      std::count(r.stdout_text.begin(), r.stdout_text.end(), '\n'));
  EXPECT_EQ(lines, 2);  // uptime line + column heading, no data rows
  EXPECT_CONTAINS(r.stdout_text, "load average");
}

// [GNU] procps w.c usage(): an unknown short option is diagnosed on
// stderr and w exits 1.
TEST(w, w_invalid_short_option) {
  Pipeline p;
  p.add(L"w.exe", {L"-Z"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("w -Z stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "w: invalid option -- 'Z'\n"
                 "Try 'w --help' for more information.\n");
}

// [GNU] procps w.c usage(): an unknown long option is "unrecognized
// option", also exit 1.
TEST(w, w_invalid_long_option) {
  Pipeline p;
  p.add(L"w.exe", {L"--bogus-option"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "w: unrecognized option '--bogus-option'\n"
                 "Try 'w --help' for more information.\n");
}

// --help exits 0 on stdout with the declared synopsis.
TEST(w, w_help) {
  Pipeline p;
  p.add(L"w.exe", {L"--help"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_CONTAINS(r.stdout_text, "Usage: w [options] [user]");
  EXPECT_CONTAINS(r.stdout_text, "--no-header");
}

// [GNU] procps w declares -V/--version; exit 0.
TEST(w, w_version) {
  Pipeline p;
  p.add(L"w.exe", {L"--version"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_CONTAINS(r.stdout_text, "w (WinuxCmd)");
}

TEST(w, w_version_short_v) {
  Pipeline p;
  p.add(L"w.exe", {L"-V"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "w (WinuxCmd)");
}
