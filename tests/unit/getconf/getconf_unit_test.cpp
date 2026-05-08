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
 *  - File: getconf_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(getconf, getconf_all) {
  Pipeline p;
  p.add(L"getconf.exe", {L"-a"});

  TEST_LOG_CMD_LIST("getconf.exe", L"-a");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("getconf output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find("System configuration") != std::string::npos);
}

TEST(getconf, getconf_page_size) {
  Pipeline p;
  p.add(L"getconf.exe", {L"PAGE_SIZE"});

  TEST_LOG_CMD_LIST("getconf.exe", L"PAGE_SIZE");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("getconf output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(getconf, getconf_path_max) {
  Pipeline p;
  p.add(L"getconf.exe", {L"PATH_MAX"});

  TEST_LOG_CMD_LIST("getconf.exe", L"PATH_MAX");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("getconf output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "260\n");
}

TEST(getconf, getconf_nprocessors) {
  Pipeline p;
  p.add(L"getconf.exe", {L"NPROCESSORS_ONLN"});

  TEST_LOG_CMD_LIST("getconf.exe", L"NPROCESSORS_ONLN");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("getconf output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  int num = std::stoi(r.stdout_text);
  EXPECT_GT(num, 0);
}
