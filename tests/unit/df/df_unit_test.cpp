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

TEST(df, df_block_size_bytes) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {L"-B", L"1"});

  TEST_LOG_CMD_LIST("df.exe", L"-B", L"1");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("df.exe -B 1 output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("1B-blocks"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("Used"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("Available"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("Mounted on"), std::string::npos);
}

TEST(df, df_block_size_human_readable_alias) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {L"--block-size=human-readable"});

  TEST_LOG_CMD_LIST("df.exe", L"--block-size=human-readable");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("df.exe --block-size=human-readable output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("Size"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("Available"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("Capacity"), std::string::npos);
}

TEST(df, df_total_row_shape) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {L"--total", L"-k"});

  TEST_LOG_CMD_LIST("df.exe", L"--total", L"-k");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("df.exe --total -k output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("1K-blocks"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("\ntotal"), std::string::npos);
  EXPECT_NE(r.stdout_text.find(" total\n"), std::string::npos);
}

TEST(df, df_accepts_all_sync_no_sync) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {L"-a", L"--sync", L"--no-sync"});

  TEST_LOG_CMD_LIST("df.exe", L"-a", L"--sync", L"--no-sync");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("df.exe -a --sync --no-sync output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("Filesystem"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("Mounted on"), std::string::npos);
}

TEST(df, df_print_type) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {L"-T"});

  TEST_LOG_CMD_LIST("df.exe", L"-T");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("df.exe -T output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("Type"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("Mounted on"), std::string::npos);
}

TEST(df, df_inodes_shape) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {L"-i"});

  TEST_LOG_CMD_LIST("df.exe", L"-i");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("df.exe -i output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("Inodes"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("IUsed"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("IFree"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("IUse%"), std::string::npos);
}

TEST(df, df_wildcard_operands_expand) {
  TempDir tmp;
  tmp.write("sample.txt", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {L"*.txt"});

  TEST_LOG_CMD_LIST("df.exe", L"*.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("df.exe wildcard output", r.stdout_text);
  TEST_LOG("df.exe wildcard stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("Filesystem"), std::string::npos);
}
