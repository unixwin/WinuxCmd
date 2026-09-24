// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(who, who_basic) {
  Pipeline p;
  p.add(L"who.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(who, who_short) {
  Pipeline p;
  p.add(L"who.exe", {L"-s"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(who, who_count) {
  Pipeline p;
  p.add(L"who.exe", {L"-q"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("# users") != std::string::npos ||
              r.stdout_text.find("1") != std::string::npos);
}

TEST(who, who_heading) {
  Pipeline p;
  p.add(L"who.exe", {L"-H"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should show column headers
}

TEST(who, who_idle) {
  Pipeline p;
  p.add(L"who.exe", {L"-u"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should show idle time
}

TEST(who, who_all) {
  Pipeline p;
  p.add(L"who.exe", {L"-a"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_CONTAINS(r.stderr_text, "not supported on Windows");
}

TEST(who, who_boot) {
  Pipeline p;
  p.add(L"who.exe", {L"-b"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_CONTAINS(r.stderr_text, "not supported on Windows");
}

TEST(who, who_dead) {
  Pipeline p;
  p.add(L"who.exe", {L"-d"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_CONTAINS(r.stderr_text, "not supported on Windows");
}

TEST(who, who_login) {
  Pipeline p;
  p.add(L"who.exe", {L"-l"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_CONTAINS(r.stderr_text, "not supported on Windows");
}

TEST(who, who_process) {
  Pipeline p;
  p.add(L"who.exe", {L"-p"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_CONTAINS(r.stderr_text, "not supported on Windows");
}

TEST(who, who_runlevel) {
  Pipeline p;
  p.add(L"who.exe", {L"-r"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_CONTAINS(r.stderr_text, "not supported on Windows");
}

TEST(who, who_message) {
  Pipeline p;
  p.add(L"who.exe", {L"-T"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should show message status
}

TEST(who, who_writable) {
  Pipeline p;
  p.add(L"who.exe", {L"-w"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should show writable status
}

TEST(who, who_message_long) {
  Pipeline p;
  p.add(L"who.exe", {L"--message"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find(" + ") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(who, who_lookup) {
  Pipeline p;
  p.add(L"who.exe", {L"--lookup"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_CONTAINS(r.stderr_text, "not supported on Windows");
}
