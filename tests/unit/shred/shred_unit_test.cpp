/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(shred, shred_basic) {
  TempDir tmp;
  tmp.write("secret.txt", "sensitive data");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shred.exe", {L"-u", L"secret.txt"});

  TEST_LOG_CMD_LIST("shred.exe", L"-u", L"secret.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(shred, shred_overwrite_count) {
  TempDir tmp;
  tmp.write("data.txt", "some data to overwrite");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shred.exe", {L"-n", L"3", L"-u", L"data.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // File should be deleted after 3 overwrites
}

TEST(shred, shred_verbose) {
  TempDir tmp;
  tmp.write("verbose.txt", "verbose test data");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shred.exe", {L"-v", L"-n", L"1", L"-u", L"verbose.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Verbose mode should show progress
}

TEST(shred, shred_zero_final) {
  TempDir tmp;
  tmp.write("zero.txt", "zero final test");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shred.exe", {L"-z", L"-n", L"1", L"-u", L"zero.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(shred, shred_exact_mode) {
  TempDir tmp;
  tmp.write("exact.txt", "exact mode test data");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shred.exe", {L"--exact", L"-n", L"1", L"-u", L"exact.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // --exact should not round up to block boundaries
}

TEST(shred, shred_remove) {
  TempDir tmp;
  tmp.write("remove.txt", "remove test data");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shred.exe", {L"--remove", L"-n", L"1", L"remove.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(shred, shred_force) {
  TempDir tmp;
  tmp.write("force.txt", "force test data");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shred.exe", {L"-f", L"-n", L"1", L"-u", L"force.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(shred, shred_nonexistent_file) {
  Pipeline p;
  p.add(L"shred.exe", {L"-u", L"nonexistent_file_xyz.txt"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
}
