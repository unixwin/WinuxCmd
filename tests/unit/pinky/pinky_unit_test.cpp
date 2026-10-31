/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

// [#1462] GNU pinky short format prints the heading AND one row per
// login entry; the old pin asserted the heading alone (the rows were
// missing), which contradicts pinky.c scan_entries/print_entry.
TEST(pinky, pinky_short_heading) {
  Pipeline p;
  p.add(L"pinky.exe", {});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("pinky output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  const std::string heading =
      "Login    Name                 TTY      Idle   When         Where\n";
  EXPECT_TRUE(r.stdout_text.rfind(heading, 0) == 0);
  // At least one login-entry row follows the heading.
  EXPECT_TRUE(r.stdout_text.size() > heading.size());
}

TEST(pinky, pinky_omit_heading) {
  Pipeline p;
  p.add(L"pinky.exe", {L"-f"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find("Login") == std::string::npos);
}

// [#1462] With a USER argument that has no login entry, GNU prints
// no rows (and no error).
TEST(pinky, pinky_short_unknown_user_no_rows) {
  Pipeline p;
  p.add(L"pinky.exe", {L"-f", L"nosuchuser"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "");
}

TEST(pinky, pinky_long_unknown_user) {
  Pipeline p;
  p.add(L"pinky.exe", {L"-l", L"nosuchuser"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(
      r.stdout_text,
      "Login name: nosuchuser                  In real life:  ???\n");
}
