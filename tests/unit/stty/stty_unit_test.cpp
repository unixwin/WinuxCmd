/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

namespace {

void expect_inappropriate_ioctl(const CommandResult& r) {
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_NE(r.stderr_text.find("Inappropriate ioctl for device"),
            std::string::npos);
}

}  // namespace

TEST(stty, stty_no_args_requires_terminal) {
  Pipeline p;
  p.add(L"stty.exe", {});
  auto r = p.run();

  expect_inappropriate_ioctl(r);
  EXPECT_NE(r.stderr_text.find("standard input"), std::string::npos);
}

TEST(stty, stty_dash_a_requires_terminal) {
  Pipeline p;
  p.add(L"stty.exe", {L"-a"});
  auto r = p.run();

  expect_inappropriate_ioctl(r);
}

TEST(stty, stty_g_requires_terminal) {
  Pipeline p;
  p.add(L"stty.exe", {L"-g"});
  auto r = p.run();

  expect_inappropriate_ioctl(r);
}

TEST(stty, stty_known_settings_validate_then_require_terminal) {
  for (const auto* arg :
       {L"sane", L"raw", L"cooked", L"echo", L"-echo", L"cbreak"}) {
    Pipeline p;
    p.add(L"stty.exe", {arg});
    auto r = p.run();

    expect_inappropriate_ioctl(r);
  }
}

TEST(stty, stty_invalid_setting_reports_try_help) {
  Pipeline p;
  p.add(L"stty.exe", {L"invalidxyz"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("invalid argument"), std::string::npos);
  EXPECT_NE(r.stderr_text.find("invalidxyz"), std::string::npos);
  EXPECT_NE(r.stderr_text.find("stty --help"), std::string::npos);
}

TEST(stty, stty_value_setting_missing_argument) {
  Pipeline p;
  p.add(L"stty.exe", {L"rows"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("missing argument to"), std::string::npos);
  EXPECT_NE(r.stderr_text.find("rows"), std::string::npos);
  EXPECT_NE(r.stderr_text.find("stty --help"), std::string::npos);
}

TEST(stty, stty_missing_file_device) {
  Pipeline p;
  p.add(L"stty.exe", {L"-F", L"missing-device"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("missing-device: No such file or directory"),
            std::string::npos);
}

TEST(stty, stty_output_styles_are_mutually_exclusive) {
  Pipeline p;
  p.add(L"stty.exe", {L"-a", L"-g"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("mutually exclusive"), std::string::npos);
}

TEST(stty, stty_output_style_rejects_settings) {
  Pipeline p;
  p.add(L"stty.exe", {L"-a", L"sane"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("modes may not be set"), std::string::npos);
}

// #1430: `size` is a GNU output-style request, not an apply-mode setting.
// It used to be accepted as a no-op setting, printing nothing and
// returning 0 on a real console.
TEST(stty, stty_size_requires_terminal) {
  Pipeline p;
  p.add(L"stty.exe", {L"size"});
  auto r = p.run();

  expect_inappropriate_ioctl(r);
}

TEST(stty, stty_size_is_an_output_style) {
  Pipeline p;
  p.add(L"stty.exe", {L"size", L"sane"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("modes may not be set"), std::string::npos);
}

TEST(stty, stty_size_and_all_are_mutually_exclusive) {
  Pipeline p;
  p.add(L"stty.exe", {L"-a", L"size"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("mutually exclusive"), std::string::npos);
}

// #1430: `speed` is the same query family as `size`.
TEST(stty, stty_speed_is_an_output_style) {
  Pipeline p;
  p.add(L"stty.exe", {L"speed", L"sane"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("modes may not be set"), std::string::npos);
}

// #1430: the rows/cols value forms are valid GNU settings; they must
// pass validation and reach the terminal check, not "invalid argument".
TEST(stty, stty_rows_cols_value_form_accepted) {
  Pipeline p;
  p.add(L"stty.exe", {L"rows", L"40", L"cols", L"120"});
  auto r = p.run();

  expect_inappropriate_ioctl(r);
  EXPECT_EQ(r.stderr_text.find("invalid argument"), std::string::npos);
}

TEST(stty, stty_rows_noninteger_value_rejected) {
  Pipeline p;
  p.add(L"stty.exe", {L"rows", L"abc"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("invalid argument"), std::string::npos);
  EXPECT_NE(r.stderr_text.find("abc"), std::string::npos);
}
