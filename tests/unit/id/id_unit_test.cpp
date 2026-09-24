// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(id, id_basic) {
  Pipeline p;
  p.add(L"id.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  // Should contain user and group information
  EXPECT_TRUE(r.stdout_text.find("uid") != std::string::npos);
}

TEST(id, id_user_only) {
  Pipeline p;
  p.add(L"id.exe", {L"-u"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_EQ(r.stdout_text.find("uid="), std::string::npos);
  std::string line = r.stdout_text;
  if (!line.empty() && line.back() == 10) line.pop_back();
  EXPECT_FALSE(line.empty());
  EXPECT_EQ(line.find_first_not_of("0123456789"), std::string::npos);
}

TEST(id, id_group_only) {
  Pipeline p;
  p.add(L"id.exe", {L"-g"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(id, id_groups) {
  Pipeline p;
  p.add(L"id.exe", {L"-G"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(id, id_name_requires_only_mode) {
  Pipeline p;
  p.add(L"id.exe", {L"-n"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("default format") != std::string::npos);
}

TEST(id, id_user_name) {
  Pipeline p;
  p.add(L"id.exe", {L"-un"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  // Should output username, not uid number
}

TEST(id, id_group_name) {
  Pipeline p;
  p.add(L"id.exe", {L"-gn"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  // Should output group name, not gid number
}

TEST(id, id_real_requires_only_mode) {
  Pipeline p;
  p.add(L"id.exe", {L"-r"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("default format") != std::string::npos);
}

TEST(id, id_real_user_outputs_numeric_id) {
  Pipeline p;
  p.add(L"id.exe", {L"-r", L"-u"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_EQ(r.stdout_text.find("uid="), std::string::npos);
}

TEST(id, id_zero_rejected_in_default_format) {
  Pipeline p;
  p.add(L"id.exe", {L"--zero"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("--zero") != std::string::npos);
}

TEST(id, id_specific_user) {
  Pipeline p;
  p.add(L"id.exe", {L"Administrator"});
  auto r = p.run();

  // May fail if user doesn't exist, but should handle gracefully
  EXPECT_TRUE(r.exit_code == 0 || r.exit_code == 1);
}

TEST(id, id_group_list_zero_delimited) {
  Pipeline p;
  p.add(L"id.exe", {L"-G", L"--zero"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_NE(r.stdout_text.find(static_cast<char>(0)), std::string::npos);
  EXPECT_EQ(r.stdout_text.back(), static_cast<char>(0));
}

TEST(id, id_rejects_multiple_only_modes) {
  Pipeline p;
  p.add(L"id.exe", {L"-u", L"-g"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("more than one") != std::string::npos);
}
