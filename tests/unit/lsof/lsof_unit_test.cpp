// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(lsof, lsof_help) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"lsof.exe", {L"--help"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("Usage:") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("OPTIONS") != std::string::npos);
}

TEST(lsof, lsof_basic_runs) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"lsof.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("COMMAND") != std::string::npos);
}

TEST(lsof, lsof_filter_self_pid) {
  TempDir tmp;

  wchar_t pid_buf[32] = {};
  _snwprintf_s(pid_buf, std::size(pid_buf), _TRUNCATE, L"%lu",
               GetCurrentProcessId());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"lsof.exe", {L"--pid", pid_buf, L"--no-headers"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(lsof, lsof_invalid_option) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"lsof.exe", {L"--definitely-invalid"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
}

TEST(lsof, lsof_field_mode_runs) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"lsof.exe", {L"-F"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(lsof, lsof_internet_filter_runs) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"lsof.exe", {L"-i", L"--no-headers"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(lsof, lsof_attached_internet_filter_runs) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"lsof.exe", {L"-iTCP:80", L"--no-headers", L"-t", L"50"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("unrecognized option") == std::string::npos);
}
