/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(sha384sum, sha384sum_basic) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha384sum.exe", {L"test.txt"});

  TEST_LOG_CMD_LIST("sha384sum.exe", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("sha384sum output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(sha384sum, sha384sum_missing_input_reports_no_such_file) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha384sum.exe", {L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "sha384sum: cannot open 'missing.txt' for reading: No such "
                  "file or directory") != std::string::npos);
}

TEST(sha384sum, sha384sum_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("indir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha384sum.exe", {L"indir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(
      r.stderr_text.find("sha384sum: cannot open 'indir' for reading: Is a directory") !=
      std::string::npos);
}

TEST(sha384sum, sha384sum_stdin) {
  Pipeline p;
  p.set_stdin("hello world\n");
  p.add(L"sha384sum.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(sha384sum, sha384sum_binary) {
  Pipeline p;
  p.set_stdin("hello");
  p.add(L"sha384sum.exe", {L"-b"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(sha384sum, sha384sum_text) {
  Pipeline p;
  p.set_stdin("hello");
  p.add(L"sha384sum.exe", {L"-t"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(sha384sum, sha384sum_tag) {
  Pipeline p;
  p.set_stdin("hello");
  p.add(L"sha384sum.exe", {L"--tag"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("SHA384") != std::string::npos);
}

TEST(sha384sum, sha384sum_quiet) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha384sum.exe", {L"-q", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("test.txt") != std::string::npos);
}

TEST(sha384sum, sha384sum_check) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  // Get hash
  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"sha384sum.exe", {L"test.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  // Create check file
  std::string hash_line = r1.stdout_text.substr(0, 96) + "  test.txt";
  tmp.write("check.sha384", hash_line);

  // Verify
  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"sha384sum.exe", {L"-c", L"check.sha384"});
  auto r2 = p2.run();

  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_TRUE(r2.stdout_text.find("test.txt: OK") != std::string::npos);
}

TEST(sha384sum, sha384sum_check_invalid) {
  TempDir tmp;
  tmp.write("test.txt", "hello");
  tmp.write(
      "check.sha384",
      "000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000  test.txt");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha384sum.exe", {L"-c", L"check.sha384"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("test.txt: FAILED") != std::string::npos);
}

TEST(sha384sum, sha384sum_check_status_suppresses_output) {
  TempDir tmp;
  tmp.write("check.txt", "hello\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"sha384sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 96) + "  check.txt";
  tmp.write("check.sha384", hash_line);

  Pipeline good;
  good.set_cwd(tmp.wpath());
  good.add(L"sha384sum.exe", {L"--status", L"-c", L"check.sha384"});
  auto good_result = good.run();

  EXPECT_EQ(good_result.exit_code, 0);
  EXPECT_EQ_TEXT(good_result.stdout_text, "");
  EXPECT_EQ_TEXT(good_result.stderr_text, "");

  tmp.write(
      "check.sha384",
      "000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000  check.txt");

  Pipeline bad;
  bad.set_cwd(tmp.wpath());
  bad.add(L"sha384sum.exe", {L"--status", L"-c", L"check.sha384"});
  auto bad_result = bad.run();

  EXPECT_NE(bad_result.exit_code, 0);
  EXPECT_EQ_TEXT(bad_result.stdout_text, "");
  EXPECT_EQ_TEXT(bad_result.stderr_text, "");
}

TEST(sha384sum, sha384sum_check_quiet_suppresses_ok_lines_only) {
  TempDir tmp;
  tmp.write("check.txt", "hello\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"sha384sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 96) + "  check.txt";
  tmp.write("check.sha384", hash_line);

  Pipeline good;
  good.set_cwd(tmp.wpath());
  good.add(L"sha384sum.exe", {L"--quiet", L"-c", L"check.sha384"});
  auto good_result = good.run();

  EXPECT_EQ(good_result.exit_code, 0);
  EXPECT_EQ_TEXT(good_result.stdout_text, "");
  EXPECT_EQ_TEXT(good_result.stderr_text, "");

  tmp.write(
      "check.sha384",
      "000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000  check.txt");

  Pipeline bad;
  bad.set_cwd(tmp.wpath());
  bad.add(L"sha384sum.exe", {L"--quiet", L"-c", L"check.sha384"});
  auto bad_result = bad.run();

  EXPECT_NE(bad_result.exit_code, 0);
  EXPECT_TRUE(bad_result.stdout_text.find("check.txt: FAILED") !=
              std::string::npos);
  EXPECT_EQ_TEXT(bad_result.stderr_text, "");
}

TEST(sha384sum, sha384sum_check_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("checkdir");

  Pipeline check;
  check.set_cwd(tmp.wpath());
  check.add(L"sha384sum.exe", {L"-c", L"checkdir"});
  auto result = check.run();

  EXPECT_EQ(result.exit_code, 1);
  EXPECT_TRUE(result.stderr_text.find(
                  "sha384sum: cannot open 'checkdir' for reading: Is a directory") !=
              std::string::npos);
}

TEST(sha384sum, sha384sum_check_reports_unreadable_listed_files) {
  TempDir tmp;
  tmp.write(
      "check.sha384",
      "000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000  missing.txt\n");

  Pipeline check;
  check.set_cwd(tmp.wpath());
  check.add(L"sha384sum.exe", {L"-c", L"check.sha384"});
  auto result = check.run();

  EXPECT_NE(result.exit_code, 0);
  EXPECT_EQ_TEXT(result.stdout_text, "");
  EXPECT_TRUE(result.stderr_text.find("cannot open 'missing.txt' for reading") !=
              std::string::npos);
  EXPECT_TRUE(result.stderr_text.find(
                  "sha384sum: WARNING: 1 listed file could not be read") !=
              std::string::npos);
}

TEST(sha384sum, sha384sum_check_ignore_missing_skips_missing_files) {
  TempDir tmp;
  tmp.write(
      "check.sha384",
      "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000  missing.txt\n");

  Pipeline check;
  check.set_cwd(tmp.wpath());
  check.add(L"sha384sum.exe",
            {L"--ignore-missing", L"-c", L"check.sha384"});
  auto result = check.run();

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_EQ_TEXT(result.stdout_text, "");
  EXPECT_EQ_TEXT(result.stderr_text, "");
}

TEST(sha384sum, sha384sum_check_accepts_binary_marker_lines) {
  TempDir tmp;
  tmp.write("check.txt", "hello\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"sha384sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 96) + " *check.txt";
  tmp.write("check.sha384", hash_line);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha384sum.exe", {L"-c", L"check.sha384"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("check.txt: OK") != std::string::npos);
}

TEST(sha384sum, sha384sum_check_warn_reports_malformed_line_locations) {
  TempDir tmp;
  tmp.write("check.txt", "hello\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"sha384sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 96) + "  check.txt";
  tmp.write("check.sha384", "not-a-checksum\n" + hash_line + "\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha384sum.exe", {L"--warn", L"-c", L"check.sha384"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(
      r.stderr_text.find("sha384sum: check.sha384: 1: improperly formatted checksum line") !=
      std::string::npos);
  EXPECT_TRUE(r.stderr_text.find(
                  "sha384sum: WARNING: 1 line is improperly formatted") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("check.txt: OK") != std::string::npos);
}

TEST(sha384sum, sha384sum_check_without_valid_lines_reports_error) {
  TempDir tmp;
  tmp.write("check.sha384", "not-a-checksum\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha384sum.exe", {L"--warn", L"-c", L"check.sha384"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "");
  EXPECT_TRUE(
      r.stderr_text.find("sha384sum: check.sha384: no properly formatted checksum lines found") !=
      std::string::npos);
}

TEST(sha384sum, sha384sum_check_strict_rejects_malformed_lines) {
  TempDir tmp;
  tmp.write("check.txt", "hello\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"sha384sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 96) + "  check.txt";
  tmp.write("check.sha384", hash_line + "\nbad-line\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha384sum.exe", {L"--strict", L"-c", L"check.sha384"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("check.txt: OK") != std::string::npos);
}

TEST(sha384sum, sha384sum_wildcard) {
  TempDir tmp;
  tmp.write("a.txt", "hello");
  tmp.write("b.txt", "world");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha384sum.exe", {L"*.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("a.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b.txt") != std::string::npos);
}

TEST(sha384sum, sha384sum_short_zero_alias_uses_nul_terminator) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha384sum.exe", {L"-z", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(
      r.stdout_text,
      std::string(
          "9b71d224bd62f3785d96d46ad3ea3d73319bfbc2890caadae2"
          "dff72519673ca72323c3d99ba5c11d7c7acc6e14b8c5da  test.txt\0",
          107));
}
