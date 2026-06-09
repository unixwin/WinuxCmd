#include "framework/winuxtest.h"

TEST(mkfifo, mkfifo_missing_operand) {
  Pipeline p;
  p.add(L"mkfifo.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("missing operand"), std::string::npos);
}

TEST(mkfifo, mkfifo_invalid_mode_fails) {
  Pipeline p;
  p.add(L"mkfifo.exe", {L"-m", L"bad!mode", L"pipe1"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("invalid mode"), std::string::npos);
}

TEST(mkfifo, mkfifo_existing_path_fails_like_gnu_shape) {
  TempDir tmp;
  tmp.write("pipe1", "already here");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkfifo.exe", {L"pipe1"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("File exists"), std::string::npos);
}

TEST(mkfifo, mkfifo_reports_windows_limitation) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkfifo.exe", {L"pipe1"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("not supported on Windows"), std::string::npos);
}

TEST(mkfifo, mkfifo_multiple_operands_report_independently) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkfifo.exe", {L"pipe1", L"pipe2"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("pipe1"), std::string::npos);
  EXPECT_NE(r.stderr_text.find("pipe2"), std::string::npos);
}
