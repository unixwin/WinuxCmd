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
 *  - File: jq_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(jq, jq_basic_filter) {
  Pipeline p;
  p.set_stdin("{\"name\":\"test\"}\n");
  p.add(L"jq.exe", {L"."});

  TEST_LOG_CMD_LIST("jq.exe", L".");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("jq basic output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(jq, jq_field_access) {
  Pipeline p;
  p.set_stdin("{\"name\":\"test\"}\n");
  p.add(L"jq.exe", {L".name"});

  TEST_LOG_CMD_LIST("jq.exe", L".name");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("jq field access output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(jq, jq_raw_output) {
  Pipeline p;
  p.set_stdin("{\"name\":\"test\"}\n");
  p.add(L"jq.exe", {L"-r", L".name"});

  TEST_LOG_CMD_LIST("jq.exe", L"-r", L".name");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("jq raw output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(jq, jq_compact_output) {
  Pipeline p;
  p.set_stdin("{\"name\":\"test\"}\n");
  p.add(L"jq.exe", {L"-c", L"."});

  TEST_LOG_CMD_LIST("jq.exe", L"-c", L".");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("jq compact output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}
