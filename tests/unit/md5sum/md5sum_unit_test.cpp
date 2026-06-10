/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *  sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 *
 *  - File: md5sum_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(md5sum, md5sum_stdin) {
  Pipeline p;
  p.set_stdin("hello world\n");
  p.add(L"md5sum.exe", {});

  TEST_LOG_CMD_LIST("md5sum.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("md5sum output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(md5sum, md5sum_file) {
  TempDir tmp;
  tmp.write("test.txt", "hello world\n");

  TEST_LOG_FILE_CONTENT("test.txt", "hello world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"md5sum.exe", {L"test.txt"});

  TEST_LOG_CMD_LIST("md5sum.exe", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("md5sum file output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(md5sum, md5sum_binary) {
  Pipeline p;
  p.set_stdin("hello world\n");
  p.add(L"md5sum.exe", {L"-b"});

  TEST_LOG_CMD_LIST("md5sum.exe", L"-b");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("md5sum binary output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(md5sum, md5sum_wildcard) {
  TempDir tmp;
  tmp.write("file1.txt", "hello");
  tmp.write("file2.txt", "world");
  tmp.write("other.log", "log");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"md5sum.exe", {L"*.txt"});

  TEST_LOG_CMD_LIST("md5sum.exe", L"*.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("md5sum wildcard output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("file1.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file2.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("other.log") == std::string::npos);
}

TEST(md5sum, md5sum_text_mode) {
  Pipeline p;
  p.set_stdin("hello world\n");
  p.add(L"md5sum.exe", {L"-t"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Text mode should produce valid hash
  EXPECT_TRUE(r.stdout_text.find("md5sum") == std::string::npos);
}

TEST(md5sum, md5sum_tag) {
  Pipeline p;
  p.set_stdin("hello world\n");
  p.add(L"md5sum.exe", {L"--tag"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // --tag should output in BSD-style format
  EXPECT_TRUE(r.stdout_text.find("MD5") != std::string::npos);
}

TEST(md5sum, md5sum_short_zero_alias_uses_nul_terminator) {
  TempDir tmp;
  tmp.write("test.txt", "hello world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"md5sum.exe", {L"-z", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text,
            std::string("6f5902ac237024bdd0c176cb93063dc4  test.txt\0", 43));
}

TEST(md5sum, md5sum_quiet) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"md5sum.exe", {L"-q", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // GNU md5sum only uses --quiet in check mode.
  EXPECT_TRUE(r.stdout_text.find("test.txt") != std::string::npos);
}

TEST(md5sum, md5sum_check_valid) {
  TempDir tmp;
  tmp.write("check.txt", "hello world\n");
  // Get the hash first
  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"md5sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  // Write hash to check file
  std::string hash_line = r1.stdout_text.substr(0, 32) + "  check.txt";
  tmp.write("check.md5", hash_line);

  // Verify
  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"md5sum.exe", {L"-c", L"check.md5"});
  auto r2 = p2.run();

  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_TRUE(r2.stdout_text.find("check.txt: OK") != std::string::npos);
}

TEST(md5sum, md5sum_check_invalid) {
  TempDir tmp;
  tmp.write("check.txt", "hello world\n");
  tmp.write("check.md5", "00000000000000000000000000000000  check.txt");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"md5sum.exe", {L"-c", L"check.md5"});
  auto r = p.run();

  // Should fail with mismatched hash
  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("check.txt: FAILED") != std::string::npos);
}

TEST(md5sum, md5sum_strict) {
  TempDir tmp;
  tmp.write("strict.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"md5sum.exe", {L"--strict", L"strict.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(md5sum, md5sum_missing_input_reports_no_such_file) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"md5sum.exe", {L"missing.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "md5sum: cannot open 'missing.txt' for reading: No such "
                  "file or directory") != std::string::npos);
}

TEST(md5sum, md5sum_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("indir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"md5sum.exe", {L"indir"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(
      r.stderr_text.find("md5sum: cannot open 'indir' for reading: Is a directory") !=
      std::string::npos);
}

TEST(md5sum, md5sum_check_status_suppresses_output) {
  TempDir tmp;
  tmp.write("check.txt", "hello world\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"md5sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 32) + "  check.txt";
  tmp.write("check.md5", hash_line);

  Pipeline good;
  good.set_cwd(tmp.wpath());
  good.add(L"md5sum.exe", {L"--status", L"-c", L"check.md5"});
  auto good_result = good.run();

  EXPECT_EQ(good_result.exit_code, 0);
  EXPECT_EQ_TEXT(good_result.stdout_text, "");
  EXPECT_EQ_TEXT(good_result.stderr_text, "");

  tmp.write("check.md5", "00000000000000000000000000000000  check.txt");

  Pipeline bad;
  bad.set_cwd(tmp.wpath());
  bad.add(L"md5sum.exe", {L"--status", L"-c", L"check.md5"});
  auto bad_result = bad.run();

  EXPECT_NE(bad_result.exit_code, 0);
  EXPECT_EQ_TEXT(bad_result.stdout_text, "");
  EXPECT_EQ_TEXT(bad_result.stderr_text, "");
}

TEST(md5sum, md5sum_check_quiet_suppresses_ok_lines_only) {
  TempDir tmp;
  tmp.write("check.txt", "hello world\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"md5sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 32) + "  check.txt";
  tmp.write("check.md5", hash_line);

  Pipeline good;
  good.set_cwd(tmp.wpath());
  good.add(L"md5sum.exe", {L"--quiet", L"-c", L"check.md5"});
  auto good_result = good.run();

  EXPECT_EQ(good_result.exit_code, 0);
  EXPECT_EQ_TEXT(good_result.stdout_text, "");
  EXPECT_EQ_TEXT(good_result.stderr_text, "");

  tmp.write("check.md5", "00000000000000000000000000000000  check.txt");

  Pipeline bad;
  bad.set_cwd(tmp.wpath());
  bad.add(L"md5sum.exe", {L"--quiet", L"-c", L"check.md5"});
  auto bad_result = bad.run();

  EXPECT_NE(bad_result.exit_code, 0);
  EXPECT_TRUE(bad_result.stdout_text.find("check.txt: FAILED") !=
              std::string::npos);
  EXPECT_EQ_TEXT(bad_result.stderr_text, "");
}

TEST(md5sum, md5sum_check_warn_reports_malformed_line_location_and_summary) {
  TempDir tmp;
  tmp.write("check.txt", "hello world\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"md5sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 32) + "  check.txt";
  tmp.write("check.md5", "bad line\n" + hash_line + "\n");

  Pipeline check;
  check.set_cwd(tmp.wpath());
  check.add(L"md5sum.exe", {L"--warn", L"-c", L"check.md5"});
  auto result = check.run();

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_TRUE(result.stdout_text.find("check.txt: OK") != std::string::npos);
  EXPECT_TRUE(result.stderr_text.find(
                  "md5sum: check.md5: 1: improperly formatted checksum line") !=
              std::string::npos);
  EXPECT_TRUE(result.stderr_text.find(
                  "md5sum: WARNING: 1 line is improperly formatted") !=
              std::string::npos);
}

TEST(md5sum, md5sum_check_without_any_valid_lines_fails) {
  TempDir tmp;
  tmp.write("check.md5", "bad line\n");

  Pipeline check;
  check.set_cwd(tmp.wpath());
  check.add(L"md5sum.exe", {L"-c", L"check.md5"});
  auto result = check.run();

  EXPECT_NE(result.exit_code, 0);
  EXPECT_EQ_TEXT(result.stdout_text, "");
  EXPECT_TRUE(
      result.stderr_text.find("md5sum: check.md5: no properly formatted checksum lines found") !=
      std::string::npos);
}

TEST(md5sum, md5sum_check_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("checkdir");

  Pipeline check;
  check.set_cwd(tmp.wpath());
  check.add(L"md5sum.exe", {L"-c", L"checkdir"});
  auto result = check.run();

  EXPECT_EQ(result.exit_code, 1);
  EXPECT_TRUE(result.stderr_text.find(
                  "md5sum: cannot open 'checkdir' for reading: Is a directory") !=
              std::string::npos);
}

TEST(md5sum, md5sum_check_accepts_binary_marker_lines) {
  TempDir tmp;
  tmp.write("check.txt", "hello world\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"md5sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 32) + " *check.txt";
  tmp.write("check.md5", hash_line);

  Pipeline check;
  check.set_cwd(tmp.wpath());
  check.add(L"md5sum.exe", {L"-c", L"check.md5"});
  auto result = check.run();

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_TRUE(result.stdout_text.find("check.txt: OK") != std::string::npos);
}

TEST(md5sum, md5sum_check_strict_fails_when_any_line_is_malformed) {
  TempDir tmp;
  tmp.write("check.txt", "hello world\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"md5sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  std::string hash_line = r1.stdout_text.substr(0, 32) + "  check.txt";
  tmp.write("check.md5", hash_line + "\nnot-a-checksum\n");

  Pipeline check;
  check.set_cwd(tmp.wpath());
  check.add(L"md5sum.exe", {L"--strict", L"--warn", L"-c", L"check.md5"});
  auto result = check.run();

  EXPECT_NE(result.exit_code, 0);
  EXPECT_TRUE(result.stdout_text.find("check.txt: OK") != std::string::npos);
  EXPECT_TRUE(result.stderr_text.find(
                  "md5sum: check.md5: 2: improperly formatted checksum line") !=
              std::string::npos);
}

TEST(md5sum, md5sum_check_reports_unreadable_listed_files) {
  TempDir tmp;
  tmp.write("check.md5", "00000000000000000000000000000000  missing.txt\n");

  Pipeline check;
  check.set_cwd(tmp.wpath());
  check.add(L"md5sum.exe", {L"-c", L"check.md5"});
  auto result = check.run();

  EXPECT_NE(result.exit_code, 0);
  EXPECT_EQ_TEXT(result.stdout_text, "");
  EXPECT_TRUE(result.stderr_text.find("cannot open 'missing.txt' for reading") !=
              std::string::npos);
  EXPECT_TRUE(result.stderr_text.find(
                  "md5sum: WARNING: 1 listed file could not be read") !=
              std::string::npos);
}

TEST(md5sum, md5sum_check_ignore_missing_skips_missing_files) {
  TempDir tmp;
  tmp.write("check.md5", "00000000000000000000000000000000  missing.txt\n");

  Pipeline check;
  check.set_cwd(tmp.wpath());
  check.add(L"md5sum.exe",
            {L"--ignore-missing", L"-c", L"check.md5"});
  auto result = check.run();

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_EQ_TEXT(result.stdout_text, "");
  EXPECT_EQ_TEXT(result.stderr_text, "");
}
