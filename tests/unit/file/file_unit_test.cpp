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
 *  - File: file_unit_test.cpp
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(file, file_basic) {
  TempDir tmp;
  tmp.write("test.txt", "Hello, World!");
  tmp.write("script.py", "print('hello')");

  TEST_LOG_FILE_CONTENT("test.txt", "Hello, World!");
  TEST_LOG_FILE_CONTENT("script.py", "print('hello')");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"file.exe", {L"test.txt", L"script.py"});

  TEST_LOG_CMD_LIST("file.exe", L"test.txt", L"script.py");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("file.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Verify the output contains expected file types
  EXPECT_TRUE(r.stdout_text.find("ASCII text") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("Python script") != std::string::npos);
}

TEST(file, file_brief) {
  TempDir tmp;
  tmp.write("document.pdf", "%PDF-1.4");

  TEST_LOG_FILE_CONTENT("document.pdf", "%PDF-1.4");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"file.exe", {L"-b", L"document.pdf"});

  TEST_LOG_CMD_LIST("file.exe", L"-b", L"document.pdf");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("file.exe -b output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // In brief mode, should only show type without filename
  EXPECT_TRUE(r.stdout_text.find("PDF") != std::string::npos);
}

TEST(file, file_directory) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "subdir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"file.exe", {L"subdir"});

  TEST_LOG_CMD_LIST("file.exe", L"subdir");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("file.exe directory output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("directory") != std::string::npos);
}

TEST(file, file_nonexistent) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"file.exe", {L"nonexistent.txt"});

  TEST_LOG_CMD_LIST("file.exe", L"nonexistent.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("file.exe nonexistent output", r.stderr_text);

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("No such file") != std::string::npos);
}
