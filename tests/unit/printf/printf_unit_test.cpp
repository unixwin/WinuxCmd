/*
 *  Copyright © 2026 WinuxCmd
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
 *  - File: printf_unit_test.cpp
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(printf, printf_reuses_format_for_extra_arguments) {
  Pipeline p;
  p.add(L"printf.exe", {L"[%s]=%d\\n", L"one", L"1", L"two", L"2"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "[one]=1\n[two]=2\n");
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

TEST(printf, printf_reuses_format_with_missing_final_argument_defaults) {
  Pipeline p;
  p.add(L"printf.exe", {L"%s:%d\\n", L"one", L"9", L"two"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "one:9\ntwo:0\n");
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

TEST(printf, printf_interprets_format_escapes) {
  Pipeline p;
  p.add(L"printf.exe", {L"A\\n\\x42\\101\\t%s\\n", L"done"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "A\nBA\tdone\n");
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

TEST(printf, printf_percent_b_interprets_gnu_escapes_and_nul) {
  Pipeline p;
  p.add(L"printf.exe", {L"%b", L"hi\\n\\0\\x41\\0101"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, std::string("hi\n\0AA", 6));
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

TEST(printf, printf_backslash_c_stops_output_from_format) {
  Pipeline p;
  p.add(L"printf.exe", {L"%s\\c%s", L"left", L"right"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "left");
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

TEST(printf, printf_backslash_c_stops_output_from_percent_b) {
  Pipeline p;
  p.add(L"printf.exe", {L"%b%s", L"left\\cright", L"later"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "left");
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

TEST(printf, printf_respects_basic_width_and_precision) {
  Pipeline p;
  p.add(L"printf.exe",
        {L"|%6s|%-6.3s|%05d|%.2f|", L"cat", L"kitten", L"42", L"3.14159"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "|   cat|kit   |00042|3.14|");
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

TEST(printf, printf_warns_for_invalid_numeric_conversion) {
  Pipeline p;
  p.add(L"printf.exe", {L"%d:%g", L"abc", L"1.2tail"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "0:1.2");
  EXPECT_NE(r.stderr_text.find("expected a numeric value"), std::string::npos);
  EXPECT_NE(r.stderr_text.find("value not completely converted"),
            std::string::npos);
}
