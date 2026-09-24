#include "framework/winuxtest.h"

TEST(logger, stderr_option_prints_tagged_message) {
  Pipeline p;
  p.add(L"logger.exe", {L"-s", L"-t", L"unit", L"hello", L"world"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_CONTAINS(r.stderr_text, "unit: hello world");
}

TEST(logger, accepts_priority_and_empty_stdin) {
  Pipeline p;
  p.add(L"logger.exe", {L"-p", L"user.warning"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}
