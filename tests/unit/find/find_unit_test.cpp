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
#include <AclAPI.h>

namespace {

bool create_directory_symlink_or_skip(const std::filesystem::path& link,
                                      const std::filesystem::path& target) {
  DWORD flags = SYMBOLIC_LINK_FLAG_DIRECTORY;
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

bool create_directory_junction(const std::filesystem::path& link,
                               const std::filesystem::path& target) {
  std::wstring command = L"cmd /d /c mklink /j \"" + link.wstring() + L"\" \"" +
                         target.wstring() + L"\" >nul";
  return _wsystem(command.c_str()) == 0;
}

bool set_last_write_time(const std::filesystem::path& path, WORD year,
                         WORD month, WORD day, WORD hour, WORD minute,
                         WORD second) {
  HANDLE handle = CreateFileW(path.wstring().c_str(), FILE_WRITE_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE |
                                  FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return false;
  }

  SYSTEMTIME local_st{};
  local_st.wYear = year;
  local_st.wMonth = month;
  local_st.wDay = day;
  local_st.wHour = hour;
  local_st.wMinute = minute;
  local_st.wSecond = second;

  SYSTEMTIME utc_st{};
  if (!TzSpecificLocalTimeToSystemTime(nullptr, &local_st, &utc_st)) {
    CloseHandle(handle);
    return false;
  }

  FILETIME file_time{};
  if (!SystemTimeToFileTime(&utc_st, &file_time)) {
    CloseHandle(handle);
    return false;
  }

  const bool ok = SetFileTime(handle, nullptr, nullptr, &file_time) != FALSE;
  CloseHandle(handle);
  return ok;
}

bool set_file_times(const std::filesystem::path& path, WORD creation_year,
                    WORD creation_month, WORD creation_day, WORD creation_hour,
                    WORD creation_minute, WORD creation_second,
                    WORD access_year, WORD access_month, WORD access_day,
                    WORD access_hour, WORD access_minute, WORD access_second,
                    WORD write_year, WORD write_month, WORD write_day,
                    WORD write_hour, WORD write_minute, WORD write_second) {
  HANDLE handle = CreateFileW(path.wstring().c_str(), FILE_WRITE_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE |
                                  FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return false;
  }

  const auto to_file_time = [](WORD year, WORD month, WORD day, WORD hour,
                               WORD minute, WORD second,
                               FILETIME& file_time) -> bool {
    SYSTEMTIME local_st{};
    local_st.wYear = year;
    local_st.wMonth = month;
    local_st.wDay = day;
    local_st.wHour = hour;
    local_st.wMinute = minute;
    local_st.wSecond = second;

    SYSTEMTIME utc_st{};
    if (!TzSpecificLocalTimeToSystemTime(nullptr, &local_st, &utc_st)) {
      return false;
    }
    return SystemTimeToFileTime(&utc_st, &file_time) != FALSE;
  };

  FILETIME creation_time{};
  FILETIME access_time{};
  FILETIME write_time{};
  const bool ok =
      to_file_time(creation_year, creation_month, creation_day, creation_hour,
                   creation_minute, creation_second, creation_time) &&
      to_file_time(access_year, access_month, access_day, access_hour,
                   access_minute, access_second, access_time) &&
      to_file_time(write_year, write_month, write_day, write_hour,
                   write_minute, write_second, write_time) &&
      SetFileTime(handle, &creation_time, &access_time, &write_time) != FALSE;
  CloseHandle(handle);
  return ok;
}

unsigned long long allocation_size_bytes(const std::filesystem::path& path) {
  DWORD attrs = GetFileAttributesW(path.wstring().c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) return 0;

  DWORD flags = 0;
  if (attrs & FILE_ATTRIBUTE_DIRECTORY) flags |= FILE_FLAG_BACKUP_SEMANTICS;
  if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) flags |= FILE_FLAG_OPEN_REPARSE_POINT;

  HANDLE handle = CreateFileW(
      path.wstring().c_str(), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, flags, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return 0;

  FILE_STANDARD_INFO info{};
  unsigned long long size = 0;
  if (GetFileInformationByHandleEx(handle, FileStandardInfo, &info,
                                   sizeof(info))) {
    size = static_cast<unsigned long long>(info.AllocationSize.QuadPart);
  }
  CloseHandle(handle);
  return size;
}

unsigned long long expected_find_blocks(const std::filesystem::path& path,
                                        unsigned long long unit) {
  const auto logical = static_cast<unsigned long long>(std::filesystem::file_size(path));
  const auto allocated = allocation_size_bytes(path);
  unsigned long long blocks = (allocated + unit - 1) / unit;
  if (logical > 0) {
    const unsigned long long min_blocks = (1024ULL + unit - 1) / unit;
    blocks = std::max(blocks, min_blocks);
  }
  return blocks;
}

std::string expected_find_sparseness(const std::filesystem::path& path) {
  const auto logical = static_cast<unsigned long long>(std::filesystem::file_size(path));
  if (logical == 0) return "1";

  const auto allocated = allocation_size_bytes(path);
  std::ostringstream stream;
  stream << (static_cast<double>(allocated) / static_cast<double>(logical));
  return stream.str();
}

std::string volume_serial_number(const std::filesystem::path& path) {
  DWORD attrs = GetFileAttributesW(path.wstring().c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) return {};

  DWORD flags = 0;
  if (attrs & FILE_ATTRIBUTE_DIRECTORY) flags |= FILE_FLAG_BACKUP_SEMANTICS;
  if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) flags |= FILE_FLAG_OPEN_REPARSE_POINT;

  HANDLE handle = CreateFileW(
      path.wstring().c_str(), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, flags, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return {};

  BY_HANDLE_FILE_INFORMATION info{};
  std::string out;
  if (GetFileInformationByHandle(handle, &info)) {
    out = std::to_string(
        static_cast<unsigned long long>(info.dwVolumeSerialNumber));
  }
  CloseHandle(handle);
  return out;
}

std::string filesystem_type_name(const std::filesystem::path& path) {
  auto dir = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
  std::error_code ec;
  auto absolute = std::filesystem::absolute(dir, ec);
  auto wdir = ec ? dir.wstring() : absolute.wstring();

  wchar_t root[MAX_PATH];
  if (!GetVolumePathNameW(wdir.c_str(), root, MAX_PATH)) return {};

  wchar_t fs_name[MAX_PATH] = L"";
  DWORD serial = 0;
  DWORD max_component = 0;
  DWORD flags = 0;
  if (!GetVolumeInformationW(root, nullptr, 0, &serial, &max_component, &flags,
                             fs_name, MAX_PATH)) {
    return {};
  }

  int size = WideCharToMultiByte(CP_UTF8, 0, fs_name, -1, nullptr, 0, nullptr,
                                 nullptr);
  if (size <= 1) return {};

  std::string out(static_cast<size_t>(size - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, fs_name, -1, out.data(), size, nullptr,
                      nullptr);
  return out;
}

unsigned long long io_block_size(const std::filesystem::path& path) {
  auto dir = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
  std::error_code ec;
  auto absolute = std::filesystem::absolute(dir, ec);
  auto wdir = ec ? dir.wstring() : absolute.wstring();

  wchar_t root[MAX_PATH];
  if (!GetVolumePathNameW(wdir.c_str(), root, MAX_PATH)) return 4096;

  DWORD sectors_per_cluster = 0;
  DWORD bytes_per_sector = 0;
  DWORD free_clusters = 0;
  DWORD total_clusters = 0;
  if (!GetDiskFreeSpaceW(root, &sectors_per_cluster, &bytes_per_sector,
                         &free_clusters, &total_clusters)) {
    return 4096;
  }

  const unsigned long long block_size =
      static_cast<unsigned long long>(sectors_per_cluster) * bytes_per_sector;
  return block_size == 0 ? 4096 : block_size;
}

struct OwnershipFields {
  std::string owner_name;
  std::string owner_id;
  std::string group_name;
  std::string group_id;
};

std::string account_name_from_sid(PSID sid) {
  if (sid == nullptr) return {};

  DWORD name_size = 0;
  DWORD domain_size = 0;
  SID_NAME_USE sid_type = SidTypeUnknown;
  LookupAccountSidW(nullptr, sid, nullptr, &name_size, nullptr, &domain_size,
                    &sid_type);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return {};

  std::wstring name(name_size, L'\0');
  std::wstring domain(domain_size, L'\0');
  if (!LookupAccountSidW(nullptr, sid, name.data(), &name_size, domain.data(),
                         &domain_size, &sid_type)) {
    return {};
  }

  name.resize(name_size);
  int size = WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, nullptr, 0,
                                 nullptr, nullptr);
  if (size <= 1) return {};
  std::string out(static_cast<size_t>(size - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, out.data(), size, nullptr,
                      nullptr);
  return out;
}

std::string account_id_from_sid(PSID sid) {
  if (sid == nullptr || !IsValidSid(sid)) return {};

  PUCHAR subauth_count = GetSidSubAuthorityCount(sid);
  if (subauth_count == nullptr || *subauth_count == 0) return {};

  DWORD* rid = GetSidSubAuthority(sid, *subauth_count - 1);
  if (rid == nullptr) return {};
  return std::to_string(*rid);
}

OwnershipFields ownership_fields(const std::filesystem::path& path) {
  std::wstring wpath = path.wstring();
  PSECURITY_DESCRIPTOR security_desc = nullptr;
  PSID owner_sid = nullptr;
  PSID group_sid = nullptr;

  const DWORD status = GetNamedSecurityInfoW(
      const_cast<wchar_t*>(wpath.c_str()), SE_FILE_OBJECT,
      OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION, &owner_sid,
      &group_sid, nullptr, nullptr, &security_desc);
  if (status != ERROR_SUCCESS) {
    if (security_desc != nullptr) LocalFree(security_desc);
    return {};
  }

  OwnershipFields out{.owner_name = account_name_from_sid(owner_sid),
                      .owner_id = account_id_from_sid(owner_sid),
                      .group_name = account_name_from_sid(group_sid),
                      .group_id = account_id_from_sid(group_sid)};
  if (security_desc != nullptr) LocalFree(security_desc);
  return out;
}

std::string creation_time_seconds(const std::filesystem::path& path) {
  DWORD attrs = GetFileAttributesW(path.wstring().c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) return "0.000000000";

  DWORD flags = 0;
  if (attrs & FILE_ATTRIBUTE_DIRECTORY) flags |= FILE_FLAG_BACKUP_SEMANTICS;
  if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) flags |= FILE_FLAG_OPEN_REPARSE_POINT;

  HANDLE handle = CreateFileW(
      path.wstring().c_str(), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, flags, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return "0.000000000";

  FILE_BASIC_INFO info{};
  std::string out = "0.000000000";
  if (GetFileInformationByHandleEx(handle, FileBasicInfo, &info, sizeof(info))) {
    const long long ticks = info.CreationTime.QuadPart;
    long long seconds = ticks / 10000000LL - 11644473600LL;
    long long nanos = (ticks % 10000000LL) * 100LL;
    if (nanos < 0) {
      --seconds;
      nanos += 1000000000LL;
    }

    std::ostringstream stream;
    stream << seconds << "." << std::setw(9) << std::setfill('0') << nanos;
    out = stream.str();
  }
  CloseHandle(handle);
  return out;
}

std::string access_time_seconds(const std::filesystem::path& path) {
  DWORD attrs = GetFileAttributesW(path.wstring().c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) return "0.000000000";

  DWORD flags = 0;
  if (attrs & FILE_ATTRIBUTE_DIRECTORY) flags |= FILE_FLAG_BACKUP_SEMANTICS;
  if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) flags |= FILE_FLAG_OPEN_REPARSE_POINT;

  HANDLE handle = CreateFileW(
      path.wstring().c_str(), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, flags, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return "0.000000000";

  FILE_BASIC_INFO info{};
  std::string out = "0.000000000";
  if (GetFileInformationByHandleEx(handle, FileBasicInfo, &info, sizeof(info))) {
    const long long ticks = info.LastAccessTime.QuadPart;
    long long seconds = ticks / 10000000LL - 11644473600LL;
    long long nanos = (ticks % 10000000LL) * 100LL;
    if (nanos < 0) {
      --seconds;
      nanos += 1000000000LL;
    }

    std::ostringstream stream;
    stream << seconds << "." << std::setw(9) << std::setfill('0') << nanos;
    out = stream.str();
  }
  CloseHandle(handle);
  return out;
}

}  // namespace

bool is_octal_mode(std::string_view text) {
  if (text.size() != 3 && text.size() != 4) return false;
  for (char ch : text) {
    if (ch < '0' || ch > '7') return false;
  }
  return true;
}

bool is_seconds_with_fraction(std::string_view text) {
  auto dot = text.find('.');
  if (dot == std::string_view::npos || dot == 0 || dot + 1 == text.size()) {
    return false;
  }
  for (size_t i = 0; i < text.size(); ++i) {
    if (i == dot) continue;
    if (text[i] < '0' || text[i] > '9') return false;
  }
  return true;
}

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

TEST(find, find_path_pattern_matches_full_path) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "src" / "nested");
  tmp.write("src/nested/match.txt", "");
  tmp.write("src/nested/skip.log", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"src", L"-path", L"src/*/*.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("src/nested/match.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("src/nested/skip.log") == std::string::npos);
}

TEST(find, find_ipath_pattern_is_case_insensitive) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "Case" / "Inner");
  tmp.write("Case/Inner/ReadMe.TXT", "");
  tmp.write("Case/Inner/Other.log", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"Case", L"-ipath", L"case/*/*.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("Case/Inner/ReadMe.TXT") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("Case/Inner/Other.log") == std::string::npos);
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

TEST(find, find_quit_stops_after_first_match) {
  TempDir tmp;
  tmp.write("first.txt", "");
  tmp.write("second.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L".", L"-name", L"*.txt", L"-quit"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);

  size_t newline_count = 0;
  for (char ch : r.stdout_text) {
    if (ch == '\n') ++newline_count;
  }
  EXPECT_EQ(newline_count, 1u);
}

TEST(find, find_empty_matches_empty_files_and_directories) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "emptydir");
  std::filesystem::create_directories(tmp.path / "nonemptydir");
  tmp.write("empty.txt", "");
  tmp.write("nonempty.txt", "content");
  tmp.write("nonemptydir/child.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L".", L"-empty"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("empty.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("emptydir") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("nonempty.txt") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("nonemptydir\n") == std::string::npos);
}

TEST(find, find_size_supports_byte_and_kib_units) {
  TempDir tmp;
  tmp.write("small.bin", "12345");
  tmp.write("large.bin", std::string(2000, 'x'));

  Pipeline exact_bytes;
  exact_bytes.set_cwd(tmp.wpath());
  exact_bytes.add(L"find.exe", {L".", L"-type", L"f", L"-size", L"5c"});
  auto exact_result = exact_bytes.run();

  EXPECT_EQ(exact_result.exit_code, 0);
  EXPECT_TRUE(exact_result.stdout_text.find("small.bin") != std::string::npos);
  EXPECT_TRUE(exact_result.stdout_text.find("large.bin") == std::string::npos);

  Pipeline kib_rounded;
  kib_rounded.set_cwd(tmp.wpath());
  kib_rounded.add(L"find.exe", {L".", L"-type", L"f", L"-size", L"+1k"});
  auto kib_result = kib_rounded.run();

  EXPECT_EQ(kib_result.exit_code, 0);
  EXPECT_TRUE(kib_result.stdout_text.find("large.bin") != std::string::npos);
  EXPECT_TRUE(kib_result.stdout_text.find("small.bin") == std::string::npos);
}

TEST(find, find_mmin_and_mtime_support_signed_ranges) {
  TempDir tmp;
  tmp.write("recent.txt", "now");
  tmp.write("older.txt", "old");

  auto now = std::filesystem::file_time_type::clock::now();
  std::filesystem::last_write_time(tmp.path / "older.txt",
                                   now - std::chrono::hours(49));

  Pipeline older_minutes;
  older_minutes.set_cwd(tmp.wpath());
  older_minutes.add(L"find.exe", {L".", L"-type", L"f", L"-mmin", L"+60"});
  auto minute_result = older_minutes.run();

  EXPECT_EQ(minute_result.exit_code, 0);
  EXPECT_TRUE(minute_result.stdout_text.find("older.txt") != std::string::npos);
  EXPECT_TRUE(minute_result.stdout_text.find("recent.txt") ==
              std::string::npos);

  Pipeline older_days;
  older_days.set_cwd(tmp.wpath());
  older_days.add(L"find.exe", {L".", L"-type", L"f", L"-mtime", L"+1"});
  auto day_result = older_days.run();

  EXPECT_EQ(day_result.exit_code, 0);
  EXPECT_TRUE(day_result.stdout_text.find("older.txt") != std::string::npos);
  EXPECT_TRUE(day_result.stdout_text.find("recent.txt") == std::string::npos);
}

TEST(find, find_true_and_false_are_expression_primaries) {
  TempDir tmp;
  tmp.write("keep.txt", "");
  tmp.write("skip.log", "");

  Pipeline true_pipeline;
  true_pipeline.set_cwd(tmp.wpath());
  true_pipeline.add(L"find.exe", {L".", L"-true", L"-name", L"*.txt"});
  auto true_result = true_pipeline.run();

  EXPECT_EQ(true_result.exit_code, 0);
  EXPECT_TRUE(true_result.stdout_text.find("keep.txt") != std::string::npos);
  EXPECT_TRUE(true_result.stdout_text.find("skip.log") == std::string::npos);

  Pipeline false_pipeline;
  false_pipeline.set_cwd(tmp.wpath());
  false_pipeline.add(L"find.exe", {L".", L"-false"});
  auto false_result = false_pipeline.run();

  EXPECT_EQ(false_result.exit_code, 0);
  EXPECT_TRUE(false_result.stdout_text.empty());
}

TEST(find, find_regex_and_iregex_match_whole_path) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "src");
  std::filesystem::create_directories(tmp.path / "docs");
  tmp.write("src/main.cpp", "");
  tmp.write("docs/ReadMe.MD", "");
  tmp.write("docs/notes.txt", "");

  Pipeline regex_pipeline;
  regex_pipeline.set_cwd(tmp.wpath());
  regex_pipeline.add(L"find.exe", {L".", L"-regex", L"./src/.*\\.cpp"});
  auto regex_result = regex_pipeline.run();

  EXPECT_EQ(regex_result.exit_code, 0);
  EXPECT_TRUE(regex_result.stdout_text.find("src/main.cpp") !=
              std::string::npos);
  EXPECT_TRUE(regex_result.stdout_text.find("docs/ReadMe.MD") ==
              std::string::npos);

  Pipeline iregex_pipeline;
  iregex_pipeline.set_cwd(tmp.wpath());
  iregex_pipeline.add(L"find.exe", {L".", L"-iregex", L"./docs/readme\\.md"});
  auto iregex_result = iregex_pipeline.run();

  EXPECT_EQ(iregex_result.exit_code, 0);
  EXPECT_TRUE(iregex_result.stdout_text.find("docs/ReadMe.MD") !=
              std::string::npos);
  EXPECT_TRUE(iregex_result.stdout_text.find("docs/notes.txt") ==
              std::string::npos);
}

TEST(find, find_newer_matches_files_modified_after_reference) {
  TempDir tmp;
  tmp.write("old.txt", "old");
  tmp.write("reference.txt", "ref");
  tmp.write("new.txt", "new");

  auto now = std::filesystem::file_time_type::clock::now();
  std::filesystem::last_write_time(tmp.path / "old.txt",
                                   now - std::chrono::hours(2));
  std::filesystem::last_write_time(tmp.path / "reference.txt",
                                   now - std::chrono::hours(1));
  std::filesystem::last_write_time(tmp.path / "new.txt", now);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L".", L"-type", L"f", L"-newer", L"reference.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("new.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("old.txt") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("reference.txt") == std::string::npos);
}

TEST(find, find_depth_processes_directory_contents_first) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "tree" / "dir");
  tmp.write("tree/dir/file.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"tree", L"-depth"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);

  auto file_pos = r.stdout_text.find("tree/dir/file.txt\n");
  auto dir_pos = r.stdout_text.find("tree/dir\n");
  auto root_pos = r.stdout_text.find("tree\n");

  EXPECT_TRUE(file_pos != std::string::npos);
  EXPECT_TRUE(dir_pos != std::string::npos);
  EXPECT_TRUE(root_pos != std::string::npos);
  if (file_pos == std::string::npos || dir_pos == std::string::npos ||
      root_pos == std::string::npos) {
    return;
  }
  EXPECT_LT(file_pos, dir_pos);
  EXPECT_LT(dir_pos, root_pos);
}

TEST(find, find_expression_not_negates_next_primary) {
  TempDir tmp;
  tmp.write("keep.txt", "");
  tmp.write("skip.log", "");

  Pipeline bang;
  bang.set_cwd(tmp.wpath());
  bang.add(L"find.exe", {L".", L"-type", L"f", L"!", L"-name", L"*.log"});

  auto bang_result = bang.run();
  EXPECT_EQ(bang_result.exit_code, 0);
  EXPECT_TRUE(bang_result.stdout_text.find("keep.txt") != std::string::npos);
  EXPECT_TRUE(bang_result.stdout_text.find("skip.log") == std::string::npos);

  Pipeline word;
  word.set_cwd(tmp.wpath());
  word.add(L"find.exe", {L".", L"-type", L"f", L"-not", L"-name", L"*.log"});

  auto word_result = word.run();
  EXPECT_EQ(word_result.exit_code, 0);
  EXPECT_TRUE(word_result.stdout_text.find("keep.txt") != std::string::npos);
  EXPECT_TRUE(word_result.stdout_text.find("skip.log") == std::string::npos);
}

TEST(find, find_expression_implicit_and_has_higher_precedence_than_or) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "src");
  std::filesystem::create_directories(tmp.path / "docs");
  tmp.write("src/main.cpp", "");
  tmp.write("docs/readme.md", "");
  tmp.write("docs/notes.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L".", L"-path", L"./src/*", L"-name", L"*.cpp", L"-o",
                      L"-name", L"*.md"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("src/main.cpp") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("docs/readme.md") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("docs/notes.txt") == std::string::npos);
}

TEST(find, find_expression_explicit_and_aliases_match_implicit_and) {
  TempDir tmp;
  tmp.write("match.cpp", "");
  tmp.write("skip.cpp", "content");
  tmp.write("empty.txt", "");

  const std::vector<std::vector<std::wstring>> cases = {
      {L".", L"-type", L"f", L"-a", L"-name", L"*.cpp", L"-a", L"-empty"},
      {L".", L"-type", L"f", L"-and", L"-name", L"*.cpp", L"-and", L"-empty"},
      {L".", L"-type", L"f", L"-name", L"*.cpp", L"-empty"},
  };

  for (const auto& args : cases) {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"find.exe", args);

    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_TRUE(r.stdout_text.find("match.cpp") != std::string::npos);
    EXPECT_TRUE(r.stdout_text.find("skip.cpp") == std::string::npos);
    EXPECT_TRUE(r.stdout_text.find("empty.txt") == std::string::npos);
  }
}

TEST(find, find_expression_or_aliases_match_either_side) {
  TempDir tmp;
  tmp.write("a.cpp", "");
  tmp.write("b.md", "");
  tmp.write("c.txt", "");

  const std::vector<std::vector<std::wstring>> cases = {
      {L".", L"-type", L"f", L"-name", L"*.cpp", L"-o", L"-name", L"*.md"},
      {L".", L"-type", L"f", L"-name", L"*.cpp", L"-or", L"-name", L"*.md"},
  };

  for (const auto& args : cases) {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"find.exe", args);

    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_TRUE(r.stdout_text.find("a.cpp") != std::string::npos);
    EXPECT_TRUE(r.stdout_text.find("b.md") != std::string::npos);
    EXPECT_TRUE(r.stdout_text.find("c.txt") == std::string::npos);
  }
}

TEST(find, find_expression_parentheses_override_precedence) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "src");
  std::filesystem::create_directories(tmp.path / "docs");
  tmp.write("src/main.cpp", "");
  tmp.write("src/readme.md", "");
  tmp.write("docs/readme.md", "");
  tmp.write("src/notes.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L".", L"-path", L"./src/*", L"(", L"-name", L"*.cpp",
                      L"-o", L"-name", L"*.md", L")"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("src/main.cpp") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("src/readme.md") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("docs/readme.md") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("src/notes.txt") == std::string::npos);
}

TEST(find, find_expression_comma_runs_both_sides_in_order) {
  TempDir tmp;
  tmp.write("a.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe",
        {L".", L"-name", L"a.txt", L"-printf", L"first:%f\\n", L",", L"-name",
         L"a.txt", L"-printf", L"second:%f\\n"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "first:a.txt\nsecond:a.txt\n");
}

TEST(find, find_expression_comma_returns_right_side_truth_value) {
  TempDir tmp;
  tmp.write("a.txt", "");
  tmp.write("b.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe",
        {L".", L"-name", L"a.txt", L",", L"-false", L"-o", L"-name",
         L"b.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("b.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("a.txt") == std::string::npos);
}

TEST(find, find_expression_syntax_errors_return_error) {
  TempDir tmp;
  tmp.write("a.txt", "");

  const std::vector<std::vector<std::wstring>> cases = {
      {L".", L"(", L"-name", L"*.txt"},
      {L".", L"-name", L"*.txt", L")"},
      {L".", L"!", L"-o", L"-name", L"*.txt"},
      {L".", L"-name", L"*.txt", L"-o"},
      {L".", L"-type", L"f", L"-a"},
  };

  for (const auto& args : cases) {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"find.exe", args);

    auto r = p.run();
    EXPECT_EQ(r.exit_code, 1);
  }
}

TEST(find, find_print0_uses_nul_separator) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "a");
  tmp.write("a/with space.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a", L"-name", L"*.txt", L"-print0"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, std::string("a/with space.txt\0", 17));
}

TEST(find, find_default_print_uses_microsoft_style_current_dir_prefix) {
  TempDir tmp;
  tmp.write("sample.txt", "x");
  tmp.write("sortable.txt", "y");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L".", L"-maxdepth", L"1", L"-type", L"f", L"-print"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(".\\sample.txt\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find(".\\sortable.txt\n") != std::string::npos);
}

TEST(find, find_printf_formats_common_file_fields) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "root");
  tmp.write("root/file.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe",
        {L"root", L"-type", L"f", L"-printf", L"%p|%f|%h|%y|%s|%m|%T@|%%\\n"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);

  std::string line = r.stdout_text;
  if (!line.empty() && line.back() == '\n') line.pop_back();

  std::vector<std::string> fields;
  size_t start = 0;
  while (start <= line.size()) {
    auto sep = line.find('|', start);
    if (sep == std::string::npos) {
      fields.push_back(line.substr(start));
      break;
    }
    fields.push_back(line.substr(start, sep - start));
    start = sep + 1;
  }

  EXPECT_EQ(fields.size(), 8u);
  if (fields.size() != 8u) return;
  EXPECT_EQ(fields[0], "root/file.txt");
  EXPECT_EQ(fields[1], "file.txt");
  EXPECT_EQ(fields[2], "root");
  EXPECT_EQ(fields[3], "f");
  EXPECT_EQ(fields[4], "5");
  EXPECT_TRUE(is_octal_mode(fields[5]));
  EXPECT_TRUE(is_seconds_with_fraction(fields[6]));
  EXPECT_EQ(fields[7], "%");
}

TEST(find, find_printf_supports_depth_field) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "tree" / "inner");
  tmp.write("tree/inner/file.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"tree", L"-printf", L"%d:%f\\n"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("0:tree\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("1:inner\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("2:file.txt\n") != std::string::npos);
}

TEST(find, find_printf_supports_start_point_and_path_below_root_fields) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "tree" / "inner");
  tmp.write("tree/inner/file.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"tree", L"-printf", L"%H|%P|%f\\n"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("tree||tree\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("tree|inner|inner\n") != std::string::npos);
  EXPECT_TRUE(
      r.stdout_text.find("tree|inner/file.txt|file.txt\n") != std::string::npos);
}

TEST(find, find_printf_supports_link_target_field_for_directory_junction) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "tree" / "targetdir");

  std::filesystem::path link = tmp.path / "tree" / "dirjunc";
  bool created = create_directory_junction(link, tmp.path / "tree" / "targetdir");
  EXPECT_TRUE(created);
  if (!created) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"tree", L"-name", L"dirjunc", L"-printf", L"%f|%l\\n"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("dirjunc|") != std::string::npos);
  EXPECT_TRUE(
      r.stdout_text.find((tmp.path / "tree" / "targetdir").generic_string()) !=
      std::string::npos);
}

TEST(find, find_printf_supports_target_type_field_for_regular_file) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a.txt", L"-printf", L"%f|%Y|%y\\n"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a.txt|f|f\n");
}

TEST(find, find_printf_supports_hard_link_count_field_for_regular_file) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  BOOL linked = CreateHardLinkW((tmp.path / "b.txt").c_str(),
                                (tmp.path / "a.txt").c_str(), nullptr);
  EXPECT_TRUE(linked);
  if (!linked) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a.txt", L"b.txt", L"-printf", L"%f|%n\\n"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("a.txt|2\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b.txt|2\n") != std::string::npos);
}

TEST(find, find_printf_supports_inode_field_for_hard_linked_files) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  BOOL linked = CreateHardLinkW((tmp.path / "b.txt").c_str(),
                                (tmp.path / "a.txt").c_str(), nullptr);
  EXPECT_TRUE(linked);
  if (!linked) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a.txt", L"b.txt", L"-printf", L"%f|%i\\n"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);

  std::istringstream lines(r.stdout_text);
  std::string first;
  std::string second;
  bool have_first = static_cast<bool>(std::getline(lines, first));
  bool have_second = static_cast<bool>(std::getline(lines, second));
  EXPECT_TRUE(have_first);
  EXPECT_TRUE(have_second);
  if (!have_first || !have_second) return;

  auto first_sep = first.find('|');
  auto second_sep = second.find('|');
  EXPECT_NE(first_sep, std::string::npos);
  EXPECT_NE(second_sep, std::string::npos);
  if (first_sep == std::string::npos || second_sep == std::string::npos) return;
  EXPECT_EQ(first.substr(0, first_sep), "a.txt");
  EXPECT_EQ(second.substr(0, second_sep), "b.txt");
  EXPECT_EQ(first.substr(first_sep + 1), second.substr(second_sep + 1));
  EXPECT_FALSE(first.substr(first_sep + 1).empty());
}

TEST(find, find_printf_supports_allocated_block_fields_for_regular_files) {
  TempDir tmp;
  tmp.write("f0.bin", "");
  tmp.write("f1.bin", "x");
  tmp.write("f600.bin", std::string(600, 'a'));
  tmp.write("f1024.bin", std::string(1024, 'b'));
  tmp.write("f1025.bin", std::string(1025, 'c'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe",
        {L"f0.bin", L"f1.bin", L"f600.bin", L"f1024.bin", L"f1025.bin",
         L"-printf", L"%f|%b|%k\\n"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);

  const auto expected_line = [&](const std::string& name) {
    const auto path = tmp.path / name;
    return name + "|" + std::to_string(expected_find_blocks(path, 512)) + "|" +
           std::to_string(expected_find_blocks(path, 1024)) + "\n";
  };

  EXPECT_EQ_TEXT(
      r.stdout_text,
      expected_line("f0.bin") + expected_line("f1.bin") +
          expected_line("f600.bin") + expected_line("f1024.bin") +
          expected_line("f1025.bin"));
}

TEST(find, find_printf_supports_device_field_for_regular_file) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a.txt", L"-printf", L"%f|%D\\n"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "a.txt|" + volume_serial_number(tmp.path / "a.txt") + "\n");
}

TEST(find, find_printf_supports_filesystem_type_field_for_regular_file) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a.txt", L"-printf", L"%f|%F\\n"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "a.txt|" + filesystem_type_name(tmp.path / "a.txt") + "\n");
}

TEST(find, find_printf_supports_io_block_size_field_for_regular_file) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a.txt", L"-printf", L"%f|%o\\n"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "a.txt|" + std::to_string(io_block_size(tmp.path / "a.txt")) +
                     "\n");
}

TEST(find, find_printf_supports_sparseness_field_for_regular_file) {
  TempDir tmp;
  tmp.write("a.txt", "x");
  tmp.write("abc.bin", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a.txt", L"abc.bin", L"-printf", L"%f|%S\\n"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(
      r.stdout_text,
      "a.txt|" + expected_find_sparseness(tmp.path / "a.txt") + "\n" +
          "abc.bin|" + expected_find_sparseness(tmp.path / "abc.bin") + "\n");
}

TEST(find, find_printf_supports_ownership_fields_for_regular_file) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  const auto ownership = ownership_fields(tmp.path / "a.txt");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a.txt", L"-printf", L"%f|%u|%U|%g|%G\\n"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "a.txt|" + ownership.owner_name + "|" + ownership.owner_id +
                     "|" + ownership.group_name + "|" + ownership.group_id +
                     "\n");
}

TEST(find, find_printf_supports_creation_time_field_for_regular_file) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a.txt", L"-printf", L"%f|%C@\\n"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "a.txt|" + creation_time_seconds(tmp.path / "a.txt") + "\n");
}

TEST(find, find_printf_supports_access_time_field_for_regular_file) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a.txt", L"-printf", L"%f|%A@\\n"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "a.txt|" + access_time_seconds(tmp.path / "a.txt") + "\n");
}

TEST(find, find_printf_supports_birth_time_field_for_regular_file) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a.txt", L"-printf", L"%f|%B@\\n"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "a.txt|" + creation_time_seconds(tmp.path / "a.txt") + "\n");
}

TEST(find, find_printf_supports_modification_time_components_for_regular_file) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  const bool set = set_last_write_time(tmp.path / "a.txt", 2024, 3, 5, 9, 7, 11);
  EXPECT_TRUE(set);
  if (!set) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe",
        {L"a.txt", L"-printf", L"%f|%TY|%Tm|%Td|%TH|%TM\\n"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a.txt|2024|03|05|09|07\n");
}

TEST(find, find_printf_supports_access_birth_change_and_modification_components) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  const bool set = set_file_times(tmp.path / "a.txt",
                                  2021, 2, 3, 4, 5, 6,
                                  2022, 3, 4, 5, 6, 7,
                                  2024, 3, 5, 9, 7, 11);
  EXPECT_TRUE(set);
  if (!set) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe",
        {L"a.txt",
         L"-printf",
         L"%f|"
         L"%AY|%Am|%Ad|%AH|%AM|%AS|%Aj|"
         L"%BY|%Bm|%Bd|%BH|%BM|%BS|%Bj|"
         L"%CY|%Cm|%Cd|%CH|%CM|%CS|%Cj|"
         L"%TY|%Tm|%Td|%TH|%TM|%TS|%Tj\\n"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(
      r.stdout_text,
      "a.txt|2022|03|04|05|06|07|063|"
      "2021|02|03|04|05|06|034|"
      "2021|02|03|04|05|06|034|"
      "2024|03|05|09|07|11|065\n");
}

TEST(find, find_printf_supports_symbolic_mode_field_for_regular_file) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a.txt", L"-printf", L"%f|%M|%m\\n"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a.txt|-rw-r--r--|644\n");
}

TEST(find, find_printf_supports_target_type_field_for_directory_junction) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "tree" / "targetdir");

  std::filesystem::path link = tmp.path / "tree" / "dirjunc";
  bool created = create_directory_junction(link, tmp.path / "tree" / "targetdir");
  EXPECT_TRUE(created);
  if (!created) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"tree", L"-name", L"dirjunc", L"-printf", L"%f|%Y\\n"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "dirjunc|d\n");
}

TEST(find, find_printf_reports_directory_junction_as_link_type) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "tree" / "targetdir");

  std::filesystem::path link = tmp.path / "tree" / "dirjunc";
  bool created = create_directory_junction(link, tmp.path / "tree" / "targetdir");
  EXPECT_TRUE(created);
  if (!created) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"tree", L"-name", L"dirjunc", L"-printf", L"%f|%y|%Y\\n"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "dirjunc|l|d\n");
}

TEST(find, find_type_l_matches_directory_junction_but_type_d_does_not) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "tree" / "targetdir");

  std::filesystem::path link = tmp.path / "tree" / "dirjunc";
  bool created = create_directory_junction(link, tmp.path / "tree" / "targetdir");
  EXPECT_TRUE(created);
  if (!created) return;

  Pipeline p_link;
  p_link.set_cwd(tmp.wpath());
  p_link.add(L"find.exe", {L"tree", L"-type", L"l"});

  auto r_link = p_link.run();
  EXPECT_EQ(r_link.exit_code, 0);
  EXPECT_TRUE(r_link.stdout_text.find("tree/dirjunc\n") != std::string::npos);

  Pipeline p_dir;
  p_dir.set_cwd(tmp.wpath());
  p_dir.add(L"find.exe", {L"tree", L"-type", L"d"});

  auto r_dir = p_dir.run();
  EXPECT_EQ(r_dir.exit_code, 0);
  EXPECT_TRUE(r_dir.stdout_text.find("tree/dirjunc\n") == std::string::npos);
  EXPECT_TRUE(r_dir.stdout_text.find("tree/targetdir\n") != std::string::npos);
}

TEST(find, find_printf_supports_symbolic_mode_field_for_directory_junction) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "tree" / "targetdir");

  std::filesystem::path link = tmp.path / "tree" / "dirjunc";
  bool created = create_directory_junction(link, tmp.path / "tree" / "targetdir");
  EXPECT_TRUE(created);
  if (!created) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"tree", L"-name", L"dirjunc", L"-printf", L"%f|%M|%Y\\n"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "dirjunc|lrwxrwxrwx|d\n");
}

TEST(find, find_printf_reports_link_text_length_in_size_field_for_directory_junction) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "tree" / "targetdir");

  std::filesystem::path link = tmp.path / "tree" / "dirjunc";
  bool created = create_directory_junction(link, tmp.path / "tree" / "targetdir");
  EXPECT_TRUE(created);
  if (!created) return;

  const auto target_text = (tmp.path / "tree" / "targetdir").generic_string();

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"tree", L"-name", L"dirjunc", L"-printf", L"%f|%s|%l\\n"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(
      r.stdout_text,
      "dirjunc|" + std::to_string(target_text.size()) + "|" + target_text + "\n");
}

TEST(find, find_depth_printf_keeps_original_start_point_for_H_and_P) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "tree" / "inner");
  tmp.write("tree/inner/file.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"tree", L"-depth", L"-printf", L"%H|%P|%f\\n"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("tree|inner/file.txt|file.txt\n") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("tree/inner/file.txt||file.txt\n") ==
              std::string::npos);
}

TEST(find, find_printf_suppresses_default_print_and_handles_escapes) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a.txt", L"-printf", L"%f\\t%y\\n"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "a.txt\tf\n");
}

TEST(find, find_printf_supports_nul_escape) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a.txt", L"-printf", L"%f\\0"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, std::string("a.txt\0", 6));
}

TEST(find, find_printf_supports_octal_escapes) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a.txt", L"-printf", L"\\141\\142\\012"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "ab\n");
}

TEST(find, find_printf_c_escape_stops_output_immediately) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a.txt", L"-printf", L"prefix\\csuffix"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "prefix");
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

TEST(find, find_printf_unrecognized_escape_warns_and_keeps_literal_text) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a.txt", L"-printf", L"left\\qright"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "left\\qright");
  EXPECT_TRUE(r.stderr_text.find("warning: unrecognized escape `\\q'") !=
              std::string::npos);
}

TEST(find, find_printf_unrecognized_format_directive_warns_and_drops_percent) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a.txt", L"-printf", L"left%Jright"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "leftJright");
  EXPECT_TRUE(
      r.stderr_text.find("warning: unrecognized format directive `%J'") !=
      std::string::npos);
}

TEST(find, find_expression_scopes_print_to_branch) {
  TempDir tmp;
  tmp.write("a.txt", "");
  tmp.write("b.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a.txt", L"b.txt", L"-name", L"a.txt", L"-print", L"-o",
                      L"-name", L"b.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "a.txt\n");
}

TEST(find, find_expression_scopes_print0_to_branch) {
  TempDir tmp;
  tmp.write("a.txt", "");
  tmp.write("b.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a.txt", L"b.txt", L"-name", L"a.txt", L"-print0", L"-o",
                      L"-name", L"b.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, std::string("a.txt\0", 6));
}

TEST(find, find_expression_scopes_printf_to_branch) {
  TempDir tmp;
  tmp.write("a.txt", "x");
  tmp.write("b.txt", "y");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe",
        {L"a.txt", L"b.txt", L"-name", L"a.txt", L"-printf", L"A:%f|%y|%s\\n",
         L"-o", L"-name", L"b.txt", L"-printf", L"B:%f|%y|%s\\n"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "A:a.txt|f|1\nB:b.txt|f|1\n");
}

TEST(find, find_expression_scopes_exec_to_branch) {
  TempDir tmp;
  tmp.write("a.txt", "x");
  tmp.write("b.txt", "y");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe",
        {L"a.txt", L"b.txt", L"-name", L"a.txt", L"-exec", L"cmd.exe", L"/C",
         L"echo", L"EXEC:{}", L";", L"-o", L"-name", L"b.txt", L"-print"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("EXEC:a.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b.txt\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("EXEC:b.txt") == std::string::npos);
}

TEST(find, find_expression_ok_decline_returns_false_for_or_fallback) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("n\n");
  p.add(L"find.exe", {L"a.txt", L"-ok", L"cmd.exe", L"/C", L"echo", L"OK:{}",
                      L";", L"-o", L"-print"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "a.txt\n");
  EXPECT_TRUE(r.stderr_text.find("cmd.exe") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("OK:a.txt") == std::string::npos);
}

TEST(find, find_expression_scopes_quit_to_reached_branch) {
  TempDir tmp;
  tmp.write("a.txt", "");
  tmp.write("b.txt", "");
  tmp.write("c.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe",
        {L"a.txt", L"c.txt", L"b.txt", L"-name", L"a.txt", L"-print", L"-o",
         L"-name", L"b.txt", L"-quit", L"-o", L"-name", L"c.txt", L"-print"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "a.txt\nc.txt\n");
}

TEST(find, find_missing_path_returns_error) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"not_exists", L"-name", L"*.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
}

TEST(find, find_delete_removes_matching_file_without_default_print) {
  TempDir tmp;
  tmp.write("delete-me.txt", "x");
  tmp.write("keep.txt", "y");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L".", L"-name", L"delete-me.txt", L"-delete"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "delete-me.txt"));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "keep.txt"));
}

TEST(find, find_expression_scopes_delete_to_branch) {
  TempDir tmp;
  tmp.write("a.txt", "x");
  tmp.write("b.txt", "y");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a.txt", L"b.txt", L"-name", L"a.txt", L"-o", L"-name",
                      L"b.txt", L"-delete"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "a.txt"));
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "b.txt"));
}

TEST(find, find_delete_removes_nested_directories_depth_first) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "tree" / "a" / "b");
  tmp.write("tree/root.txt", "root");
  tmp.write("tree/a/mid.txt", "mid");
  tmp.write("tree/a/b/leaf.txt", "leaf");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"tree", L"-delete"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "tree"));
}

TEST(find, find_delete_returns_error_when_matched_directory_is_not_empty) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "tree");
  tmp.write("tree/unmatched.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"tree", L"-type", L"d", L"-delete"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "tree"));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "tree" / "unmatched.txt"));
}

TEST(find, find_delete_and_prune_without_explicit_depth_returns_gnu_error) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "tree" / "nested");
  tmp.write("tree/nested/keep.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"tree", L"-prune", L"-delete"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(
      r.stderr_text.find(
          "The -delete action automatically turns on -depth, but -prune "
          "does nothing when -depth is in effect.") != std::string::npos);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "tree"));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "tree" / "nested" /
                                      "keep.txt"));
}

TEST(find, find_delete_and_prune_with_explicit_depth_continues) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "tree" / "nested");
  tmp.write("tree/nested/leaf.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"tree", L"-depth", L"-prune", L"-delete"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "tree"));
}

TEST(find, find_exec_semicolon_runs_once_per_match) {
  TempDir tmp;
  tmp.write("a.txt", "x");
  tmp.write("b.log", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L".", L"-name", L"*.txt", L"-exec", L"cmd.exe", L"/C",
                      L"echo", L"{}", L";"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("a.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b.log") == std::string::npos);
}

TEST(find, find_exec_replaces_placeholder_inside_argument) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe",
        {L"a.txt", L"-exec", L"cmd.exe", L"/C", L"echo", L"file={}", L";"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("file=a.txt") != std::string::npos);
}

TEST(find, find_exec_allows_child_arguments_that_look_like_options) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a.txt", L"-exec", L"cmd.exe", L"/C", L"echo",
                      L"-child-flag", L"{}", L";"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("-child-flag a.txt") != std::string::npos);
}

TEST(find, find_exec_plus_batches_paths) {
  TempDir tmp;
  tmp.write("a.txt", "x");
  tmp.write("b.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L".", L"-type", L"f", L"-exec", L"cmd.exe", L"/C",
                      L"echo", L"files", L"{}", L"+"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("files") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("a.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b.txt") != std::string::npos);
}

TEST(find, find_ok_prompts_and_respects_answer) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("n\n");
  p.add(L"find.exe",
        {L"a.txt", L"-ok", L"cmd.exe", L"/C", L"echo", L"{}", L";"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.find("cmd.exe") != std::string::npos);
}

TEST(find, find_ok_plus_prompts_once_and_runs_aggregate_batch) {
  TempDir tmp;
  tmp.write("a.txt", "x");
  tmp.write("b.txt", "y");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("y\n");
  p.add(L"find.exe", {L".", L"-type", L"f", L"-ok", L"cmd.exe", L"/C",
                      L"echo", L"files", L"{}", L"+"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("files") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("a.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b.txt") != std::string::npos);

  size_t prompt_count = 0;
  size_t pos = 0;
  while ((pos = r.stderr_text.find("> ?", pos)) != std::string::npos) {
    ++prompt_count;
    pos += 3;
  }
  EXPECT_EQ(prompt_count, 1u);
}

TEST(find, find_ok_plus_decline_skips_aggregate_batch) {
  TempDir tmp;
  tmp.write("a.txt", "x");
  tmp.write("b.txt", "y");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("n\n");
  p.add(L"find.exe", {L".", L"-type", L"f", L"-ok", L"cmd.exe", L"/C",
                      L"echo", L"files", L"{}", L"+"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.find("cmd.exe") != std::string::npos);
}

TEST(find, find_exec_missing_terminator_returns_error) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a.txt", L"-exec", L"cmd.exe", L"/C", L"echo", L"{}"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
}

TEST(find, find_missing_root_diagnostic_uses_generic_slashes) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"missing\\child"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(
      r.stderr_text.find(
          "find: 'missing/child': No such file or directory") !=
      std::string::npos);
}

TEST(find, find_delete_missing_root_diagnostic_uses_generic_slashes) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"missing\\child", L"-delete"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(
      r.stderr_text.find(
          "find: 'missing/child': No such file or directory") !=
      std::string::npos);
}

TEST(find, find_prune_skips_descending_into_directories) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "keep" / "nested");
  tmp.write("keep/nested/match.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"keep", L"-prune"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("keep\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("keep/nested") == std::string::npos);
}

TEST(find, find_expression_scopes_prune_to_matching_branch) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "skip" / "nested");
  std::filesystem::create_directories(tmp.path / "keep");
  tmp.write("skip/nested/hidden.txt", "");
  tmp.write("keep/match.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L".", L"-name", L"skip", L"-prune", L"-o", L"-type", L"f",
                      L"-name", L"*.txt", L"-print"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("keep/match.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("skip/nested/hidden.txt") ==
              std::string::npos);
}

TEST(find, find_L_follows_directory_symlink_if_available) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "work");
  std::filesystem::create_directories(tmp.path / "target" / "nested");
  tmp.write("target/nested/through-link.txt", "");

  if (!create_directory_symlink_or_skip(tmp.path / "work" / "linked",
                                        tmp.path / "target")) {
    return;
  }

  Pipeline p;
  p.set_cwd((tmp.path / "work").wstring());
  p.add(L"find.exe", {L"-L", L".", L"-name", L"through-link.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("linked/nested/through-link.txt") !=
              std::string::npos);
}

TEST(find, find_H_follows_command_line_directory_symlink_if_available) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "target" / "nested");
  tmp.write("target/nested/through-link.txt", "");

  if (!create_directory_symlink_or_skip(tmp.path / "linked",
                                        tmp.path / "target")) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"-H", L"linked", L"-name", L"through-link.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("linked/nested/through-link.txt") !=
              std::string::npos);
}

TEST(find, find_H_does_not_follow_nested_directory_symlink_if_available) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "target" / "inside");
  std::filesystem::create_directories(tmp.path / "target" / "real");
  tmp.write("target/inside/through-root.txt", "");
  tmp.write("target/real/visible.txt", "");

  if (!create_directory_symlink_or_skip(tmp.path / "linked",
                                        tmp.path / "target")) {
    return;
  }
  if (!create_directory_symlink_or_skip(tmp.path / "target" / "nested-link",
                                        tmp.path / "target" / "inside")) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"-H", L"linked", L"-name", L"*.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("linked/real/visible.txt") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("linked/inside/through-root.txt") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("linked/nested-link/through-root.txt") ==
              std::string::npos);
}

TEST(find,
     find_last_symlink_mode_option_wins_for_command_line_directory_symlink_if_available) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "target" / "nested");
  tmp.write("target/nested/through-link.txt", "");

  if (!create_directory_symlink_or_skip(tmp.path / "linked",
                                        tmp.path / "target")) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"-H", L"-P", L"linked", L"-name", L"through-link.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("linked/nested/through-link.txt") ==
              std::string::npos);
}

TEST(find,
     find_last_symlink_mode_option_wins_for_nested_directory_symlink_if_available) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "target" / "inside");
  std::filesystem::create_directories(tmp.path / "target" / "real");
  tmp.write("target/inside/through-root.txt", "");
  tmp.write("target/real/visible.txt", "");

  if (!create_directory_symlink_or_skip(tmp.path / "linked",
                                        tmp.path / "target")) {
    return;
  }
  if (!create_directory_symlink_or_skip(tmp.path / "target" / "nested-link",
                                        tmp.path / "target" / "inside")) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"-L", L"-H", L"linked", L"-name", L"*.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("linked/real/visible.txt") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("linked/inside/through-root.txt") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("linked/nested-link/through-root.txt") ==
              std::string::npos);
}

TEST(find,
     find_last_symlink_mode_option_wins_for_command_line_directory_junction) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "target" / "nested");
  tmp.write("target/nested/through-link.txt", "");

  bool created = create_directory_junction(tmp.path / "linked",
                                           tmp.path / "target");
  EXPECT_TRUE(created);
  if (!created) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"-H", L"-P", L"linked", L"-name", L"through-link.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("linked/nested/through-link.txt") ==
              std::string::npos);
}

TEST(find,
     find_last_symlink_mode_option_wins_for_nested_directory_junction) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "target" / "inside");
  std::filesystem::create_directories(tmp.path / "target" / "real");
  tmp.write("target/inside/through-root.txt", "");
  tmp.write("target/real/visible.txt", "");

  bool root_created = create_directory_junction(tmp.path / "linked",
                                                tmp.path / "target");
  EXPECT_TRUE(root_created);
  if (!root_created) return;

  bool nested_created = create_directory_junction(tmp.path / "target" /
                                                      "nested-link",
                                                  tmp.path / "target" /
                                                      "inside");
  EXPECT_TRUE(nested_created);
  if (!nested_created) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"-L", L"-H", L"linked", L"-name", L"*.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("linked/real/visible.txt") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("linked/inside/through-root.txt") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("linked/nested-link/through-root.txt") ==
              std::string::npos);
}

TEST(find, find_invalid_size_returns_error) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L".", L"-size", L"bad"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
}
