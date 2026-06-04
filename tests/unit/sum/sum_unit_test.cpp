/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(sum, sum_basic) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sum.exe", {L"test.txt"});

  TEST_LOG_CMD_LIST("sum.exe", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("sum output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "8403 1 test.txt\n");
}

TEST(sum, sum_sysv_option_uses_byte_sum_and_512_byte_blocks) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sum.exe", {L"-s", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "532 1 test.txt\n");
}

TEST(sum, sum_last_algorithm_option_wins) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sum.exe", {L"-s", L"-r", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "8403 1 test.txt\n");
}

TEST(sum, sum_block_count_depends_on_algorithm) {
  TempDir tmp;
  tmp.write("large.txt", std::string(1025, 'a'));

  Pipeline bsd;
  bsd.set_cwd(tmp.wpath());
  bsd.add(L"sum.exe", {L"large.txt"});
  auto bsd_result = bsd.run();

  Pipeline sysv;
  sysv.set_cwd(tmp.wpath());
  sysv.add(L"sum.exe", {L"--sysv", L"large.txt"});
  auto sysv_result = sysv.run();

  EXPECT_EQ(bsd_result.exit_code, 0);
  EXPECT_EQ(sysv_result.exit_code, 0);
  EXPECT_TRUE(bsd_result.stdout_text.find(" 2 large.txt") != std::string::npos);
  EXPECT_TRUE(sysv_result.stdout_text.find(" 3 large.txt") !=
              std::string::npos);
}

TEST(sum, sum_stdin_has_no_filename) {
  Pipeline p;
  p.set_stdin("hello");
  p.add(L"sum.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "8403 1\n");
}
