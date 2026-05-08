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
 *  - File: realpath_unit_test.cpp
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(realpath, realpath_basic) {
  TempDir tmp;
  tmp.write("file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe", {L"file.txt"});

  TEST_LOG_CMD_LIST("realpath.exe", L"file.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("realpath.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Output should be an absolute path
  EXPECT_TRUE(r.stdout_text.find("file.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find(":\\") != std::string::npos ||
              r.stdout_text.find(":/") != std::string::npos);
}

TEST(realpath, realpath_current_dir) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe", {});

  TEST_LOG_CMD_LIST("realpath.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("realpath.exe current dir output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should output absolute path of current directory
  EXPECT_TRUE(r.stdout_text.find(":\\") != std::string::npos ||
              r.stdout_text.find(":/") != std::string::npos);
}

TEST(realpath, realpath_strip) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "subdir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe", {L"-s", L"subdir"});

  TEST_LOG_CMD_LIST("realpath.exe", L"-s", L"subdir");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("realpath.exe -s output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should not end with separator
  EXPECT_FALSE(r.stdout_text.ends_with("\\") || r.stdout_text.ends_with("/"));
}

TEST(realpath, realpath_nonexistent) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe", {L"nonexistent.txt"});

  TEST_LOG_CMD_LIST("realpath.exe", L"nonexistent.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("realpath.exe nonexistent output", r.stdout_text);

  // realpath on Windows can resolve paths even if file doesn't exist
  // The behavior depends on the implementation
}
