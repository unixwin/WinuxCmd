/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(pinky, pinky_short_heading) {
  Pipeline p;
  p.add(L"pinky.exe", {});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("pinky output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(
      r.stdout_text,
      "Login    Name                 TTY      Idle   When         Where\n");
}

TEST(pinky, pinky_omit_heading) {
  Pipeline p;
  p.add(L"pinky.exe", {L"-f"});

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
