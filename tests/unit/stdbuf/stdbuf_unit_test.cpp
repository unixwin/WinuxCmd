/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(stdbuf, stdbuf_basic) {
  Pipeline p;
  p.add(L"stdbuf.exe", {L"--output=0", L"echo.exe", L"test"});

  TEST_LOG_CMD_LIST("stdbuf.exe", L"--output=0", L"echo.exe", L"test");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(stdbuf, stdbuf_attached_output_mode) {
  Pipeline p;
  p.add(L"stdbuf.exe", {L"-o0", L"echo.exe", L"test"});

  TEST_LOG_CMD_LIST("stdbuf.exe", L"-o0", L"echo.exe", L"test");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(stdbuf, stdbuf_accepts_bare_binary_suffix) {
  Pipeline p;
  p.add(L"stdbuf.exe", {L"--output=KiB", L"echo.exe", L"test"});

  TEST_LOG_CMD_LIST("stdbuf.exe", L"--output=KiB", L"echo.exe", L"test");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(stdbuf, stdbuf_rejects_line_buffered_stdin) {
  Pipeline p;
  p.add(L"stdbuf.exe", {L"-iL", L"echo.exe", L"test"});

  TEST_LOG_CMD_LIST("stdbuf.exe", L"-iL", L"echo.exe", L"test");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("stdbuf stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_FALSE(r.stderr_text.empty());
}

TEST(stdbuf, stdbuf_rejects_zero_sized_buffer_mode) {
  Pipeline p;
  p.add(L"stdbuf.exe", {L"--output=0KiB", L"echo.exe", L"test"});

  TEST_LOG_CMD_LIST("stdbuf.exe", L"--output=0KiB", L"echo.exe", L"test");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("stdbuf stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("invalid mode for standard output") !=
              std::string::npos);
}
