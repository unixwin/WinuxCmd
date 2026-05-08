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
 *  - File: less_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(less, less_file_basic) {
  TempDir tmp;
  tmp.write("test.txt", "line1\nline2\nline3\n");

  TEST_LOG_FILE_CONTENT("test.txt", "line1\nline2\nline3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"less.exe", {L"-F", L"test.txt"});

  TEST_LOG_CMD_LIST("less.exe", L"-F", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("less output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(less, less_stdin) {
  Pipeline p;
  p.set_stdin("line1\nline2\nline3\n");
  p.add(L"less.exe", {L"-F"});

  TEST_LOG_CMD_LIST("less.exe", L"-F");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("less stdin output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(less, less_quit_if_one_screen) {
  TempDir tmp;
  tmp.write("short.txt", "test\n");

  TEST_LOG_FILE_CONTENT("short.txt", "test\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"less.exe", {L"-F", L"short.txt"});

  TEST_LOG_CMD_LIST("less.exe", L"-F", L"short.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("less quit if one screen output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}
