// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

// GNU ncurses tput resolves capabilities through TERM, so every case that
// expects emitted bytes pins TERM explicitly. Oracle bytes below are the
// WSL ncurses 6.4 answers carried by the rubash#884 whitelist table.

TEST(tput, tput_clear) {
  Pipeline p;
  p.set_env(L"TERM", L"xterm");
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
  p.set_env(L"TERM", L"xterm");
  p.add(L"tput.exe", {L"reset"});

  TEST_LOG_CMD_LIST("tput.exe", L"reset");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("tput output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(tput, tput_bold) {
  Pipeline p;
  p.set_env(L"TERM", L"xterm");
  p.add(L"tput.exe", {L"bold"});

  TEST_LOG_CMD_LIST("tput.exe", L"bold");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("tput output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "\x1b[1m");
}

TEST(tput, tput_sgr0) {
  Pipeline p;
  p.set_env(L"TERM", L"xterm");
  p.add(L"tput.exe", {L"sgr0"});

  TEST_LOG_CMD_LIST("tput.exe", L"sgr0");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("tput output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // [GNU] xterm sgr0 is "\e(B\e[m", not the plain "\e[0m" the old table had.
  EXPECT_EQ_TEXT(r.stdout_text, "\x1b(B\x1b[m");
}

TEST(tput, tput_cup) {
  Pipeline p;
  p.set_env(L"TERM", L"xterm");
  p.add(L"tput.exe", {L"cup", L"5", L"10"});

  TEST_LOG_CMD_LIST("tput.exe", L"cup", L"5", L"10");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("tput output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // [GNU] cup is 1-based (tparm %i): cup 5 10 → "\e[6;11H".
  EXPECT_EQ_TEXT(r.stdout_text, "\x1b[6;11H");
}

TEST(tput, tput_missing_argument) {
  Pipeline p;
  p.add(L"tput.exe", {});

  TEST_LOG_CMD_LIST("tput.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tput stderr", r.stderr_text);

  // [GNU, #1460 oracle] no capname → usage on stderr, exit 2.
  EXPECT_EQ(r.exit_code, 2);
  EXPECT_FALSE(r.stderr_text.empty());
}

TEST(tput, tput_unknown_capability) {
  Pipeline p;
  p.set_env(L"TERM", L"xterm");
  p.add(L"tput.exe", {L"unknown"});

  TEST_LOG_CMD_LIST("tput.exe", L"unknown");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tput stderr", r.stderr_text);

  // [GNU, #1460 oracle] ncurses wording + exit 4.
  EXPECT_EQ(r.exit_code, 4);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "tput: unknown terminfo capability 'unknown'\n");
}

// [#1258] TERM matrix: the answer depends on the terminfo entry TERM names.
TEST(tput, tput_setaf_8color_term) {
  Pipeline p;
  p.set_env(L"TERM", L"xterm");
  p.add(L"tput.exe", {L"setaf", L"1"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("tput output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "\x1b[31m");
}

TEST(tput, tput_setaf_256color_term) {
  Pipeline p;
  p.set_env(L"TERM", L"xterm-256color");
  p.add(L"tput.exe", {L"setaf", L"9"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("tput output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // [GNU] 256-color bright pairs carry no ";5;" segment: setaf 9 → "\e[91m".
  EXPECT_EQ_TEXT(r.stdout_text, "\x1b[91m");
}

TEST(tput, tput_setaf_256color_palette) {
  Pipeline p;
  p.set_env(L"TERM", L"xterm-256color");
  p.add(L"tput.exe", {L"setaf", L"200"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("tput output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "\x1b[38;5;200m");
}

TEST(tput, tput_setab_256color_term) {
  Pipeline p;
  p.set_env(L"TERM", L"xterm-256color");
  p.add(L"tput.exe", {L"setab", L"15"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("tput output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "\x1b[107m");
}

TEST(tput, tput_setaf_formats_operand_verbatim_on_8color) {
  Pipeline p;
  p.set_env(L"TERM", L"screen");
  p.add(L"tput.exe", {L"setaf", L"10"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("tput output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // [GNU] 8-color entries format N into the termcap fallback "\e[3%p1%dm".
  EXPECT_EQ_TEXT(r.stdout_text, "\x1b[310m");
}

// [#1258] GNU parses operands with strtol base-0: a leading zero is octal.
TEST(tput, tput_setaf_octal_spelling) {
  Pipeline p;
  p.set_env(L"TERM", L"xterm");
  p.add(L"tput.exe", {L"setaf", L"010"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("tput output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "\x1b[38m");
}

TEST(tput, tput_dumb_answers_nothing) {
  Pipeline p;
  p.set_env(L"TERM", L"dumb");
  p.add(L"tput.exe", {L"setaf", L"1"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(tput, tput_absent_capability_is_silent_rc1) {
  Pipeline p;
  p.set_env(L"TERM", L"vt100");
  p.add(L"tput.exe", {L"setaf", L"1"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(tput, tput_missing_term_is_gnu_error) {
  Pipeline p;
  p.set_env(L"TERM", L"");
  p.add(L"tput.exe", {L"clear"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tput stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "tput: No value for $TERM and no -T specified\n");
}

TEST(tput, tput_dash_t_overrides_term_env) {
  Pipeline p;
  p.set_env(L"TERM", L"dumb");
  p.add(L"tput.exe", {L"-T", L"xterm-256color", L"setaf", L"9"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("tput output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "\x1b[91m");
}

// [GNU] a capname stream emits each answer in order.
TEST(tput, tput_capname_sequence) {
  Pipeline p;
  p.set_env(L"TERM", L"xterm");
  p.add(L"tput.exe", {L"bold", L"cup", L"0", L"0"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("tput output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "\x1b[1m\x1b[1;1H");
}

// [GNU differential, #1166] GNU tput (ncurses 6.4, WSL attested) answers
// capability queries from the terminfo database alone and never consumes
// stdin -- on success and on failure alike. Any script doing
//   printf 'aq' | { tput lines; IFS= read -rsn1 k; ... }
// must still see its 'aq' afterwards. The pure-Win32 pipeline keeps the
// stdin pipe open and measures what the child left unconsumed.
//
// The failure arm uses an unknown capname: since #1460 numeric capabilities
// are implemented, `tput lines` on xterm SUCCEEDS with "24" (rc 0) in GNU,
// so the old `lines` case stopped exercising a failure path at all.
TEST(tput, tput_failure_path_keeps_stdin_readable) {
  Pipeline p;
  p.set_env(L"TERM", L"xterm");
  p.add(L"tput.exe", {L"nosuchcap"});
  p.set_stdin("aq");
  p.probe_stdin_leftover(true);

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("stdin leftover", r.stdin_leftover);
  EXPECT_EQ(r.exit_code, 4);
  EXPECT_EQ_TEXT(r.stdin_leftover, "aq");
}

TEST(tput, tput_success_path_keeps_stdin_readable) {
  Pipeline p;
  p.set_env(L"TERM", L"xterm");
  p.add(L"tput.exe", {L"clear"});
  p.set_stdin("aq");
  p.probe_stdin_leftover(true);

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("stdin leftover", r.stdin_leftover);
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdin_leftover, "aq");
}

// [GNU, #1460 oracle] tput with no capname prints its usage on stderr and
// exits 2 (ncurses 6.4 WSL attested) — not the historical rc 1.
TEST(tput, tput_missing_argument_keeps_stdin_readable) {
  Pipeline p;
  p.add(L"tput.exe", {});
  p.set_stdin("aq");
  p.probe_stdin_leftover(true);

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("stdin leftover", r.stdin_leftover);
  EXPECT_EQ(r.exit_code, 2);
  EXPECT_EQ_TEXT(r.stdin_leftover, "aq");
}

// ---- #1460: numeric capabilities and the ncurses-6.4 error contract ----

// [GNU] `tput colors` / `pairs` answer the terminfo numbers with a newline.
TEST(tput, tput_numeric_colors_pairs) {
  Pipeline p;
  p.set_env(L"TERM", L"xterm-256color");
  p.add(L"tput.exe", {L"colors"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "256\n");

  Pipeline q;
  q.set_env(L"TERM", L"xterm-256color");
  q.add(L"tput.exe", {L"pairs"});
  auto rq = q.run();
  EXPECT_EQ(rq.exit_code, 0);
  EXPECT_EQ_TEXT(rq.stdout_text, "65536\n");
}

// [GNU] An absent numeric capability prints "-1" and exits 0 — vt100 has no
// colors, linux has no cols/lines constants in the non-tty oracle.
TEST(tput, tput_numeric_absent_prints_minus_one_rc0) {
  Pipeline p;
  p.set_env(L"TERM", L"vt100");
  p.add(L"tput.exe", {L"colors"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "-1\n");
}

// [GNU] numeric answers stream one per line: `tput colors cols` → 256\n80\n.
TEST(tput, tput_numeric_capname_sequence) {
  Pipeline p;
  p.set_env(L"TERM", L"xterm-256color");
  p.add(L"tput.exe", {L"colors", L"cols"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "256\n80\n");
}

// [GNU] unknown capname: "tput: unknown terminfo capability 'nosuchcap'" and
// exit 4 (ncurses 6.4 WSL attested; the old build said rc 1 with different
// wording).
TEST(tput, tput_unknown_capability_message_rc4) {
  Pipeline p;
  p.set_env(L"TERM", L"xterm-256color");
  p.add(L"tput.exe", {L"nosuchcap"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 4);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "tput: unknown terminfo capability 'nosuchcap'\n");
}

// [GNU] `tput setaf` with no operand prints the raw terminfo template.
TEST(tput, tput_parameterized_missing_operand_raw_template) {
  Pipeline p;
  p.set_env(L"TERM", L"xterm-256color");
  p.add(L"tput.exe", {L"setaf"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(
      r.stdout_text,
      "\x1b[%?%p1%{8}%<%t3%p1%d%e%p1%{16}%<%t9%p1%{8}%-%d%e38;5;%p1%d%;m");
}

// [GNU] a parameterized capability with a non-numeric operand formats with 0
// and lets the loop diagnose the operand as the next (unknown) capname.
TEST(tput, tput_parameterized_non_numeric_operand_formats_zero_rc4) {
  Pipeline p;
  p.set_env(L"TERM", L"xterm-256color");
  p.add(L"tput.exe", {L"cup", L"abc"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 4);
  EXPECT_EQ_TEXT(r.stdout_text, "\x1b[1;1H");
  EXPECT_EQ_TEXT(r.stderr_text,
                 "tput: unknown terminfo capability 'abc'\n");
}

// [GNU] a numeric capability ignores its trailing argv: `colors 9` prints 256
// and then diagnoses '9' as an unknown capname (rc 4).
TEST(tput, tput_numeric_ignores_trailing_operand_rc4) {
  Pipeline p;
  p.set_env(L"TERM", L"xterm-256color");
  p.add(L"tput.exe", {L"colors", L"9"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 4);
  EXPECT_EQ_TEXT(r.stdout_text, "256\n");
  EXPECT_EQ_TEXT(r.stderr_text,
                 "tput: unknown terminfo capability '9'\n");
}
