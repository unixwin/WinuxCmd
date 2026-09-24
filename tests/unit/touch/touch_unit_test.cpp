// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

namespace {
struct FileTimes {
  FILETIME access{};
  FILETIME write{};
};

auto file_open_flags(bool no_dereference = false) -> DWORD {
  DWORD flags = FILE_FLAG_BACKUP_SEMANTICS;
  if (no_dereference) flags |= FILE_FLAG_OPEN_REPARSE_POINT;
  return flags;
}

auto open_time_handle(const std::filesystem::path &path, DWORD access,
                      bool no_dereference = false) -> HANDLE {
  return CreateFileW(path.wstring().c_str(), access,
                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                     nullptr, OPEN_EXISTING, file_open_flags(no_dereference),
                     nullptr);
}

auto read_file_times(const std::filesystem::path &path,
                     bool no_dereference = false) -> FileTimes {
  HANDLE h = open_time_handle(path, FILE_READ_ATTRIBUTES, no_dereference);
  EXPECT_TRUE(h != INVALID_HANDLE_VALUE);

  FILETIME create{}, access{}, write{};
  EXPECT_TRUE(GetFileTime(h, &create, &access, &write) != 0);
  CloseHandle(h);
  return FileTimes{access, write};
}

auto utc_filetime(WORD year, WORD month, WORD day, WORD hour, WORD minute,
                  WORD second) -> FILETIME {
  SYSTEMTIME st{};
  st.wYear = year;
  st.wMonth = month;
  st.wDay = day;
  st.wHour = hour;
  st.wMinute = minute;
  st.wSecond = second;

  FILETIME ft{};
  EXPECT_TRUE(SystemTimeToFileTime(&st, &ft) != 0);
  return ft;
}

auto set_file_times(const std::filesystem::path &path, const FILETIME &access,
                    const FILETIME &write, bool no_dereference = false)
    -> bool {
  HANDLE h = open_time_handle(path, FILE_WRITE_ATTRIBUTES, no_dereference);
  if (h == INVALID_HANDLE_VALUE) return false;
  bool ok = SetFileTime(h, nullptr, &access, &write) != 0;
  CloseHandle(h);
  return ok;
}

bool create_file_symlink_or_skip(const std::filesystem::path &link,
                                 const std::filesystem::path &target) {
  DWORD flags = 0;
#ifdef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
  flags |= SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
#endif
  if (CreateSymbolicLinkW(link.wstring().c_str(), target.wstring().c_str(),
                          flags)) {
    return true;
  }
  std::cout << "  SKIPPED (CreateSymbolicLinkW failed with error "
            << GetLastError() << ")\n";
  return false;
}

bool create_directory_junction_or_skip(const std::filesystem::path &link,
                                       const std::filesystem::path &target) {
  std::wstring command = L"cmd /d /c mklink /j \"" + link.wstring() + L"\" \"" +
                         target.wstring() + L"\" >nul";
  if (_wsystem(command.c_str()) == 0) return true;
  std::cout << "  SKIPPED (mklink /j failed)\n";
  return false;
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

TEST(touch, touch_creates_utf8_filename) {
  TempDir tmp;
  const std::wstring name = L"\x6D4B\x8BD5.txt";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"touch.exe", {name});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / name));
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

TEST(touch, touch_missing_trailing_separator_is_not_created) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"touch.exe", {L"missing.txt/"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "missing.txt"));
  EXPECT_TRUE(r.stderr_text.find("touch: cannot touch 'missing.txt/'") !=
              std::string::npos);
}

TEST(touch, touch_file_with_trailing_separator_reports_not_directory) {
  TempDir tmp;
  tmp.write("file.txt", "payload");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"touch.exe", {L"file.txt/"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ(tmp.read("file.txt"), "payload");
  EXPECT_TRUE(r.stderr_text.find("touch: cannot touch 'file.txt/': Not a "
                                 "directory") != std::string::npos);
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

TEST(touch, touch_date_accepts_compact_local_timestamp) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"touch.exe", {L"-d", L"20250102030405", L"compact.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);

  auto st = filetime_to_local(read_file_times(tmp.path / "compact.txt").write);
  EXPECT_EQ(st.wYear, 2025);
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

TEST(touch, touch_updates_directory_time) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "dir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"touch.exe", {L"-d", L"2024-04-05 06:07:08 UTC", L"dir"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);

  auto st = filetime_to_utc(read_file_times(tmp.path / "dir").write);
  EXPECT_EQ(st.wYear, 2024);
  EXPECT_EQ(st.wMonth, 4);
  EXPECT_EQ(st.wDay, 5);
  EXPECT_EQ(st.wHour, 6);
  EXPECT_EQ(st.wMinute, 7);
  EXPECT_EQ(st.wSecond, 8);
}

TEST(touch, touch_no_dereference_updates_junction_itself) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "target-dir");
  auto link = tmp.path / "junction-dir";
  if (!create_directory_junction_or_skip(link, tmp.path / "target-dir")) {
    return;
  }

  auto target_time = utc_filetime(2024, 7, 8, 9, 10, 11);
  auto link_time = utc_filetime(2023, 7, 8, 9, 10, 11);
  EXPECT_TRUE(
      set_file_times(tmp.path / "target-dir", target_time, target_time));
  if (!set_file_times(link, link_time, link_time, true)) {
    std::cout << "  SKIPPED (SetFileTime on junction failed with error "
              << GetLastError() << ")\n";
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"touch.exe",
        {L"-h", L"-d", L"2025-08-09 10:11:12 UTC", L"junction-dir"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);

  auto after_link = filetime_to_utc(read_file_times(link, true).write);
  EXPECT_EQ(after_link.wYear, 2025);
  EXPECT_EQ(after_link.wMonth, 8);
  EXPECT_EQ(after_link.wDay, 9);
  EXPECT_EQ(after_link.wHour, 10);
  EXPECT_EQ(after_link.wMinute, 11);
  EXPECT_EQ(after_link.wSecond, 12);

  auto after_target =
      filetime_to_utc(read_file_times(tmp.path / "target-dir").write);
  EXPECT_EQ(after_target.wYear, 2024);
  EXPECT_EQ(after_target.wMonth, 7);
  EXPECT_EQ(after_target.wDay, 8);
  EXPECT_EQ(after_target.wHour, 9);
  EXPECT_EQ(after_target.wMinute, 10);
  EXPECT_EQ(after_target.wSecond, 11);
}

TEST(touch, touch_no_dereference_updates_symlink_itself) {
  TempDir tmp;
  tmp.write("target.txt", "target");
  auto link = tmp.path / "link.txt";
  if (!create_file_symlink_or_skip(link,
                                   std::filesystem::path(L"target.txt"))) {
    return;
  }

  auto target_time = utc_filetime(2024, 1, 2, 3, 4, 5);
  auto link_time = utc_filetime(2023, 1, 2, 3, 4, 5);
  EXPECT_TRUE(
      set_file_times(tmp.path / "target.txt", target_time, target_time));
  if (!set_file_times(link, link_time, link_time, true)) {
    std::cout << "  SKIPPED (SetFileTime on symlink failed with error "
              << GetLastError() << ")\n";
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"touch.exe", {L"-h", L"-d", L"2025-02-03 04:05:06 UTC", L"link.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);

  auto after_link = filetime_to_utc(read_file_times(link, true).write);
  EXPECT_EQ(after_link.wYear, 2025);
  EXPECT_EQ(after_link.wMonth, 2);
  EXPECT_EQ(after_link.wDay, 3);
  EXPECT_EQ(after_link.wHour, 4);
  EXPECT_EQ(after_link.wMinute, 5);
  EXPECT_EQ(after_link.wSecond, 6);

  auto after_target =
      filetime_to_utc(read_file_times(tmp.path / "target.txt").write);
  EXPECT_EQ(after_target.wYear, 2024);
  EXPECT_EQ(after_target.wMonth, 1);
  EXPECT_EQ(after_target.wDay, 2);
  EXPECT_EQ(after_target.wHour, 3);
  EXPECT_EQ(after_target.wMinute, 4);
  EXPECT_EQ(after_target.wSecond, 5);
}

TEST(touch, touch_no_dereference_reference_reads_symlink_time) {
  TempDir tmp;
  tmp.write("ref-target.txt", "ref");
  tmp.write("target.txt", "target");
  auto ref_link = tmp.path / "ref-link.txt";
  if (!create_file_symlink_or_skip(ref_link,
                                   std::filesystem::path(L"ref-target.txt"))) {
    return;
  }

  auto referent_time = utc_filetime(2024, 5, 6, 7, 8, 9);
  auto link_time = utc_filetime(2025, 6, 7, 8, 9, 10);
  EXPECT_TRUE(set_file_times(tmp.path / "ref-target.txt", referent_time,
                             referent_time));
  if (!set_file_times(ref_link, link_time, link_time, true)) {
    std::cout << "  SKIPPED (SetFileTime on symlink failed with error "
              << GetLastError() << ")\n";
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"touch.exe", {L"-h", L"-r", L"ref-link.txt", L"target.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);

  auto target = filetime_to_utc(read_file_times(tmp.path / "target.txt").write);
  EXPECT_EQ(target.wYear, 2025);
  EXPECT_EQ(target.wMonth, 6);
  EXPECT_EQ(target.wDay, 7);
  EXPECT_EQ(target.wHour, 8);
  EXPECT_EQ(target.wMinute, 9);
  EXPECT_EQ(target.wSecond, 10);
}
