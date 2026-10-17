/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(unix2dos, unix2dos_from_stdin) {
  Pipeline p;
  p.set_stdin("line1\nline2\n");
  p.add(L"unix2dos.exe", {});

  TEST_LOG_CMD_LIST("unix2dos.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("unix2dos output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("\r\n") != std::string::npos);
}

// [#1211] piped stdin is a byte stream: fd 0 must not drop data at 0x1A.
TEST(unix2dos, unix2dos_stdin_bytes_issue1211) {
  const std::string payload(
      "a\nb\n\x1a"
      "c\n",
      7);
  const std::string expected(
      "a\r\nb\r\n\x1a"
      "c\r\n",
      10);
  Pipeline p;
  p.set_stdin(payload);
  p.add(L"unix2dos.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, expected);
}
