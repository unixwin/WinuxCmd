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
 *  - File: ptx_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(ptx, ptx_basic_input) {
  Pipeline p;
  p.set_stdin("hello world\nfoo bar\n");
  p.add(L"ptx.exe", {});

  TEST_LOG_CMD_LIST("ptx.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ptx.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(ptx, ptx_file_input) {
  TempDir tmp;
  tmp.write("a.txt", "hello world\nfoo bar\n");

  TEST_LOG_FILE_CONTENT("a.txt", "hello world\nfoo bar\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ptx.exe", {L"a.txt"});

  TEST_LOG_CMD_LIST("ptx.exe", L"a.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ptx.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}
