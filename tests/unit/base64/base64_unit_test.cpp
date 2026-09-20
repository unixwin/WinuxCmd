// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(base64, shared_stdin_reader_preserves_binary_bytes) {
  const std::string payload("A\x1a\0\r\nB", 6);
  for (const auto *command : {L"base64.exe", L"base32.exe"}) {
    Pipeline p;
    p.set_stdin(payload);
    p.add(command, {});
    p.add(command, {L"-d"});
    const auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_text, payload);
    EXPECT_TRUE(r.stderr_text.empty());
  }
}

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

TEST(base64, base64_encode_file_with_utf8_name) {
  TempDir tmp;
  const std::wstring filename = L"\u6570\u636e.txt";
  const auto file_path = tmp.path / filename;
  {
    std::ofstream ofs(file_path, std::ios::binary);
    ofs << "hello\n";
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"base64.exe", {filename});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "aGVsbG8K\n");
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
  EXPECT_EQ_TEXT(unwrapped_result.stdout_text, "aGVsbG8gd29ybGQ=");
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
  EXPECT_EQ_TEXT(r.stderr_text,
                 "base64: extra operand 'two.txt'\n"
                 "Try 'base64 --help' for more information.\n");
}

TEST(base64, base64_missing_input_reports_no_such_file) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"base64.exe", {L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "base64: cannot open 'missing.txt' for reading: No such "
                  "file or directory") != std::string::npos);
}

TEST(base64, base64_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("indir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"base64.exe", {L"indir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "base64: cannot open 'indir' for reading: Is a directory") !=
              std::string::npos);
}

// [GNU] any non-numeric or negative -w value dies with the raw token
// quoted: "invalid wrap size: '<raw>'" (uutils #14084).
TEST(base64, base64_wrap_rejects_negative_with_quoted_value) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"base64.exe", {L"-w", L"-5", L"input.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("base64: invalid wrap size: '-5'"),
            std::string::npos);
}

TEST(base64, base64_wrap_rejects_non_numeric_with_quoted_value) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"base64.exe", {L"-w", L"-d", L"input.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("base64: invalid wrap size: '-d'"),
            std::string::npos);
}

TEST(base64, base64_wrap_zero_disables_wrapping) {
  TempDir tmp;
  tmp.write("input.txt", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"base64.exe", {L"-w", L"0", L"input.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  // [GNU] -w 0 disables wrapping and emits no trailing newline.
  EXPECT_EQ_TEXT(r.stdout_text, "YWJj");
}
