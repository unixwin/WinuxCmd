// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(ptx, ptx_basic_input) {
  Pipeline p;
  p.set_stdin("hello world\nfoo bar\n");
  p.add(L"ptx.exe", {});

  TEST_LOG_CMD_LIST("ptx.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ptx.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(ptx, ptx_default_width_matches_gnu_alignment) {
  Pipeline p;
  p.set_stdin("alpha beta\n");
  p.add(L"ptx.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, std::string(39, ' ') + "alpha beta\n" +
                                    std::string(31, ' ') + "alpha   beta\n");
}

TEST(ptx, ptx_file_input) {
  TempDir tmp;
  tmp.write("a.txt", "hello world\nfoo bar\n");

  TEST_LOG_FILE_CONTENT("a.txt", "hello world\nfoo bar\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ptx.exe", {L"a.txt"});

  TEST_LOG_CMD_LIST("ptx.exe", L"a.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ptx.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(ptx, ptx_context_does_not_cross_line_boundaries) {
  TempDir tmp;
  tmp.write("a.txt", "alpha one\nbeta two\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ptx.exe", {L"-w", L"80", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("alpha one beta") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("beta two alpha") == std::string::npos);
}

TEST(ptx, ptx_auto_reference) {
  TempDir tmp;
  tmp.write("a.txt", "hello world\nfoo bar\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ptx.exe", {L"-A", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Auto-reference should include line numbers
}

TEST(ptx, ptx_ignore_file) {
  TempDir tmp;
  tmp.write("a.txt", "hello world\nfoo bar\n");
  tmp.write("ignore.txt", "foo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ptx.exe", {L"-i", L"ignore.txt", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Words from ignore file should be excluded
}

TEST(ptx, ptx_gnu_extensions) {
  TempDir tmp;
  tmp.write("a.txt", "hello world\nfoo bar\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ptx.exe", {L"-G", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(ptx, ptx_format_roff) {
  TempDir tmp;
  tmp.write("a.txt", "hello world\nfoo bar\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ptx.exe", {L"-R", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // RoFF format output
}

TEST(ptx, ptx_format_tex) {
  TempDir tmp;
  tmp.write("a.txt", "hello world\nfoo bar\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ptx.exe", {L"-T", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // TeX format output
}

TEST(ptx, ptx_width) {
  TempDir tmp;
  tmp.write("a.txt", "hello world\nfoo bar\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ptx.exe", {L"-w", L"80", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(ptx, ptx_break_file) {
  TempDir tmp;
  tmp.write("a.txt", "hello world\nfoo bar\n");
  tmp.write("break.txt", " \n\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ptx.exe", {L"-b", L"break.txt", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(ptx, ptx_flag_truncation) {
  TempDir tmp;
  tmp.write("a.txt", "hello world\nfoo bar\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ptx.exe", {L"-F", L"X", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(ptx, ptx_macro_name) {
  TempDir tmp;
  tmp.write("a.txt", "hello world\nfoo bar\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ptx.exe", {L"-M", L"MYMACRO", L"-R", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}
