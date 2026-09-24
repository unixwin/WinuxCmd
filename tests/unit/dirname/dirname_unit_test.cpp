// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(dirname, dirname_simple) {
  Pipeline p;
  p.add(L"dirname.exe", {L"C:\\Users\\test\\file.txt"});

  TEST_LOG_CMD_LIST("dirname.exe", L"C:\\Users\\test\\file.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("dirname output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "C:\\Users\\test\n");
}

TEST(dirname, dirname_linux_path) {
  Pipeline p;
  p.add(L"dirname.exe", {L"/home/user/test/file.txt"});

  TEST_LOG_CMD_LIST("dirname.exe", L"/home/user/test/file.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("dirname linux path output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "/home/user/test\n");
}

TEST(dirname, dirname_top_level_absolute_path_returns_root) {
  Pipeline p;
  p.add(L"dirname.exe", {L"/usr/"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "/\n");
}

TEST(dirname, dirname_no_slash) {
  Pipeline p;
  p.add(L"dirname.exe", {L"file.txt"});

  TEST_LOG_CMD_LIST("dirname.exe", L"file.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("dirname no slash output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, ".\n");
}

TEST(dirname, dirname_double_slash_root_preserves_msys_unc_root) {
  std::vector<std::wstring> args;
  args.push_back(L"//");
  Pipeline p;
  p.add(L"dirname.exe", args);

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "//\n");
}

TEST(dirname, dirname_drive_paths_preserve_drive_prefix) {
  std::wstring drive_root = L"C:";
  drive_root.push_back(92);
  std::wstring drive_file = drive_root + L"foo";
  std::vector<std::wstring> args;
  args.push_back(L"C:");
  args.push_back(L"C:.");
  args.push_back(L"C:foo");
  args.push_back(L"C:/foo");
  args.push_back(drive_root);
  args.push_back(drive_file);
  Pipeline p;
  p.add(L"dirname.exe", args);

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "C:\nC:\nC:\nC:\nC:\nC:\n");
}

TEST(dirname, dirname_unc_paths_preserve_double_separator_root) {
  std::wstring bs_root;
  bs_root.push_back(92);
  bs_root.push_back(92);
  std::wstring bs_server = bs_root + L"server";
  std::wstring bs_file = bs_server;
  bs_file.push_back(92);
  bs_file += L"share";
  bs_file.push_back(92);
  bs_file += L"file";
  std::vector<std::wstring> args;
  args.push_back(L"//server");
  args.push_back(L"//server/share");
  args.push_back(bs_server);
  args.push_back(bs_file);
  Pipeline p;
  p.add(L"dirname.exe", args);

  auto r = p.run();

  std::string expected = "//\n//server\n";
  expected.push_back(92);
  expected.push_back(92);
  expected.push_back(10);
  expected.push_back(92);
  expected.push_back(92);
  expected += "server";
  expected.push_back(92);
  expected += "share\n";
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, expected);
}
TEST(dirname, dirname_zero_terminates_each_result) {
  Pipeline p;
  p.add(L"dirname.exe",
        {L"-z", L"C:\\Users\\test\\file.txt", L"/home/user/test/file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  std::string expected("C:\\Users\\test", 13);
  expected.push_back('\0');
  expected += "/home/user/test";
  expected.push_back('\0');
  EXPECT_EQ(r.stdout_text, expected);
}

TEST(dirname, dirname_missing_operand_reports_help_hint) {
  Pipeline p;
  p.add(L"dirname.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "dirname: missing operand\nTry 'dirname --help' for more information.\n");
}
