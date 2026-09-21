// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(tr, tr_translate_simple) {
  Pipeline p;
  p.set_stdin("hello world");
  p.add(L"tr.exe", {L"a-z", L"A-Z"});

  TEST_LOG_CMD_LIST("tr.exe", L"a-z", L"A-Z");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tr output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "HELLO WORLD");
}

TEST(tr, tr_delete_characters) {
  Pipeline p;
  p.set_stdin("hello123world");
  p.add(L"tr.exe", {L"-d", L"0-9"});

  TEST_LOG_CMD_LIST("tr.exe", L"-d", L"0-9");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tr delete output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "helloworld");
}

TEST(tr, tr_translate_character_classes) {
  Pipeline p;
  p.set_stdin("abc xyz 123");
  p.add(L"tr.exe", {L"[:lower:]", L"[:upper:]"});

  TEST_LOG_CMD_LIST("tr.exe", L"[:lower:]", L"[:upper:]");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tr class translate output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "ABC XYZ 123");
}

TEST(tr, tr_octal_escape_uses_all_digits) {
  Pipeline p;
  p.set_stdin("abc");
  p.add(L"tr.exe", {L"\\141", L"X"});

  TEST_LOG_CMD_LIST("tr.exe", L"\\141", L"X");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tr octal escape output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "Xbc");
}

TEST(tr, tr_delete_digit_class) {
  Pipeline p;
  p.set_stdin("a1b23c456");
  p.add(L"tr.exe", {L"-d", L"[:digit:]"});

  TEST_LOG_CMD_LIST("tr.exe", L"-d", L"[:digit:]");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tr digit class delete output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "abc");
}

TEST(tr, tr_squeeze_repeats) {
  Pipeline p;
  p.set_stdin("hello    world");
  p.add(L"tr.exe", {L"-s", L" "});

  TEST_LOG_CMD_LIST("tr.exe", L"-s", L" ");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tr squeeze output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "hello world");
}

TEST(tr, tr_squeeze_space_class) {
  Pipeline p;
  p.set_stdin("a   b\t\tc\n\n");
  p.add(L"tr.exe", {L"-s", L"[:space:]"});

  TEST_LOG_CMD_LIST("tr.exe", L"-s", L"[:space:]");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tr space class squeeze output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a b\tc\n");
}

TEST(tr, tr_translate_then_squeeze_last_set) {
  Pipeline p;
  p.set_stdin("aaabbbccc");
  p.add(L"tr.exe", {L"-s", L"[:lower:]", L"[:upper:]"});

  TEST_LOG_CMD_LIST("tr.exe", L"-s", L"[:lower:]", L"[:upper:]");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tr translate squeeze output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "ABC");
}

TEST(tr, tr_delete_and_squeeze) {
  Pipeline p;
  p.set_stdin("aabbcc123  456");
  p.add(L"tr.exe", {L"-ds", L"0-9", L" "});

  TEST_LOG_CMD_LIST("tr.exe", L"-ds", L"0-9", L" ");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tr delete and squeeze output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "aabbcc ");
}

TEST(tr, tr_delete_and_squeeze_preserves_single_squeeze_char) {
  Pipeline p;
  p.set_stdin("a1 b");
  p.add(L"tr.exe", {L"-ds", L"[:digit:]", L" "});

  TEST_LOG_CMD_LIST("tr.exe", L"-ds", L"[:digit:]", L" ");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tr delete and squeeze single output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a b");
}

TEST(tr, tr_missing_operand_reports_help_hint) {
  Pipeline p;
  p.add(L"tr.exe", {});

  TEST_LOG_CMD_LIST("tr.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tr missing operand stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ(r.stdout_text, "");
  EXPECT_EQ_TEXT(r.stderr_text,
                 "tr: missing operand\n"
                 "Try 'tr --help' for more information.\n");
}

TEST(tr, tr_translate_requires_second_set) {
  Pipeline p;
  p.add(L"tr.exe", {L"a-z"});

  TEST_LOG_CMD_LIST("tr.exe", L"a-z");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tr missing set2 stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ(r.stdout_text, "");
  EXPECT_EQ_TEXT(r.stderr_text,
                 "tr: missing operand after 'a-z'\n"
                 "Two strings must be given when translating.\n"
                 "Try 'tr --help' for more information.\n");
}

TEST(tr, tr_delete_and_squeeze_require_second_set) {
  Pipeline p;
  p.add(L"tr.exe", {L"-ds", L"0-9"});

  TEST_LOG_CMD_LIST("tr.exe", L"-ds", L"0-9");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tr missing squeeze set stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ(r.stdout_text, "");
  EXPECT_EQ_TEXT(r.stderr_text,
                 "tr: missing operand after '0-9'\n"
                 "Two strings must be given when deleting and squeezing.\n"
                 "Try 'tr --help' for more information.\n");
}

TEST(tr, tr_delete_without_squeeze_rejects_extra_operand) {
  Pipeline p;
  p.add(L"tr.exe", {L"-d", L"0-9", L"X"});

  TEST_LOG_CMD_LIST("tr.exe", L"-d", L"0-9", L"X");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tr delete extra operand stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ(r.stdout_text, "");
  EXPECT_EQ_TEXT(r.stderr_text,
                 "tr: extra operand 'X'\n"
                 "Only one string may be given when deleting without "
                 "squeezing repeats.\n"
                 "Try 'tr --help' for more information.\n");
}

TEST(tr, tr_reverse_range_reports_gnu_shaped_diagnostic) {
  Pipeline p;
  p.add(L"tr.exe", {L"z-a", L"x"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ(r.stdout_text, "");
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "tr: range-endpoints of 'z-a' are in reverse collating sequence order\n");
}

TEST(tr, tr_delete_carriage_returns_from_binary_stdin) {
  Pipeline p;
  p.set_stdin("a\rb\r\nc");
  p.add(L"tr.exe", {L"-d", L"\\r"});

  TEST_LOG_CMD_LIST("tr.exe", L"-d", L"\\r");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tr delete CR output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "ab\nc");
}

// [GNU] -A is an undocumented hidden flag accepted as a no-op (issue #1079).
TEST(tr, tr_undocumented_A_flag_is_noop) {
  Pipeline p;
  p.set_stdin("abc");
  p.add(L"tr.exe", {L"-A", L"a", L"b"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "bbc");
}
