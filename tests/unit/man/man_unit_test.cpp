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

// [refs #1127] -k/--apropos searches registered names and descriptions.
TEST(man, apropos_finds_matching_command) {
  Pipeline p;
  p.add(L"man.exe", {L"-k", L"grep"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "grep - ");
}

TEST(man, apropos_without_match_reports_nothing_appropriate) {
  Pipeline p;
  p.add(L"man.exe", {L"--apropos", L"zzzqqqnomatch"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_CONTAINS(r.stdout_text, "nothing appropriate.");
}

// [refs #1127] --all shows every requested page in turn.
TEST(man, all_shows_each_requested_page) {
  Pipeline p;
  p.add(L"man.exe", {L"--all", L"ls", L"echo"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "ls");
  EXPECT_CONTAINS(r.stdout_text, "echo");
}

// [refs #1127] -C with a missing file is an error; an existing file is
// accepted.
TEST(man, config_file_must_exist) {
  TempDir tmp;
  tmp.write("man.conf", "# empty\n");

  Pipeline missing;
  missing.add(L"man.exe", {L"-C", (tmp.path / "absent.conf").wstring(), L"ls"});
  auto r_missing = missing.run();
  EXPECT_EQ(r_missing.exit_code, 1);
  EXPECT_CONTAINS(r_missing.stderr_text, "cannot open config file");

  Pipeline present;
  present.add(L"man.exe",
              {L"--config-file", (tmp.path / "man.conf").wstring(), L"ls"});
  auto r_present = present.run();
  EXPECT_EQ(r_present.exit_code, 0);
}

// [refs #1127] -d emits lookup traces on standard error.
TEST(man, debug_traces_lookup) {
  Pipeline p;
  p.add(L"man.exe", {L"-d", L"ls"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stderr_text, "[debug]");
}

// [refs #1127] --catman and --ditroff report the platform difference.
TEST(man, catman_and_ditroff_report_differs) {
  Pipeline catman;
  catman.add(L"man.exe", {L"--catman"});
  auto r_catman = catman.run();
  EXPECT_EQ(r_catman.exit_code, 1);
  EXPECT_CONTAINS(r_catman.stderr_text, "--catman is not available");

  Pipeline ditroff;
  ditroff.add(L"man.exe", {L"--ditroff", L"ls"});
  auto r_ditroff = ditroff.run();
  EXPECT_EQ(r_ditroff.exit_code, 1);
  EXPECT_CONTAINS(r_ditroff.stderr_text, "--ditroff is not available");
}

// [refs #1127] -7/--ascii and -D/--default are accepted.
TEST(man, ascii_and_default_are_accepted) {
  Pipeline p;
  p.add(L"man.exe", {L"-7", L"-D", L"ls"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "ls");
}
