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
 *  - File: unlink_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(unlink, unlink_basic) {
  TempDir tmp;
  tmp.write("test.txt", "hello world");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"unlink.exe", {L"test.txt"});

  TEST_LOG_CMD_LIST("unlink.exe", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(unlink, unlink_missing_operand) {
  Pipeline p;
  p.add(L"unlink.exe", {});

  TEST_LOG_CMD_LIST("unlink.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("unlink stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_FALSE(r.stderr_text.empty());
}

TEST(unlink, unlink_rejects_multiple_operands) {
  TempDir tmp;
  tmp.write("one.txt", "one");
  tmp.write("two.txt", "two");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"unlink.exe", {L"one.txt", L"two.txt"});

  TEST_LOG_CMD_LIST("unlink.exe", L"one.txt", L"two.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("unlink stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "one.txt"));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "two.txt"));
}
