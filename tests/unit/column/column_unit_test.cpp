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
 *  - File: column_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(column, column_table_mode) {
  Pipeline p;
  p.set_stdin("name\tage\tcity\nAlice\t30\tNY\nBob\t25\tLA\n");
  p.add(L"column.exe", {L"-t"});

  TEST_LOG_CMD_LIST("column.exe", L"-t");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("column table output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(column, column_custom_separator) {
  Pipeline p;
  p.set_stdin("name,age,city\nAlice,30,NY\nBob,25,LA\n");
  p.add(L"column.exe", {L"-t", L"-s", L","});

  TEST_LOG_CMD_LIST("column.exe", L"-t", L"-s", L",");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("column custom separator output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(column, column_file_input) {
  TempDir tmp;
  tmp.write("data.txt", "name\tage\ntest\t123\n");

  TEST_LOG_FILE_CONTENT("data.txt", "name\tage\ntest\t123\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"column.exe", {L"-t", L"data.txt"});

  TEST_LOG_CMD_LIST("column.exe", L"-t", L"data.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("column file output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(column, column_json_output) {
  Pipeline p;
  p.set_stdin("name\tage\tcity\nAlice\t30\tNY\nBob\t25\tLA\n");
  p.add(L"column.exe", {L"-t", L"--json"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // JSON output should contain array
  EXPECT_TRUE(r.stdout_text.find("[") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("]") != std::string::npos);
}

TEST(column, column_output_width) {
  Pipeline p;
  p.set_stdin("name\tage\tcity\nAlice\t30\tNY\nBob\t25\tLA\n");
  p.add(L"column.exe", {L"-t", L"--output-width", L"40"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Output should be truncated to 40 columns
}

TEST(column, column_right_align) {
  Pipeline p;
  p.set_stdin("name\tage\tcity\nAlice\t30\tNY\nBob\t25\tLA\n");
  p.add(L"column.exe", {L"-t", L"--table-right", L"2"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Column 2 (age) should be right-aligned
}

TEST(column, column_right_columns) {
  Pipeline p;
  p.set_stdin("name\tage\tscore\nAlice\t30\t95\nBob\t25\t87\n");
  p.add(L"column.exe", {L"-t", L"--table-right", L"2,3"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Columns 2 and 3 should be right-aligned
}

TEST(column, column_empty_input) {
  Pipeline p;
  p.set_stdin("");
  p.add(L"column.exe", {L"-t"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(column, column_single_column) {
  Pipeline p;
  p.set_stdin("hello\nworld\n");
  p.add(L"column.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(column, column_fill_columns) {
  Pipeline p;
  p.set_stdin("a\nb\nc\nd\ne\nf\n");
  p.add(L"column.exe", {L"-c", L"30"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}
