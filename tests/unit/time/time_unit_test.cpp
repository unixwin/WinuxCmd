// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(time_cmd, time_preserves_exit_code) {
  Pipeline p;
  p.add(L"time.exe", {L"cmd.exe", L"/c", L"exit", L"7"});

  TEST_LOG_CMD_LIST("time.exe", L"cmd.exe", L"/c", L"exit", L"7");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("time.exe output", r.stderr_text);

  EXPECT_EQ(r.exit_code, 7);
  EXPECT_FALSE(r.stderr_text.empty());
  EXPECT_NE(r.stderr_text.find("real"), std::string::npos);
  EXPECT_NE(r.stderr_text.find("user"), std::string::npos);
  EXPECT_NE(r.stderr_text.find("sys"), std::string::npos);
}

TEST(time_cmd, time_posix_format) {
  Pipeline p;
  p.add(L"time.exe", {L"-p", L"cmd.exe", L"/c", L"exit", L"0"});

  TEST_LOG_CMD_LIST("time.exe", L"-p", L"cmd.exe", L"/c", L"exit", L"0");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("time.exe -p output", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stderr_text.empty());
  EXPECT_NE(r.stderr_text.find("real "), std::string::npos);
  EXPECT_NE(r.stderr_text.find("user "), std::string::npos);
  EXPECT_NE(r.stderr_text.find("sys "), std::string::npos);
}
// #1375: time's own option parsing stops at COMMAND; `time ls -l`
// used to fail with "time: invalid option -- 'l'".
TEST(time_cmd, time_wrapped_command_options_pass_through_issue1375) {
  TempDir tmp;
  tmp.write("marker.txt", "x\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"time.exe", {L"ls.exe", L"-l"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("marker.txt") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("real") != std::string::npos);
}

// [#1127 P2c] GNU time's -o/--output writes the report to a file; the
// child exit status still passes through and stderr stays clean.
TEST(time_cmd, time_output_option_writes_report_to_file) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"time.exe", {L"-o", L"report.txt", L"cmd.exe", L"/c", L"exit", L"3"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 3);
  EXPECT_TRUE(r.stderr_text.find("real") == std::string::npos);
  const std::string report = tmp.read("report.txt");
  EXPECT_TRUE(report.find("real") != std::string::npos);
  EXPECT_TRUE(report.find("user") != std::string::npos);
  EXPECT_TRUE(report.find("sys") != std::string::npos);
}

// [#1127 P2c] -a/--append appends to the -o file instead of truncating.
TEST(time_cmd, time_append_option_appends_report) {
  TempDir tmp;

  Pipeline first;
  first.set_cwd(tmp.wpath());
  first.add(L"time.exe",
            {L"-o", L"report.txt", L"cmd.exe", L"/c", L"exit", L"0"});
  EXPECT_EQ(first.run().exit_code, 0);

  Pipeline second;
  second.set_cwd(tmp.wpath());
  second.add(L"time.exe",
             {L"-a", L"-o", L"report.txt", L"cmd.exe", L"/c", L"exit", L"0"});
  EXPECT_EQ(second.run().exit_code, 0);

  const std::string report = tmp.read("report.txt");
  size_t occurrences = 0;
  for (size_t pos = report.find("real"); pos != std::string::npos;
       pos = report.find("real", pos + 1)) {
    ++occurrences;
  }
  EXPECT_EQ(occurrences, 2u);
}

// [#1127 P2c] -f/--format expands resource specifiers (%x, %e, %C, %%).
TEST(time_cmd, time_format_option_expands_specifiers) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"time.exe", {L"-f", L"status=%x cmd=%C pct=%%", L"-o", L"out.txt",
                      L"cmd.exe", L"/c", L"exit", L"5"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 5);
  const std::string out = tmp.read("out.txt");
  EXPECT_TRUE(out.find("status=5") != std::string::npos);
  EXPECT_TRUE(out.find("cmd=cmd.exe") != std::string::npos);
  EXPECT_TRUE(out.find("pct=%") != std::string::npos);
}

// [#1127 P2c] -v/--verbose prints the full GNU-style field list.
TEST(time_cmd, time_verbose_option_prints_full_report) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"time.exe",
        {L"-v", L"-o", L"verbose.txt", L"cmd.exe", L"/c", L"exit", L"0"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  const std::string out = tmp.read("verbose.txt");
  EXPECT_TRUE(out.find("Command being timed") != std::string::npos);
  EXPECT_TRUE(out.find("Maximum resident set size (kbytes)") !=
              std::string::npos);
  EXPECT_TRUE(out.find("Page size (bytes)") != std::string::npos);
  EXPECT_TRUE(out.find("Exit status: 0") != std::string::npos);
}

// [#1127 P2c] --portability is GNU's long name for -p; --quiet is accepted
// and keeps the exit status passthrough.
TEST(time_cmd, time_portability_and_quiet_options) {
  Pipeline p;
  p.add(L"time.exe", {L"--portability", L"cmd.exe", L"/c", L"exit", L"0"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("real ") != std::string::npos);
  // POSIX form carries no 's' suffix.
  EXPECT_TRUE(r.stderr_text.find("s\n") == std::string::npos);

  Pipeline q;
  q.add(L"time.exe", {L"-q", L"cmd.exe", L"/c", L"exit", L"9"});
  auto qr = q.run();
  EXPECT_EQ(qr.exit_code, 9);
  EXPECT_TRUE(qr.stderr_text.find("real") != std::string::npos);
}

// [#1127 P2c] An -o file that cannot be opened fails before running.
TEST(time_cmd, time_output_open_failure_is_an_error) {
  TempDir tmp;
  const std::wstring bad_path = tmp.wpath() + L"\\missing\\report.txt";

  Pipeline p;
  p.add(L"time.exe", {L"-o", bad_path, L"cmd.exe", L"/c", L"exit", L"0"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_TRUE(r.stderr_text.find("cannot open") != std::string::npos);
}
