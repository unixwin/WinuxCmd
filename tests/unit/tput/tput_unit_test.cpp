// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(tput, tput_clear) {
  Pipeline p;
  p.add(L"tput.exe", {L"clear"});

  TEST_LOG_CMD_LIST("tput.exe", L"clear");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("tput output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "\x1b[H\x1b[2J\x1b[3J");
}

TEST(tput, tput_reset) {
  Pipeline p;
  p.add(L"tput.exe", {L"reset"});

  TEST_LOG_CMD_LIST("tput.exe", L"reset");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("tput output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(tput, tput_bold) {
  Pipeline p;
  p.add(L"tput.exe", {L"bold"});

  TEST_LOG_CMD_LIST("tput.exe", L"bold");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("tput output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(tput, tput_sgr0) {
  Pipeline p;
  p.add(L"tput.exe", {L"sgr0"});

  TEST_LOG_CMD_LIST("tput.exe", L"sgr0");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("tput output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(tput, tput_cup) {
  Pipeline p;
  p.add(L"tput.exe", {L"cup"});

  TEST_LOG_CMD_LIST("tput.exe", L"cup");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("tput output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(tput, tput_missing_argument) {
  Pipeline p;
  p.add(L"tput.exe", {});

  TEST_LOG_CMD_LIST("tput.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tput stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_FALSE(r.stderr_text.empty());
}

TEST(tput, tput_unknown_capability) {
  Pipeline p;
  p.add(L"tput.exe", {L"unknown"});

  TEST_LOG_CMD_LIST("tput.exe", L"unknown");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tput stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_FALSE(r.stderr_text.empty());
}

// [GNU differential, #1166] GNU tput (ncurses 6.4, WSL attested) answers
// capability queries from the terminfo database alone and never consumes
// stdin -- on success and on failure alike. Any script doing
//   printf 'aq' | { tput lines; IFS= read -rsn1 k; ... }
// must still see its 'aq' afterwards. The pure-Win32 pipeline keeps the
// stdin pipe open and measures what the child left unconsumed.
TEST(tput, tput_failure_path_keeps_stdin_readable) {
  Pipeline p;
  p.add(L"tput.exe", {L"lines"});
  p.set_stdin("aq");
  p.probe_stdin_leftover(true);

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("stdin leftover", r.stdin_leftover);
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdin_leftover, "aq");
}

TEST(tput, tput_success_path_keeps_stdin_readable) {
  Pipeline p;
  p.add(L"tput.exe", {L"clear"});
  p.set_stdin("aq");
  p.probe_stdin_leftover(true);

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("stdin leftover", r.stdin_leftover);
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdin_leftover, "aq");
}

TEST(tput, tput_missing_argument_keeps_stdin_readable) {
  Pipeline p;
  p.add(L"tput.exe", {});
  p.set_stdin("aq");
  p.probe_stdin_leftover(true);

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("stdin leftover", r.stdin_leftover);
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdin_leftover, "aq");
}
