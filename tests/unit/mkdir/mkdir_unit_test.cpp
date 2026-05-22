/*
 *  Copyright © 2026 WinuxCmd
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: mkdir_unit_test.cpp
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(mkdir, mkdir_basic) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkdir.exe", {L"test_dir"});

  TEST_LOG_CMD_LIST("mkdir.exe", L"test_dir");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("mkdir.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify the directory was created
  bool dir_exists = std::filesystem::exists(tmp.path / "test_dir") &&
                    std::filesystem::is_directory(tmp.path / "test_dir");
  EXPECT_TRUE(dir_exists);
}

TEST(mkdir, mkdir_p_parents) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkdir.exe", {L"-p", L"dir1/dir2/dir3"});

  TEST_LOG_CMD_LIST("mkdir.exe", L"-p", L"dir1/dir2/dir3");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("mkdir.exe -p output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify the nested directories were created
  bool dir_exists =
      std::filesystem::exists(tmp.path / "dir1" / "dir2" / "dir3") &&
      std::filesystem::is_directory(tmp.path / "dir1" / "dir2" / "dir3");
  EXPECT_TRUE(dir_exists);
}

TEST(mkdir, mkdir_multiple) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkdir.exe", {L"dir1", L"dir2", L"dir3"});

  TEST_LOG_CMD_LIST("mkdir.exe", L"dir1", L"dir2", L"dir3");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("mkdir.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify all directories were created
  bool dir1_exists = std::filesystem::exists(tmp.path / "dir1") &&
                     std::filesystem::is_directory(tmp.path / "dir1");
  bool dir2_exists = std::filesystem::exists(tmp.path / "dir2") &&
                     std::filesystem::is_directory(tmp.path / "dir2");
  bool dir3_exists = std::filesystem::exists(tmp.path / "dir3") &&
                     std::filesystem::is_directory(tmp.path / "dir3");
  EXPECT_TRUE(dir1_exists);
  EXPECT_TRUE(dir2_exists);
  EXPECT_TRUE(dir3_exists);
}

TEST(mkdir, mkdir_mode_readonly_sets_windows_attribute) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkdir.exe", {L"-m", L"555", L"readonly"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);

  auto dir = tmp.path / "readonly";
  DWORD attrs = GetFileAttributesW(dir.wstring().c_str());
  EXPECT_NE(attrs, INVALID_FILE_ATTRIBUTES);
  EXPECT_TRUE((attrs & FILE_ATTRIBUTE_READONLY) != 0);

  SetFileAttributesW(dir.wstring().c_str(),
                     attrs & ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY));
}

TEST(mkdir, mkdir_parents_mode_applies_to_leaf_only) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkdir.exe", {L"-p", L"-m", L"555", L"parent/leaf"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);

  auto parent = tmp.path / "parent";
  auto leaf = parent / "leaf";
  DWORD parent_attrs = GetFileAttributesW(parent.wstring().c_str());
  DWORD leaf_attrs = GetFileAttributesW(leaf.wstring().c_str());
  EXPECT_NE(parent_attrs, INVALID_FILE_ATTRIBUTES);
  EXPECT_NE(leaf_attrs, INVALID_FILE_ATTRIBUTES);
  EXPECT_FALSE((parent_attrs & FILE_ATTRIBUTE_READONLY) != 0);
  EXPECT_TRUE((leaf_attrs & FILE_ATTRIBUTE_READONLY) != 0);

  SetFileAttributesW(leaf.wstring().c_str(),
                     leaf_attrs & ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY));
}

TEST(mkdir, mkdir_accepts_context_placeholder) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkdir.exe", {L"-Z", L"ctx_dir"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::is_directory(tmp.path / "ctx_dir"));
}
