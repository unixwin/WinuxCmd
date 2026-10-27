#include "framework/winuxtest.h"

TEST(runcon, runcon_no_args_reports_windows_limitation) {
  Pipeline p;
  p.add(L"runcon.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("failed to get current context"),
            std::string::npos);
}

TEST(runcon, runcon_plain_context_requires_command) {
  Pipeline p;
  p.add(L"runcon.exe", {L"system_u:system_r:httpd_t:s0"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("no command specified"), std::string::npos);
}

TEST(runcon, runcon_plain_context_form_reports_windows_limitation) {
  Pipeline p;
  p.add(L"runcon.exe",
        {L"system_u:system_r:httpd_t:s0", L"cmd.exe", L"/c", L"echo", L"hi"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("not supported on Windows"), std::string::npos);
}

TEST(runcon, runcon_custom_context_without_command_requires_command) {
  Pipeline p;
  p.add(L"runcon.exe", {L"-t", L"httpd_t"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("no command specified"), std::string::npos);
}

TEST(runcon, runcon_custom_context_with_command_reports_windows_limitation) {
  Pipeline p;
  p.add(L"runcon.exe", {L"-u", L"system_u", L"-r", L"system_r", L"-t",
                        L"httpd_t", L"cmd.exe", L"/c", L"echo", L"hi"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("not supported on Windows"), std::string::npos);
}
// #1375: runcon's own option parsing stops at CONTEXT, so the wrapped
// command's -l is not misread as runcon's own -l/--range missing its
// argument; and option errors exit 125 (GNU EXIT_USAGE), not 1.
TEST(runcon, runcon_wrapped_command_options_pass_through_issue1375) {
  Pipeline p;
  p.add(L"runcon.exe", {L"ctx", L"ls.exe", L"-l"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("not supported on Windows"),
            std::string::npos);
  EXPECT_EQ(r.stderr_text.find("option requires an argument"),
            std::string::npos);
}

TEST(runcon, runcon_option_error_exits_125_issue1375) {
  Pipeline p;
  p.add(L"runcon.exe", {L"--zzz", L"ctx", L"ls.exe"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 125);
}

