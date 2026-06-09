#include "framework/winuxtest.h"

TEST(runcon, runcon_no_args_reports_windows_limitation) {
  Pipeline p;
  p.add(L"runcon.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("not supported on Windows"), std::string::npos);
}

TEST(runcon, runcon_plain_context_requires_command) {
  Pipeline p;
  p.add(L"runcon.exe", {L"system_u:system_r:httpd_t:s0"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("missing command"), std::string::npos);
}

TEST(runcon, runcon_plain_context_form_reports_windows_limitation) {
  Pipeline p;
  p.add(L"runcon.exe",
        {L"system_u:system_r:httpd_t:s0", L"cmd.exe", L"/c", L"echo", L"hi"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("not supported on Windows"), std::string::npos);
}

TEST(runcon, runcon_custom_context_without_command_reports_windows_limitation) {
  Pipeline p;
  p.add(L"runcon.exe", {L"-t", L"httpd_t"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("not supported on Windows"), std::string::npos);
}

TEST(runcon, runcon_custom_context_with_command_reports_windows_limitation) {
  Pipeline p;
  p.add(L"runcon.exe", {L"-u", L"system_u", L"-r", L"system_r", L"-t",
                        L"httpd_t", L"cmd.exe", L"/c", L"echo", L"hi"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("not supported on Windows"), std::string::npos);
}
