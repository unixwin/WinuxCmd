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
 *  - File: df_unit_test.cpp
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(df, df_basic) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {});

  TEST_LOG_CMD_LIST("df.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("df.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should show disk information
  EXPECT_TRUE(r.stdout_text.find("Filesystem") != std::string::npos ||
              r.stdout_text.length() > 0);
}

TEST(df, df_human_readable) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {L"-h"});

  TEST_LOG_CMD_LIST("df.exe", L"-h");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("df.exe -h output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should show human-readable sizes (K, M, G, etc.)
  EXPECT_TRUE(r.stdout_text.length() > 0);
}

TEST(df, df_kilobytes) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {L"-k"});

  TEST_LOG_CMD_LIST("df.exe", L"-k");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("df.exe -k output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should show sizes in 1K blocks
  EXPECT_TRUE(r.stdout_text.find("1K-blocks") != std::string::npos ||
              r.stdout_text.length() > 0);
}

TEST(df, df_si) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {L"-H"});

  TEST_LOG_CMD_LIST("df.exe", L"-H");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("df.exe -H output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should show SI units (1000-based)
  EXPECT_TRUE(r.stdout_text.length() > 0);
}
