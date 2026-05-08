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
 *  - File: tr_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(tr, tr_translate_simple) {
  Pipeline p;
  p.set_stdin("hello world");
  p.add(L"tr.exe", {L"a-z", L"A-Z"});

  TEST_LOG_CMD_LIST("tr.exe", L"a-z", L"A-Z");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tr output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "HELLO WORLD");
}

TEST(tr, tr_delete_characters) {
  Pipeline p;
  p.set_stdin("hello123world");
  p.add(L"tr.exe", {L"-d", L"0-9"});

  TEST_LOG_CMD_LIST("tr.exe", L"-d", L"0-9");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tr delete output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "helloworld");
}

TEST(tr, tr_squeeze_repeats) {
  Pipeline p;
  p.set_stdin("hello    world");
  p.add(L"tr.exe", {L"-s", L" "});

  TEST_LOG_CMD_LIST("tr.exe", L"-s", L" ");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tr squeeze output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "hello world");
}

TEST(tr, tr_delete_and_squeeze) {
  Pipeline p;
  p.set_stdin("aabbcc123  456");
  p.add(L"tr.exe", {L"-ds", L"0-9", L" "});

  TEST_LOG_CMD_LIST("tr.exe", L"-ds", L"0-9", L" ");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tr delete and squeeze output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "aabbcc");
}
