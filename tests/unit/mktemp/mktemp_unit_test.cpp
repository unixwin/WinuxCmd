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
 *  - File: mktemp_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(mktemp, mktemp_basic) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mktemp.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  // Remove trailing newline
  std::string filename = r.stdout_text;
  if (!filename.empty() && filename.back() == '\n') {
    filename.pop_back();
  }
  EXPECT_TRUE(std::filesystem::exists(tmp.path / filename));
}

TEST(mktemp, mktemp_custom_prefix) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mktemp.exe", {L"test_XXXXXX"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  std::string filename = r.stdout_text;
  if (!filename.empty() && filename.back() == '\n') {
    filename.pop_back();
  }
  EXPECT_TRUE(filename.find("test_") != std::string::npos);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / filename));
}

TEST(mktemp, mktemp_template_replaces_x_run_before_existing_suffix) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mktemp.exe", {L"file-XXXX.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  std::string filename = r.stdout_text;
  if (!filename.empty() && filename.back() == '\n') {
    filename.pop_back();
  }
  EXPECT_TRUE(filename.starts_with("file-"));
  EXPECT_TRUE(filename.ends_with(".txt"));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / filename));
}

TEST(mktemp, mktemp_dry_run) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mktemp.exe", {L"-u", L"test_XXXXXX"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  // With -u, file should not actually be created
  std::string filename = r.stdout_text;
  if (!filename.empty() && filename.back() == '\n') {
    filename.pop_back();
  }
  EXPECT_FALSE(std::filesystem::exists(tmp.path / filename));
}

TEST(mktemp, mktemp_suffix_appends_to_template) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mktemp.exe", {L"--suffix=.txt", L"file-XXXX"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  std::string filename = r.stdout_text;
  if (!filename.empty() && filename.back() == '\n') {
    filename.pop_back();
  }
  EXPECT_TRUE(filename.starts_with("file-"));
  EXPECT_TRUE(filename.ends_with(".txt"));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / filename));
}

TEST(mktemp, mktemp_suffix_rejects_directory_separator) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mktemp.exe", {L"--suffix=dir\\bad", L"file-XXXX"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ(r.stdout_text, "");
  EXPECT_TRUE(r.stderr_text.find(
                  "mktemp: invalid suffix 'dir\\bad', contains directory separator") !=
              std::string::npos);
}

TEST(mktemp, mktemp_tmpdir_prints_path_not_just_basename) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "out");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mktemp.exe", {L"-p", L"out", L"file-XXXX"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  std::string created = r.stdout_text;
  if (!created.empty() && created.back() == '\n') {
    created.pop_back();
  }
  EXPECT_TRUE(created.starts_with("out\\") || created.starts_with("out/"));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / std::filesystem::u8path(created)));
}

TEST(mktemp, mktemp_template_with_directory_prints_relative_path) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "nested");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mktemp.exe", {L"nested\\file-XXXX"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  std::string created = r.stdout_text;
  if (!created.empty() && created.back() == '\n') {
    created.pop_back();
  }
  EXPECT_TRUE(created.starts_with("nested\\") || created.starts_with("nested/"));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / std::filesystem::u8path(created)));
}
