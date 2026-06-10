/*
 *  Copyright © 2026 WinuxCmd
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: echo_unit_test.cpp
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(echo, echo_basic) {
  Pipeline p;
  p.add(L"echo.exe", {L"hello", L"world"});

  TEST_LOG_CMD_LIST("echo.exe", L"hello", L"world");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("Output", r.stdout_text);

  EXPECT_EQ_TEXT(r.stdout_text, "hello world\n");
}

TEST(echo, echo_empty_args_outputs_newline) {
  Pipeline p;
  p.add(L"echo.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(r.stdout_text, "\n");
}

TEST(echo, echo_no_newline) {
  Pipeline p;
  p.add(L"echo.exe", {L"-n", L"hello", L"world"});

  TEST_LOG_CMD_LIST("echo.exe", L"-n", L"hello", L"world");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("Output", r.stdout_text);

  EXPECT_EQ_TEXT(r.stdout_text, "hello world");
}

TEST(echo, echo_uppercase) {
  Pipeline p;
  p.add(L"echo.exe", {L"-u", L"hello", L"world"});

  TEST_LOG_CMD_LIST("echo.exe", L"-u", L"hello", L"world");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("Output", r.stdout_text);

  EXPECT_EQ_TEXT(r.stdout_text, "HELLO WORLD\n");
}

TEST(echo, echo_repeat) {
  Pipeline p;
  p.add(L"echo.exe", {L"--repeat", L"3", L"test"});

  TEST_LOG_CMD_LIST("echo.exe", L"--repeat", L"3", L"test");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("Output", r.stdout_text);

  EXPECT_EQ_TEXT(r.stdout_text, "test\ntest\ntest\n");
}

TEST(echo, echo_escapes) {
  Pipeline p;
  p.add(L"echo.exe", {L"-e", L"line1\\nline2\\t tabbed"});

  TEST_LOG_CMD_LIST("echo.exe", L"-e", L"line1\\nline2\\t tabbed");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("Output", r.stdout_text);

  EXPECT_EQ_TEXT(r.stdout_text, "line1\nline2\t tabbed\n");
}

TEST(echo, echo_escape_no_hex_keeps_backslash) {
  Pipeline p;
  p.add(L"echo.exe", {L"-e", L"foo\\x bar"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(r.stdout_text, "foo\\x bar\n");
}

TEST(echo, echo_unknown_escape_keeps_backslash) {
  Pipeline p;
  p.add(L"echo.exe", {L"-e", L"foo\\ bar"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(r.stdout_text, "foo\\ bar\n");
}

TEST(echo, echo_old_style_octal_escape) {
  Pipeline p;
  p.add(L"echo.exe", {L"-e", L"\\43foo"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(r.stdout_text, "#foo\n");
}

TEST(echo, echo_old_style_octal_stops_after_three_digits) {
  Pipeline p;
  p.add(L"echo.exe", {L"-e", L"\\1011"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(r.stdout_text, "A1\n");
}

TEST(echo, echo_old_style_octal_wraps_to_single_byte) {
  Pipeline p;
  p.add(L"echo.exe", {L"-e", L"\\777"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ(r.stdout_text, std::string("\xFF\n", 2));
}

TEST(echo, echo_hex_escape_round_trips_non_utf8_byte) {
  Pipeline p;
  p.add(L"echo.exe", {L"-e", L"\\xFF"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ(r.stdout_text, std::string("\xFF\n", 2));
}

TEST(echo, echo_hex_escape_utf8_sequence_forms_multibyte_text) {
  Pipeline p;
  p.add(L"echo.exe", {L"-e", L"\\x41\\xf0\\x9f\\x98\\x82\\x42"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(r.stdout_text, "A😂B\n");
}

TEST(echo, echo_unicode_escape_u_emits_utf8) {
  Pipeline p;
  p.add(L"echo.exe", {L"-e", L"\\u4F60\\u597D"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(r.stdout_text, "你好\n");
}

TEST(echo, echo_unicode_escape_U_emits_utf8) {
  Pipeline p;
  p.add(L"echo.exe", {L"-e", L"\\U0001F602"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(r.stdout_text, "😂\n");
}

TEST(echo, echo_unicode_escape_without_hex_keeps_backslash) {
  Pipeline p;
  p.add(L"echo.exe", {L"-e", L"foo\\u bar \\U baz"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(r.stdout_text, "foo\\u bar \\U baz\n");
}

TEST(echo, echo_c_escape_suppresses_further_output_and_newline) {
  Pipeline p;
  p.add(L"echo.exe", {L"-e", L"foo\\cbar"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(r.stdout_text, "foo");
}

TEST(echo, echo_suppress_escapes) {
  Pipeline p;
  p.add(L"echo.exe", {L"-E", L"line1\\nline2"});

  TEST_LOG_CMD_LIST("echo.exe", L"-E", L"line1\\nline2");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("Output", r.stdout_text);

  EXPECT_EQ_TEXT(r.stdout_text, "line1\\nline2\n");
}

TEST(echo, echo_escape_last_option_wins_disable) {
  Pipeline p;
  p.add(L"echo.exe", {L"-e", L"-E", L"\\na"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(r.stdout_text, "\\na\n");
}

TEST(echo, echo_escape_last_option_wins_enable) {
  Pipeline p;
  p.add(L"echo.exe", {L"-E", L"-e", L"\\na"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(r.stdout_text, "\na\n");
}

TEST(echo, echo_escape_last_option_wins_with_no_newline) {
  Pipeline p;
  p.add(L"echo.exe", {L"-E", L"-e", L"-n", L"\\na"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(r.stdout_text, "\na");
}

TEST(echo, echo_escape_disable_last_option_wins_with_no_newline) {
  Pipeline p;
  p.add(L"echo.exe", {L"-e", L"-E", L"-n", L"\\na"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(r.stdout_text, "\\na");
}

TEST(echo, echo_posixly_correct_processes_escapes_by_default) {
  Pipeline p;
  p.set_env(L"POSIXLY_CORRECT", L"1");
  p.add(L"echo.exe", {L"foo\\n"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(r.stdout_text, "foo\n\n");
}

TEST(echo, echo_posixly_correct_keeps_n_option) {
  Pipeline p;
  p.set_env(L"POSIXLY_CORRECT", L"1");
  p.add(L"echo.exe", {L"-n", L"foo"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(r.stdout_text, "foo");
}

TEST(echo, echo_posixly_correct_ignores_E_and_processes_escapes) {
  Pipeline p;
  p.set_env(L"POSIXLY_CORRECT", L"1");
  p.add(L"echo.exe", {L"-n", L"-E", L"foo\\cbar"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(r.stdout_text, "foo");
}

TEST(echo, echo_posixly_correct_treats_help_as_literal_text) {
  Pipeline p;
  p.set_env(L"POSIXLY_CORRECT", L"1");
  p.add(L"echo.exe", {L"--help"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(r.stdout_text, "--help\n");
}

TEST(echo, echo_posixly_correct_treats_version_as_literal_text) {
  Pipeline p;
  p.set_env(L"POSIXLY_CORRECT", L"1");
  p.add(L"echo.exe", {L"--version"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(r.stdout_text, "--version\n");
}

TEST(echo, echo_posixly_correct_treats_non_n_option_cluster_as_literal_text) {
  Pipeline p;
  p.set_env(L"POSIXLY_CORRECT", L"1");
  p.add(L"echo.exe", {L"-nE", L"foo"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(r.stdout_text, "-nE foo\n");
}

TEST(echo, echo_complex) {
  Pipeline p;
  p.add(L"echo.exe", {L"-n", L"-e", L"Hello\\tWorld!\\n"});

  TEST_LOG_CMD_LIST("echo.exe", L"-n", L"-e", L"Hello\\tWorld!\\n");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("Output", r.stdout_text);

  EXPECT_EQ_TEXT(r.stdout_text, "Hello\tWorld!\n");
}
