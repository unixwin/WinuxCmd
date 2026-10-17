/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(dos2unix, dos2unix_from_stdin) {
  Pipeline p;
  p.set_stdin("line1\r\nline2\r\n");
  p.add(L"dos2unix.exe", {});

  TEST_LOG_CMD_LIST("dos2unix.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("dos2unix output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "line1\nline2\n");
}

TEST(dos2unix, dos2unix_stdin_preserves_missing_final_newline) {
  Pipeline p;
  p.set_stdin("x");
  p.add(L"dos2unix.exe", {});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "x");
}

// [#1211] piped stdin is a byte stream: fd 0 must not drop data at 0x1A.
TEST(dos2unix, dos2unix_stdin_bytes_issue1211) {
  const std::string payload(
      "a\r\nb\r\n\x1a"
      "c\r\n",
      10);
  const std::string expected(
      "a\nb\n\x1a"
      "c\n",
      7);
  Pipeline p;
  p.set_stdin(payload);
  p.add(L"dos2unix.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, expected);
}
