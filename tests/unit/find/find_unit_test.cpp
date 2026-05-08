/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the “Software”), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: find_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(find, find_name_pattern) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "src");
  tmp.write("src/a.cpp", "");
  tmp.write("src/b.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"src", L"-name", L"*.cpp"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("src/a.cpp") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("src/b.txt") == std::string::npos);
}

TEST(find, find_iname_and_type) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "Dir");
  tmp.write("Dir/ReadMe.MD", "");
  tmp.write("Dir/file.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"Dir", L"-type", L"f", L"-iname", L"readme.*"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("Dir/ReadMe.MD") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file.txt") == std::string::npos);
}

TEST(find, find_maxdepth) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "a" / "b");
  tmp.write("a/top.txt", "");
  tmp.write("a/b/deep.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a", L"-maxdepth", L"1", L"-name", L"*.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("a/top.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("a/b/deep.txt") == std::string::npos);
}

TEST(find, find_missing_path_returns_error) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"not_exists", L"-name", L"*.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
}

TEST(find, find_unsupported_delete) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L".", L"-delete"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
}
