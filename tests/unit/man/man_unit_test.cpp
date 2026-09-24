#include "framework/winuxtest.h"

TEST(man, list_mode_shows_registered_commands) {
  Pipeline p;
  p.add(L"man.exe", {L"--list"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "Available commands:");
  EXPECT_CONTAINS(r.stdout_text, "grep");
}

TEST(man, command_page_is_returned_for_registered_command) {
  Pipeline p;
  p.add(L"man.exe", {L"ls"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "ls");
}

TEST(man, missing_command_reports_no_manual_entry) {
  Pipeline p;
  p.add(L"man.exe", {L"definitely_missing_command"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_CONTAINS(r.stdout_text, "no manual entry");
}

TEST(man, no_arguments_prints_usage) {
  Pipeline p;
  p.add(L"man.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "Usage: man");
}
