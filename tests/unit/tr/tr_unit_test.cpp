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

TEST(tr, tr_translate_character_classes) {
  Pipeline p;
  p.set_stdin("abc xyz 123");
  p.add(L"tr.exe", {L"[:lower:]", L"[:upper:]"});

  TEST_LOG_CMD_LIST("tr.exe", L"[:lower:]", L"[:upper:]");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tr class translate output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "ABC XYZ 123");
}

TEST(tr, tr_octal_escape_uses_all_digits) {
  Pipeline p;
  p.set_stdin("abc");
  p.add(L"tr.exe", {L"\\141", L"X"});

  TEST_LOG_CMD_LIST("tr.exe", L"\\141", L"X");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tr octal escape output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "Xbc");
}

TEST(tr, tr_delete_digit_class) {
  Pipeline p;
  p.set_stdin("a1b23c456");
  p.add(L"tr.exe", {L"-d", L"[:digit:]"});

  TEST_LOG_CMD_LIST("tr.exe", L"-d", L"[:digit:]");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tr digit class delete output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "abc");
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

TEST(tr, tr_squeeze_space_class) {
  Pipeline p;
  p.set_stdin("a   b\t\tc\n\n");
  p.add(L"tr.exe", {L"-s", L"[:space:]"});

  TEST_LOG_CMD_LIST("tr.exe", L"-s", L"[:space:]");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tr space class squeeze output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a b\tc\n");
}

TEST(tr, tr_translate_then_squeeze_last_set) {
  Pipeline p;
  p.set_stdin("aaabbbccc");
  p.add(L"tr.exe", {L"-s", L"[:lower:]", L"[:upper:]"});

  TEST_LOG_CMD_LIST("tr.exe", L"-s", L"[:lower:]", L"[:upper:]");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tr translate squeeze output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "ABC");
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
  EXPECT_EQ_TEXT(r.stdout_text, "aabbcc ");
}

TEST(tr, tr_delete_and_squeeze_preserves_single_squeeze_char) {
  Pipeline p;
  p.set_stdin("a1 b");
  p.add(L"tr.exe", {L"-ds", L"[:digit:]", L" "});

  TEST_LOG_CMD_LIST("tr.exe", L"-ds", L"[:digit:]", L" ");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tr delete and squeeze single output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a b");
}
