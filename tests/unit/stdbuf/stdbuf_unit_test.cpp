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

TEST(stdbuf, stdbuf_preserves_argument_with_spaces) {
  Pipeline p;
  p.add(L"stdbuf.exe", {L"printf.exe", L"<%s>", L"a b"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("stdbuf preserved space argument output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(r.stdout_text, "<a b>");
}

TEST(stdbuf, stdbuf_missing_command_reports_help_hint_on_stderr) {
  Pipeline p;
  p.add(L"stdbuf.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "stdbuf: missing command\n"
      "Try 'stdbuf --help' for more information.\n");
}

TEST(stdbuf, stdbuf_invalid_option_returns_125) {
  Pipeline p;
  p.add(L"stdbuf.exe", {L"--invalid"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "stdbuf: unrecognized option '--invalid'\n"
      "Try 'stdbuf --help' for more information.\n");
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

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_FALSE(r.stderr_text.empty());
}

TEST(stdbuf, stdbuf_rejects_zero_sized_buffer_mode) {
  Pipeline p;
  p.add(L"stdbuf.exe", {L"--output=0KiB", L"echo.exe", L"test"});

  TEST_LOG_CMD_LIST("stdbuf.exe", L"--output=0KiB", L"echo.exe", L"test");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("stdbuf stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_TRUE(r.stderr_text.find("invalid mode for standard output") !=
              std::string::npos);
}
