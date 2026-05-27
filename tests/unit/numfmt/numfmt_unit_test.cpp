/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(numfmt, numfmt_basic) {
  Pipeline p;
  p.set_stdin("1000\n2000\n");
  p.add(L"numfmt.exe", {});

  TEST_LOG_CMD_LIST("numfmt.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("numfmt output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(numfmt, numfmt_from_iec) {
  Pipeline p;
  p.set_stdin("1.5K\n2.0M\n");
  p.add(L"numfmt.exe", {L"--from=iec"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("1536") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("2097152") != std::string::npos);
}

TEST(numfmt, numfmt_to_iec) {
  Pipeline p;
  p.set_stdin("1536\n2097152\n");
  p.add(L"numfmt.exe", {L"--to=iec"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("1.5K") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("2.0M") != std::string::npos);
}

TEST(numfmt, numfmt_round_up) {
  Pipeline p;
  p.set_stdin("1.1K\n");
  p.add(L"numfmt.exe", {L"--from=iec", L"--round=up"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // 1.1K = 1126.4 bytes, rounded up should be 1127
  EXPECT_TRUE(r.stdout_text.find("1127") != std::string::npos);
}

TEST(numfmt, numfmt_round_down) {
  Pipeline p;
  p.set_stdin("1.9K\n");
  p.add(L"numfmt.exe", {L"--from=iec", L"--round=down"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // 1.9K = 1945.6 bytes, rounded down should be 1945
  EXPECT_TRUE(r.stdout_text.find("1945") != std::string::npos);
}

TEST(numfmt, numfmt_round_nearest) {
  Pipeline p;
  p.set_stdin("1.5K\n");
  p.add(L"numfmt.exe", {L"--from=iec", L"--round=nearest"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // 1.5K = 1536 bytes, nearest is 1536
  EXPECT_TRUE(r.stdout_text.find("1536") != std::string::npos);
}

TEST(numfmt, numfmt_padding) {
  Pipeline p;
  p.set_stdin("42\n");
  p.add(L"numfmt.exe", {L"--padding=10"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should have leading spaces
  EXPECT_TRUE(r.stdout_text.find("42") != std::string::npos);
}

TEST(numfmt, numfmt_header) {
  Pipeline p;
  p.set_stdin("HEADER\n1000\n2000\n");
  p.add(L"numfmt.exe", {L"--header=1"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("HEADER") != std::string::npos);
}

TEST(numfmt, numfmt_invalid_input) {
  Pipeline p;
  p.set_stdin("notanumber\n");
  p.add(L"numfmt.exe", {});
  auto r = p.run();

  // Should report error for invalid input
  EXPECT_NE(r.exit_code, 0);
}
