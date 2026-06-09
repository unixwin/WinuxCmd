#include "framework/winuxtest.h"

TEST(chcon, chcon_missing_operand_fails) {
  Pipeline p;
  p.add(L"chcon.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("missing operand"), std::string::npos);
}

TEST(chcon, chcon_missing_file_after_context_fails) {
  Pipeline p;
  p.add(L"chcon.exe", {L"system_u:object_r:user_home_t:s0"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("missing file operand"), std::string::npos);
}

TEST(chcon, chcon_reference_requires_file_operand) {
  Pipeline p;
  p.add(L"chcon.exe", {L"--reference=reference.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("missing file operand"), std::string::npos);
}

TEST(chcon, chcon_existing_file_reports_windows_limitation) {
  TempDir tmp;
  tmp.write("file.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chcon.exe", {L"system_u:object_r:user_home_t:s0", L"file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("not supported on Windows"), std::string::npos);
}

TEST(chcon, chcon_reference_form_reports_windows_limitation) {
  TempDir tmp;
  tmp.write("reference.txt", "ref");
  tmp.write("target.txt", "target");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chcon.exe", {L"--reference=reference.txt", L"target.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("not supported on Windows"), std::string::npos);
}

TEST(chcon, chcon_verbose_reports_each_file) {
  TempDir tmp;
  tmp.write("one.txt", "1");
  tmp.write("two.txt", "2");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chcon.exe",
        {L"-v", L"system_u:object_r:user_home_t:s0", L"one.txt", L"two.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("one.txt"), std::string::npos);
  EXPECT_NE(r.stderr_text.find("two.txt"), std::string::npos);
}
