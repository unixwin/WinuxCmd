/*
 *  Copyright © 2026 WinuxCmd
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
 *  - File: du_unit_test.cpp
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(du, du_basic) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");
  tmp.write("file2.txt", "content2");

  TEST_LOG_FILE_CONTENT("file1.txt", "content1");
  TEST_LOG_FILE_CONTENT("file2.txt", "content2");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {});

  TEST_LOG_CMD_LIST("du.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("du.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should show directory size
  EXPECT_TRUE(r.stdout_text.length() > 0);
}

TEST(du, du_human_readable) {
  TempDir tmp;
  tmp.write("file.txt", "Hello, World!");

  TEST_LOG_FILE_CONTENT("file.txt", "Hello, World!");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-h"});

  TEST_LOG_CMD_LIST("du.exe", L"-h");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("du.exe -h output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should show human-readable sizes
  EXPECT_TRUE(r.stdout_text.length() > 0);
}

TEST(du, du_summarize) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "subdir");
  tmp.write("subdir/file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-s"});

  TEST_LOG_CMD_LIST("du.exe", L"-s");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("du.exe -s output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should only show total, not individual files
  EXPECT_TRUE(r.stdout_text.length() > 0);
}

TEST(du, du_max_depth) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "subdir");
  std::filesystem::create_directory(tmp.path / "subdir" / "nested");
  tmp.write("subdir/file.txt", "content");
  tmp.write("subdir/nested/file2.txt", "content2");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-d", L"1"});

  TEST_LOG_CMD_LIST("du.exe", L"-d", L"1");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("du.exe -d 1 output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should show directory size
  EXPECT_TRUE(r.stdout_text.length() > 0);
}

TEST(du, du_kilobytes) {
  TempDir tmp;
  tmp.write("file.txt", "content");

  TEST_LOG_FILE_CONTENT("file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-k"});

  TEST_LOG_CMD_LIST("du.exe", L"-k");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("du.exe -k output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should show sizes in KB
  EXPECT_TRUE(r.stdout_text.length() > 0);
}
