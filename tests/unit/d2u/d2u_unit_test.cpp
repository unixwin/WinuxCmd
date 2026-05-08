/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software", to
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
 *  - File: d2u_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(d2u, d2u_from_stdin) {
  Pipeline p;
  p.set_stdin("line1\r\nline2\r\nline3\r\n");
  p.add(L"d2u.exe", {});

  TEST_LOG_CMD_LIST("d2u.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("d2u output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "line1\nline2\nline3\n");
}

TEST(d2u, d2u_file) {
  TempDir tmp;
  tmp.write("dos.txt", "line1\r\nline2\r\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"d2u.exe", {L"dos.txt"});

  TEST_LOG_CMD_LIST("d2u.exe", L"dos.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(d2u, d2u_verbose) {
  TempDir tmp;
  tmp.write("dos.txt", "line1\r\nline2\r\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"d2u.exe", {L"-v", L"dos.txt"});

  TEST_LOG_CMD_LIST("d2u.exe", L"-v", L"dos.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("d2u stdout", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("converted") != std::string::npos);
}
