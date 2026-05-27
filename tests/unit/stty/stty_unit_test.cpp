/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(stty, stty_no_args_shows_settings) {
  Pipeline p;
  p.add(L"stty.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should output terminal settings
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(stty, stty_dash_a_shows_all) {
  Pipeline p;
  p.add(L"stty.exe", {L"-a"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(stty, stty_g_machine_readable) {
  Pipeline p;
  p.add(L"stty.exe", {L"-g"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Machine-readable format contains colons and hex values
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(stty, stty_sane) {
  Pipeline p;
  p.add(L"stty.exe", {L"sane"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(stty, stty_raw) {
  Pipeline p;
  p.add(L"stty.exe", {L"raw"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(stty, stty_cooked) {
  Pipeline p;
  p.add(L"stty.exe", {L"cooked"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(stty, stty_echo) {
  Pipeline p;
  p.add(L"stty.exe", {L"echo"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(stty, stty_no_echo) {
  Pipeline p;
  p.add(L"stty.exe", {L"-echo"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(stty, stty_cbreak) {
  Pipeline p;
  p.add(L"stty.exe", {L"cbreak"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(stty, stty_invalid_setting) {
  Pipeline p;
  p.add(L"stty.exe", {L"invalidxyz"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
}
