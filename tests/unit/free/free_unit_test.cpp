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
 *  - File: free_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(free, free_default) {
  Pipeline p;
  p.add(L"free.exe", {});

  TEST_LOG_CMD_LIST("free.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("free output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(free, free_bytes) {
  Pipeline p;
  p.add(L"free.exe", {L"-b"});

  TEST_LOG_CMD_LIST("free.exe", L"-b");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("free bytes output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(free, free_human_readable) {
  Pipeline p;
  p.add(L"free.exe", {L"-h"});

  TEST_LOG_CMD_LIST("free.exe", L"-h");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("free human readable output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(free, free_megabytes) {
  Pipeline p;
  p.add(L"free.exe", {L"-m"});

  TEST_LOG_CMD_LIST("free.exe", L"-m");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("free megabytes output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}
