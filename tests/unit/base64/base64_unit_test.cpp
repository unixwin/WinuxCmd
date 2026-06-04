/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *  sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 *
 *  - File: base64_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(base64, base64_encode_basic) {
  Pipeline p;
  p.set_stdin("hello world");
  p.add(L"base64.exe", {});

  TEST_LOG_CMD_LIST("base64.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("base64 output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "aGVsbG8gd29ybGQ=\n");
}

TEST(base64, base64_decode_basic) {
  Pipeline p;
  p.set_stdin("aGVsbG8gd29ybGQ=");
  p.add(L"base64.exe", {L"-d"});

  TEST_LOG_CMD_LIST("base64.exe", L"-d");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("base64 decode output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "hello world");
}

TEST(base64, base64_encode_file) {
  TempDir tmp;
  tmp.write("test.txt", "hello world\n");

  TEST_LOG_FILE_CONTENT("test.txt", "hello world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"base64.exe", {L"test.txt"});

  TEST_LOG_CMD_LIST("base64.exe", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("base64 file output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "aGVsbG8gd29ybGQK\n");
}

TEST(base64, base64_decode_file) {
  TempDir tmp;
  tmp.write("encoded.txt", "aGVsbG8gd29ybGQK\n");

  TEST_LOG_FILE_CONTENT("encoded.txt", "aGVsbG8gd29ybGQK\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"base64.exe", {L"-d", L"encoded.txt"});

  TEST_LOG_CMD_LIST("base64.exe", L"-d", L"encoded.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("base64 decode file output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "hello world\n");
}

TEST(base64, base64_wrap_width_and_wrap_zero) {
  Pipeline wrapped;
  wrapped.set_stdin("hello world");
  wrapped.add(L"base64.exe", {L"-w", L"4"});

  auto wrapped_result = wrapped.run();

  EXPECT_EQ(wrapped_result.exit_code, 0);
  EXPECT_EQ_TEXT(wrapped_result.stdout_text, "aGVs\nbG8g\nd29y\nbGQ=\n");

  Pipeline unwrapped;
  unwrapped.set_stdin("hello world");
  unwrapped.add(L"base64.exe", {L"--wrap=0"});

  auto unwrapped_result = unwrapped.run();

  EXPECT_EQ(unwrapped_result.exit_code, 0);
  EXPECT_EQ_TEXT(unwrapped_result.stdout_text, "aGVsbG8gd29ybGQ=\n");
}

TEST(base64, base64_empty_input_produces_no_output) {
  Pipeline p;
  p.set_stdin("");
  p.add(L"base64.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "");
}

TEST(base64, base64_decode_reports_invalid_input_without_ignore_garbage) {
  Pipeline p;
  p.set_stdin("aGVs!bG8=");
  p.add(L"base64.exe", {L"-d"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("invalid input") != std::string::npos);
}

TEST(base64, base64_decode_ignore_garbage_recovers_payload) {
  Pipeline p;
  p.set_stdin("aGVs!bG8=");
  p.add(L"base64.exe", {L"-d", L"--ignore-garbage"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "hello");
}

TEST(base64, base64_rejects_extra_file_operand) {
  TempDir tmp;
  tmp.write("one.txt", "one");
  tmp.write("two.txt", "two");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"base64.exe", {L"one.txt", L"two.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("extra operand") != std::string::npos);
}
