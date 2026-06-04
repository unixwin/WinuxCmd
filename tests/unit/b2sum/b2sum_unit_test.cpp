/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(b2sum, b2sum_basic) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"b2sum.exe", {L"test.txt"});

  TEST_LOG_CMD_LIST("b2sum.exe", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("b2sum output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(b2sum, b2sum_stdin) {
  Pipeline p;
  p.set_stdin("hello world\n");
  p.add(L"b2sum.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(b2sum, b2sum_binary) {
  Pipeline p;
  p.set_stdin("hello");
  p.add(L"b2sum.exe", {L"-b"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(b2sum, b2sum_text) {
  Pipeline p;
  p.set_stdin("hello");
  p.add(L"b2sum.exe", {L"-t"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(b2sum, b2sum_tag) {
  Pipeline p;
  p.set_stdin("hello");
  p.add(L"b2sum.exe", {L"--tag"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("BLAKE2") != std::string::npos);
}

TEST(b2sum, b2sum_quiet) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"b2sum.exe", {L"-q", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("test.txt") == std::string::npos);
}

TEST(b2sum, b2sum_length) {
  Pipeline p;
  p.set_stdin("hello");
  p.add(L"b2sum.exe", {L"-l", L"128"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should output 128-bit hash (32 hex chars)
}

TEST(b2sum, b2sum_check) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  // Get hash
  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"b2sum.exe", {L"test.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  // Create check file
  std::string hash_line = r1.stdout_text.substr(0, 128) + "  test.txt";
  tmp.write("check.b2", hash_line);

  // Verify
  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"b2sum.exe", {L"-c", L"check.b2"});
  auto r2 = p2.run();

  EXPECT_EQ(r2.exit_code, 0);
}

TEST(b2sum, b2sum_wildcard) {
  TempDir tmp;
  tmp.write("a.txt", "hello");
  tmp.write("b.txt", "world");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"b2sum.exe", {L"*.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("a.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b.txt") != std::string::npos);
}
