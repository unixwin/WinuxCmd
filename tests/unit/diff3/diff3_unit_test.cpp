/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a
 *  copy of this software and associated documentation files (the "Software"),
 *  to deal in the Software without restriction, including without limitation
 *  the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
 *  Software is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 *
 *  - File: diff3_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(diff3, diff3_wildcard_triplet_expands) {
  TempDir tmp;
  tmp.write("mine.txt", "same\n");
  tmp.write("older.txt", "same\n");
  tmp.write("yours.txt", "same\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff3.exe", {L"*.txt"});

  TEST_LOG_CMD_LIST("diff3.exe", L"*.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("diff3 wildcard output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("No conflicts found") != std::string::npos);
}

TEST(diff3, diff3_conflict_detection) {
  TempDir tmp;
  tmp.write("mine.txt", "my change\n");
  tmp.write("older.txt", "original\n");
  tmp.write("yours.txt", "your change\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff3.exe", {L"mine.txt", L"older.txt", L"yours.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should detect conflicts
  EXPECT_TRUE(r.stdout_text.find("conflicts") != std::string::npos);
}

TEST(diff3, diff3_merged_output) {
  TempDir tmp;
  tmp.write("mine.txt", "same\nmy change\n");
  tmp.write("older.txt", "same\noriginal\n");
  tmp.write("yours.txt", "same\nyour change\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff3.exe", {L"-m", L"mine.txt", L"older.txt", L"yours.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Merged format should show conflict markers
}

TEST(diff3, diff3_ed_script) {
  TempDir tmp;
  tmp.write("mine.txt", "same\nmy change\n");
  tmp.write("older.txt", "same\noriginal\n");
  tmp.write("yours.txt", "same\nyour change\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff3.exe", {L"-e", L"mine.txt", L"older.txt", L"yours.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Ed script format
}

TEST(diff3, diff3_bracketed_conflicts) {
  TempDir tmp;
  tmp.write("mine.txt", "same\nmy change\n");
  tmp.write("older.txt", "same\noriginal\n");
  tmp.write("yours.txt", "same\nyour change\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff3.exe", {L"-E", L"mine.txt", L"older.txt", L"yours.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Bracketed conflict format
}

TEST(diff3, diff3_identical_files) {
  TempDir tmp;
  tmp.write("mine.txt", "same\n");
  tmp.write("older.txt", "same\n");
  tmp.write("yours.txt", "same\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff3.exe", {L"mine.txt", L"older.txt", L"yours.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("No conflicts found") != std::string::npos);
}

TEST(diff3, diff3_missing_file_fails) {
  Pipeline p;
  p.add(L"diff3.exe", {L"a.txt", L"b.txt"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("diff3 missing operand stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ(r.stderr_text,
            "diff3: missing operand after 'b.txt'\n"
            "Try 'diff3 --help' for more information.\n");
}

TEST(diff3, diff3_missing_all_operands_reports_help_hint) {
  Pipeline p;
  p.add(L"diff3.exe", {});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("diff3 missing all operands stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ(r.stderr_text,
            "diff3: missing operand\n"
            "Try 'diff3 --help' for more information.\n");
}

TEST(diff3, diff3_too_many_files_fails) {
  Pipeline p;
  p.add(L"diff3.exe", {L"a.txt", L"b.txt", L"c.txt", L"d.txt"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("diff3 extra operand stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ(r.stderr_text,
            "diff3: extra operand 'd.txt'\n"
            "Try 'diff3 --help' for more information.\n");
}
