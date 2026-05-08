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
 *  - File: yes_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(yes, yes_default) {
  Pipeline p;
  p.add(L"yes.exe", {});
  p.add(L"head.exe", {L"-n", L"5"});  // Limit output to 5 lines

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should output "y" 5 times
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find("y") != std::string::npos);
}

TEST(yes, yes_custom_string) {
  Pipeline p;
  p.add(L"yes.exe", {L"hello"});
  p.add(L"head.exe", {L"-n", L"3"});  // Limit output to 3 lines

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should output "hello" 3 times
  EXPECT_TRUE(r.stdout_text.find("hello") != std::string::npos);
}
