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
 *  - File: touch_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

namespace {
struct FileTimes {
  FILETIME access{};
  FILETIME write{};
};

auto read_file_times(const std::filesystem::path &path) -> FileTimes {
  HANDLE h =
      CreateFileW(path.wstring().c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  EXPECT_TRUE(h != INVALID_HANDLE_VALUE);

  FILETIME create{}, access{}, write{};
  EXPECT_TRUE(GetFileTime(h, &create, &access, &write) != 0);
  CloseHandle(h);
  return FileTimes{access, write};
}

auto filetime_to_utc(const FILETIME &ft) -> SYSTEMTIME {
  SYSTEMTIME st{};
  EXPECT_TRUE(FileTimeToSystemTime(&ft, &st) != 0);
  return st;
}

auto filetime_to_local(const FILETIME &ft) -> SYSTEMTIME {
  FILETIME local_ft{};
  SYSTEMTIME st{};
  EXPECT_TRUE(FileTimeToLocalFileTime(&ft, &local_ft) != 0);
  EXPECT_TRUE(FileTimeToSystemTime(&local_ft, &st) != 0);
  return st;
}

auto filetime_ticks(const FILETIME &ft) -> unsigned long long {
  ULARGE_INTEGER uli{};
  uli.LowPart = ft.dwLowDateTime;
  uli.HighPart = ft.dwHighDateTime;
  return uli.QuadPart;
}
}  // namespace

TEST(touch, touch_creates_file) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"touch.exe", {L"new.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "new.txt"));
}

TEST(touch, touch_no_create_option) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"touch.exe", {L"-c", L"missing.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(!std::filesystem::exists(tmp.path / "missing.txt"));
}

TEST(touch, touch_reference_updates_target_time) {
  TempDir tmp;
  tmp.write("ref.txt", "ref");
  tmp.write("target.txt", "target");

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"touch.exe", {L"-r", L"ref.txt", L"target.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);

  auto ref_time = std::filesystem::last_write_time(tmp.path / "ref.txt");
  auto target_time = std::filesystem::last_write_time(tmp.path / "target.txt");
  EXPECT_EQ(ref_time, target_time);
}

TEST(touch, touch_date_sets_fixed_utc_time) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"touch.exe", {L"-d", L"2024-01-02 03:04:05 UTC", L"fixed.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);

  auto st = filetime_to_utc(read_file_times(tmp.path / "fixed.txt").write);
  EXPECT_EQ(st.wYear, 2024);
  EXPECT_EQ(st.wMonth, 1);
  EXPECT_EQ(st.wDay, 2);
  EXPECT_EQ(st.wHour, 3);
  EXPECT_EQ(st.wMinute, 4);
  EXPECT_EQ(st.wSecond, 5);
}

TEST(touch, touch_t_timestamp_uses_local_time) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"touch.exe", {L"-t", L"202403040506.07", L"stamp.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);

  auto st = filetime_to_local(read_file_times(tmp.path / "stamp.txt").write);
  EXPECT_EQ(st.wYear, 2024);
  EXPECT_EQ(st.wMonth, 3);
  EXPECT_EQ(st.wDay, 4);
  EXPECT_EQ(st.wHour, 5);
  EXPECT_EQ(st.wMinute, 6);
  EXPECT_EQ(st.wSecond, 7);
}

TEST(touch, touch_time_access_leaves_modify_time) {
  TempDir tmp;
  tmp.write("target.txt", "target");

  Pipeline set_modify;
  set_modify.set_cwd(tmp.wpath());
  set_modify.add(L"touch.exe",
                 {L"-d", L"2024-01-02 03:04:05 UTC", L"target.txt"});
  EXPECT_EQ(set_modify.run().exit_code, 0);

  auto before = read_file_times(tmp.path / "target.txt");

  Pipeline set_access;
  set_access.set_cwd(tmp.wpath());
  set_access.add(L"touch.exe", {L"--time=access", L"-d",
                                L"2024-02-03 04:05:06 UTC", L"target.txt"});
  EXPECT_EQ(set_access.run().exit_code, 0);

  auto after = read_file_times(tmp.path / "target.txt");
  EXPECT_EQ(filetime_ticks(after.write), filetime_ticks(before.write));

  auto access = filetime_to_utc(after.access);
  EXPECT_EQ(access.wYear, 2024);
  EXPECT_EQ(access.wMonth, 2);
  EXPECT_EQ(access.wDay, 3);
  EXPECT_EQ(access.wHour, 4);
  EXPECT_EQ(access.wMinute, 5);
  EXPECT_EQ(access.wSecond, 6);
}

TEST(touch, touch_reference_is_origin_for_relative_date) {
  TempDir tmp;

  Pipeline ref;
  ref.set_cwd(tmp.wpath());
  ref.add(L"touch.exe", {L"-d", L"2024-01-02 03:04:05 UTC", L"ref.txt"});
  EXPECT_EQ(ref.run().exit_code, 0);

  Pipeline target;
  target.set_cwd(tmp.wpath());
  target.add(L"touch.exe",
             {L"-r", L"ref.txt", L"-d", L"+1 day", L"target.txt"});
  EXPECT_EQ(target.run().exit_code, 0);

  auto st = filetime_to_utc(read_file_times(tmp.path / "target.txt").write);
  EXPECT_EQ(st.wYear, 2024);
  EXPECT_EQ(st.wMonth, 1);
  EXPECT_EQ(st.wDay, 3);
  EXPECT_EQ(st.wHour, 3);
  EXPECT_EQ(st.wMinute, 4);
  EXPECT_EQ(st.wSecond, 5);
}
