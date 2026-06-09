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
 *  - File: basename_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(basename, basename_simple) {
  Pipeline p;
  p.add(L"basename.exe", {L"C:\\Users\\test\\file.txt"});

  TEST_LOG_CMD_LIST("basename.exe", L"C:\\Users\\test\\file.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("basename output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "file.txt\n");
}

TEST(basename, basename_linux_path) {
  Pipeline p;
  p.add(L"basename.exe", {L"/home/user/test/file.txt"});

  TEST_LOG_CMD_LIST("basename.exe", L"/home/user/test/file.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("basename linux path output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "file.txt\n");
}

TEST(basename, basename_with_suffix) {
  Pipeline p;
  p.add(L"basename.exe", {L"C:\\Users\\test\\file.txt", L".txt"});

  TEST_LOG_CMD_LIST("basename.exe", L"C:\\Users\\test\\file.txt", L".txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("basename with suffix output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "file\n");
}

TEST(basename, basename_does_not_strip_suffix_identical_to_name) {
  Pipeline p;
  p.add(L"basename.exe", {L"/usr/bin/sort", L"sort"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "sort\n");
}

TEST(basename, basename_multiple_with_suffix_option) {
  Pipeline p;
  p.add(L"basename.exe",
        {L"--suffix=.h", L"include\\stdio.h", L"include\\stdlib.h"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "stdio\nstdlib\n");
}

TEST(basename, basename_accepts_abbreviated_multiple_long_option) {
  Pipeline p;
  p.add(L"basename.exe", {L"--mul", L"/foo/bar/baz", L"/foo/bar/baz"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "baz\nbaz\n");
}

TEST(basename, basename_accepts_abbreviated_suffix_long_option) {
  Pipeline p;
  p.add(L"basename.exe",
        {L"--suf", L".exe", L"/foo/bar/baz.exe", L"/foo/bar/baz.exe"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "baz\nbaz\n");
}

TEST(basename, basename_accepts_abbreviated_zero_long_option) {
  Pipeline p;
  p.add(L"basename.exe", {L"--ze", L"-a", L"/foo/bar/baz", L"/foo/bar/baz"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, std::string("baz\0baz\0", 8));
}

TEST(basename, basename_empty_short_suffix_still_implies_multiple) {
  Pipeline p;
  p.add(L"basename.exe",
        {L"-s", L"", L"include\\stdio.h", L"include\\stdlib.h"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "stdio.h\nstdlib.h\n");
}

TEST(basename, basename_empty_long_suffix_still_implies_multiple) {
  Pipeline p;
  p.add(L"basename.exe",
        {L"--suffix=", L"include\\stdio.h", L"include\\stdlib.h"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "stdio.h\nstdlib.h\n");
}

TEST(basename, basename_zero_terminates_each_result) {
  Pipeline p;
  p.add(L"basename.exe", {L"-z", L"-a", L"C:\\tmp\\one", L"/tmp/two"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, std::string("one\0two\0", 8));
}

TEST(basename, basename_root_slash_is_preserved) {
  Pipeline p;
  p.add(L"basename.exe", {L"/"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "/\n");
}

TEST(basename, basename_repeated_root_slashes_collapse_to_single_slash) {
  Pipeline p;
  p.add(L"basename.exe", {L"///"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "/\n");
}
TEST(basename, basename_double_slash_root_collapses_to_single_slash) {
  Pipeline p;
  p.add(L"basename.exe", {L"//"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "/\n");
}

TEST(basename, basename_missing_operand_reports_help_hint) {
  Pipeline p;
  p.add(L"basename.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "basename: missing operand\nTry 'basename --help' for more "
      "information.\n");
}

TEST(basename, basename_extra_operand_is_rejected_without_multiple_mode) {
  Pipeline p;
  p.add(L"basename.exe", {L"a", L"b", L"c"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "basename: extra operand 'c'\nTry 'basename --help' for more "
      "information.\n");
}
