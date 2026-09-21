// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include <regex>

#include "framework/winuxtest.h"

namespace {

auto first_usage_value(const std::string& text) -> std::uintmax_t {
  std::stringstream ss(text);
  std::uintmax_t value = 0;
  ss >> value;
  return value;
}

// GNU du reports allocated blocks, which WinuxCmd models as the
// GetCompressedFileSizeW result rounded up to the volume cluster size.
auto volume_cluster_size(const std::filesystem::path& path) -> uint64_t {
  std::wstring root = path.root_path().wstring();
  if (root.empty()) {
    wchar_t cwd[MAX_PATH];
    if (GetCurrentDirectoryW(MAX_PATH, cwd) == 0) {
      return 4096;
    }
    root = std::wstring(cwd, 2) + L"\\";
  }
  DWORD spc = 0, bps = 0, nu = 0;
  if (!GetDiskFreeSpaceW(root.c_str(), &spc, &bps, &nu, &nu)) {
    return 4096;
  }
  return static_cast<uint64_t>(spc) * bps;
}

auto allocated_size(uint64_t logical, const std::filesystem::path& volume)
    -> uint64_t {
  const uint64_t cluster = volume_cluster_size(volume);
  return cluster == 0 ? logical : (logical + cluster - 1) / cluster * cluster;
}

auto expected_blocks(uint64_t logical, uint64_t block,
                     const std::filesystem::path& volume) -> uint64_t {
  const uint64_t allocated = allocated_size(logical, volume);
  return allocated == 0 ? 0 : 1 + (allocated - 1) / block;
}

// Mirrors src/commands/du.cpp format_size(): ceiling to tenths of the unit.
auto format_size_for_test(uint64_t size, bool si) -> std::string {
  const char* units = si ? "BKMGTPE" : "BKMGTP";
  const uint64_t base = si ? 1000 : 1024;
  if (size < base) {
    return std::to_string(size);
  }
  int idx = 0;
  uint64_t unit = base;
  while (idx < 6 && size / unit >= base) {
    unit *= base;
    ++idx;
  }
  ++idx;
  const uint64_t q = size / unit;
  const uint64_t r = size % unit;
  uint64_t tenths = q * 10 + (r * 10 + unit - 1) / unit;
  while (tenths >= base * 10 && idx < 6) {
    tenths = (tenths + base - 1) / base;
    ++idx;
  }
  char buf[32];
  if (tenths < 100) {
    snprintf(buf, sizeof(buf), "%llu.%llu%c",
             static_cast<unsigned long long>(tenths / 10),
             static_cast<unsigned long long>(tenths % 10), units[idx]);
  } else {
    snprintf(buf, sizeof(buf), "%llu%c",
             static_cast<unsigned long long>((tenths + 9) / 10), units[idx]);
  }
  return std::string(buf);
}

auto line_usage_values(const std::string& text) -> std::vector<std::uintmax_t> {
  std::vector<std::uintmax_t> values;
  std::stringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    std::stringstream ss(line);
    std::uintmax_t value = 0;
    if (ss >> value) {
      values.push_back(value);
    }
  }
  return values;
}

auto usage_for_path(const std::string& text, const std::string& suffix)
    -> std::optional<std::uintmax_t> {
  std::stringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    if (!line.ends_with(suffix)) continue;
    std::stringstream ss(line);
    std::uintmax_t value = 0;
    if (ss >> value) return value;
  }
  return std::nullopt;
}

auto local_filetime(int year, int month, int day, int hour, int minute,
                    int second = 0) -> std::optional<FILETIME> {
  SYSTEMTIME local{};
  local.wYear = static_cast<WORD>(year);
  local.wMonth = static_cast<WORD>(month);
  local.wDay = static_cast<WORD>(day);
  local.wHour = static_cast<WORD>(hour);
  local.wMinute = static_cast<WORD>(minute);
  local.wSecond = static_cast<WORD>(second);

  TIME_ZONE_INFORMATION tzi{};
  SYSTEMTIME utc{};
  if (!TzSpecificLocalTimeToSystemTime(&tzi, &local, &utc)) {
    return std::nullopt;
  }

  FILETIME ft{};
  if (!SystemTimeToFileTime(&utc, &ft)) {
    return std::nullopt;
  }
  return ft;
}

auto set_file_times(const std::filesystem::path& path,
                    const std::optional<FILETIME>& creation,
                    const std::optional<FILETIME>& access,
                    const std::optional<FILETIME>& write) -> bool {
  HANDLE handle =
      CreateFileW(path.wstring().c_str(), FILE_WRITE_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return false;
  }

  const FILETIME* creation_ptr = creation ? &*creation : nullptr;
  const FILETIME* access_ptr = access ? &*access : nullptr;
  const FILETIME* write_ptr = write ? &*write : nullptr;
  bool ok = SetFileTime(handle, creation_ptr, access_ptr, write_ptr) != 0;
  CloseHandle(handle);
  return ok;
}

auto line_for_suffix(const std::string& text, const std::string& suffix)
    -> std::optional<std::string> {
  std::stringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    if (line.ends_with(suffix)) {
      return line;
    }
  }
  return std::nullopt;
}

auto path_selected_time_string(const std::filesystem::path& path,
                               const std::string& word)
    -> std::optional<std::string> {
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (!GetFileAttributesExW(path.wstring().c_str(), GetFileExInfoStandard,
                            &data)) {
    return std::nullopt;
  }

  FILETIME selected = data.ftLastWriteTime;
  if (word == "access" || word == "atime" || word == "use") {
    selected = data.ftLastAccessTime;
  } else if (word == "status" || word == "ctime") {
    selected = data.ftCreationTime;
  }

  FILETIME local_ft{};
  if (!FileTimeToLocalFileTime(&selected, &local_ft)) {
    local_ft = selected;
  }

  SYSTEMTIME st{};
  if (!FileTimeToSystemTime(&local_ft, &st)) {
    return std::nullopt;
  }

  char buf[32];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth,
           st.wDay, st.wHour, st.wMinute);
  return std::string(buf);
}

}  // namespace

TEST(du, du_basic) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");
  tmp.write("file2.txt", "content2");

  TEST_LOG_FILE_CONTENT("file1.txt", "content1");
  TEST_LOG_FILE_CONTENT("file2.txt", "content2");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {});

  TEST_LOG_CMD_LIST("du.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("du.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should show directory size
  EXPECT_TRUE(r.stdout_text.length() > 0);
}

TEST(du, du_human_readable) {
  TempDir tmp;
  tmp.write("file.txt", "Hello, World!");

  TEST_LOG_FILE_CONTENT("file.txt", "Hello, World!");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-h"});

  TEST_LOG_CMD_LIST("du.exe", L"-h");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("du.exe -h output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should show human-readable sizes
  EXPECT_TRUE(r.stdout_text.length() > 0);
}

TEST(du, du_summarize) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "subdir");
  tmp.write("subdir/file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-s"});

  TEST_LOG_CMD_LIST("du.exe", L"-s");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("du.exe -s output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should only show total, not individual files
  EXPECT_TRUE(r.stdout_text.length() > 0);
}

TEST(du, du_max_depth) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "subdir");
  std::filesystem::create_directory(tmp.path / "subdir" / "nested");
  tmp.write("subdir/file.txt", "content");
  tmp.write("subdir/nested/file2.txt", "content2");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-d", L"1"});

  TEST_LOG_CMD_LIST("du.exe", L"-d", L"1");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("du.exe -d 1 output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should show directory size
  EXPECT_TRUE(r.stdout_text.length() > 0);
}

TEST(du, du_kilobytes) {
  TempDir tmp;
  tmp.write("file.txt", "content");

  TEST_LOG_FILE_CONTENT("file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-k"});

  TEST_LOG_CMD_LIST("du.exe", L"-k");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("du.exe -k output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should show sizes in KB
  EXPECT_TRUE(r.stdout_text.length() > 0);
}

TEST(du, du_kilobytes_rounds_up_small_files) {
  TempDir tmp;
  tmp.write("file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-k", L"file.txt"});

  TEST_LOG_CMD_LIST("du.exe", L"-k", L"file.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("du.exe -k file output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(first_usage_value(r.stdout_text),
            expected_blocks(7, 1024, tmp.path));
}

TEST(du, du_megabytes_uses_1M_blocks) {
  TempDir tmp;
  tmp.write("file.txt", std::string(1200000, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-m", L"file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(first_usage_value(r.stdout_text),
            expected_blocks(1200000, 1u << 20, tmp.path));
}

// GNU appends the unit letter for a bare-suffix block size: "du -BM" prints
// "2M". uutils #13605.
TEST(du, du_bare_suffix_block_size_appends_unit) {
  TempDir tmp;
  tmp.write("file.txt", std::string(1200000, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-BM", L"file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "2M\tfile.txt\n");
}

// An integer-prefixed block size scales silently, without the suffix.
TEST(du, du_integer_block_size_has_no_suffix) {
  TempDir tmp;
  tmp.write("file.txt", std::string(1200000, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-B", L"1M", L"file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(first_usage_value(r.stdout_text),
            expected_blocks(1200000, 1u << 20, tmp.path));
  EXPECT_EQ(r.stdout_text.find("2M"), std::string::npos);
}

TEST(du, du_default_uses_1024_byte_blocks) {
  TempDir tmp;
  tmp.write("file.txt", std::string(600, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(first_usage_value(r.stdout_text),
            expected_blocks(600, 1024, tmp.path));
}

TEST(du, du_H_is_dereference_args_not_si) {
  TempDir tmp;
  tmp.write("file.txt", std::string(1500, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-H", L"file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(first_usage_value(r.stdout_text),
            expected_blocks(1500, 1024, tmp.path));
  EXPECT_EQ(r.stdout_text.find("K"), std::string::npos);
}

TEST(du, du_si_is_long_option_only) {
  TempDir tmp;
  tmp.write("file.txt", std::string(1500, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"--si", L"file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find(
                format_size_for_test(allocated_size(1500, tmp.path), true)),
            std::string::npos);
}

TEST(du, du_block_size_one_reports_bytes) {
  TempDir tmp;
  tmp.write("file.txt", "abcdef");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"--block-size=1", L"file.txt"});

  TEST_LOG_CMD_LIST("du.exe", L"--block-size=1", L"file.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("du.exe --block-size=1 output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(first_usage_value(r.stdout_text), expected_blocks(6, 1, tmp.path));
}

TEST(du, du_apparent_size_is_accepted_as_windows_file_length_mode) {
  TempDir tmp;
  tmp.write("file.txt", "abcdef");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"--apparent-size", L"--block-size=1", L"file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(first_usage_value(r.stdout_text), 6);
}

TEST(du, du_threshold_positive_excludes_smaller_entries) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "root");
  tmp.write("root/small.txt", "tiny");
  tmp.write("root/large.bin", std::string(1500, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-a", L"-b", L"--threshold=1000", L"root"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("large.bin"), std::string::npos);
  EXPECT_EQ(r.stdout_text.find("small.txt"), std::string::npos);
}

TEST(du, du_threshold_negative_excludes_larger_entries) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "root");
  tmp.write("root/small.txt", "tiny");
  tmp.write("root/large.bin", std::string(1500, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-a", L"-b", L"--threshold=-10", L"root"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("small.txt"), std::string::npos);
  EXPECT_EQ(r.stdout_text.find("large.bin"), std::string::npos);
}

TEST(du, du_exclude_patterns_skip_files_and_directories) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "root" / "skipdir");
  tmp.write("root/keep.txt", "abc");
  tmp.write("root/skip.log", "defg");
  tmp.write("root/skipdir/nested.txt", "hidden");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-b", L"--exclude=*.log", L"--exclude=skipdir", L"root"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(first_usage_value(r.stdout_text), 3);
  EXPECT_EQ(r.stdout_text.find("skip.log"), std::string::npos);
  EXPECT_EQ(r.stdout_text.find("skipdir"), std::string::npos);
  EXPECT_EQ(r.stdout_text.find("nested.txt"), std::string::npos);
}

TEST(du, du_exclude_from_file_skips_matching_entries) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "root" / "skipdir");
  tmp.write("root/keep.txt", "abc");
  tmp.write("root/skip.log", "defg");
  tmp.write("root/skipdir/nested.txt", "hidden");
  tmp.write("exclude.txt", "*.log\r\nskipdir\r\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-b", L"--exclude-from=exclude.txt", L"root"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(first_usage_value(r.stdout_text), 3);
  EXPECT_EQ(r.stdout_text.find("skip.log"), std::string::npos);
  EXPECT_EQ(r.stdout_text.find("skipdir"), std::string::npos);
  EXPECT_EQ(r.stdout_text.find("nested.txt"), std::string::npos);
}

TEST(du, du_short_X_exclude_from_file_skips_matching_entries) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "root" / "skipdir");
  tmp.write("root/keep.txt", "abc");
  tmp.write("root/skip.log", "defg");
  tmp.write("root/skipdir/nested.txt", "hidden");
  tmp.write("exclude.txt", "*.log\nskipdir\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-b", L"-X", L"exclude.txt", L"root"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(first_usage_value(r.stdout_text), 3);
  EXPECT_EQ(r.stdout_text.find("skip.log"), std::string::npos);
  EXPECT_EQ(r.stdout_text.find("skipdir"), std::string::npos);
  EXPECT_EQ(r.stdout_text.find("nested.txt"), std::string::npos);
}

TEST(du, du_files0_from_reads_nul_terminated_paths) {
  TempDir tmp;
  tmp.write("a.txt", "abc");
  tmp.write("b.txt", "defg");
  tmp.write_bytes("list.bin", {'a', '.', 't', 'x', 't', '\0', 'b', '.', 't',
                               'x', 't', '\0'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-b", L"-c", L"--files0-from", L"list.bin"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("3\ta.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("4\tb.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("7\ttotal") != std::string::npos);
}

TEST(du, du_files0_from_rejects_named_operands) {
  TempDir tmp;
  tmp.write("a.txt", "abc");
  tmp.write_bytes("list.bin", {'a', '.', 't', 'x', 't', '\0'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"--files0-from", L"list.bin", L"a.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("--files0-from disallows") !=
              std::string::npos);
}

TEST(du, du_later_block_size_option_overrides_human_readable) {
  TempDir tmp;
  tmp.write("file.txt", std::string(1500, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-h", L"-B", L"1", L"file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(first_usage_value(r.stdout_text),
            expected_blocks(1500, 1, tmp.path));
  EXPECT_EQ(r.stdout_text.find("K"), std::string::npos);
}

TEST(du, du_later_human_readable_overrides_block_size) {
  TempDir tmp;
  tmp.write("file.txt", std::string(1500, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-B", L"1", L"-h", L"file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find(
                format_size_for_test(allocated_size(1500, tmp.path), false)),
            std::string::npos);
}

TEST(du, du_later_m_option_overrides_block_size) {
  TempDir tmp;
  tmp.write("file.txt", std::string(1200000, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"--block-size=1", L"-m", L"file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(first_usage_value(r.stdout_text),
            expected_blocks(1200000, 1u << 20, tmp.path));
}

TEST(du, du_block_size_human_readable_word) {
  TempDir tmp;
  tmp.write("file.txt", std::string(1500, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"--block-size=human-readable", L"file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find(
                format_size_for_test(allocated_size(1500, tmp.path), false)),
            std::string::npos);
}

TEST(du, du_block_size_si_word) {
  TempDir tmp;
  tmp.write("file.txt", std::string(10000, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"--block-size=si", L"file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find(
                format_size_for_test(allocated_size(10000, tmp.path), true)),
            std::string::npos);
}

TEST(du, du_max_depth_zero_emits_only_root_directory) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "root" / "subdir");
  tmp.write("root/file.txt", "root");
  tmp.write("root/subdir/nested.txt", "nested");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-d", L"0", L"root"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("root") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("subdir") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file.txt") == std::string::npos);
}

TEST(du, du_all_with_max_depth_zero_does_not_emit_child_files) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "root" / "subdir");
  tmp.write("root/file.txt", "root");
  tmp.write("root/subdir/nested.txt", "nested");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-a", L"-d", L"0", L"root"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("root") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file.txt") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("nested.txt") == std::string::npos);
}

TEST(du, du_total_with_bytes) {
  TempDir tmp;
  tmp.write("one.txt", "abc");
  tmp.write("two.txt", "defg");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-b", L"-c", L"one.txt", L"two.txt"});

  TEST_LOG_CMD_LIST("du.exe", L"-b", L"-c", L"one.txt", L"two.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("du.exe -b -c output", r.stdout_text);

  auto values = line_usage_values(r.stdout_text);
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(values.size() >= 3u);
  if (!values.empty()) {
    EXPECT_EQ(values.back(), 7);
  }
  EXPECT_NE(r.stdout_text.find("total"), std::string::npos);
}

TEST(du, du_invalid_block_size_fails) {
  TempDir tmp;
  tmp.write("file.txt", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-B", L"bogus", L"file.txt"});

  TEST_LOG_CMD_LIST("du.exe", L"-B", L"bogus", L"file.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("du.exe invalid block size stderr", r.stderr_text);

  EXPECT_NE(r.exit_code, 0);
}

TEST(du, du_inodes) {
  TempDir tmp;
  tmp.write("file1.txt", "abc");
  tmp.write("file2.txt", "def");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"--inodes", L"."});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // --inodes should show file count instead of size
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(du, du_one_file_system) {
  TempDir tmp;
  tmp.write("file.txt", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-x", L"."});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // -x should stay on same filesystem
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(du, du_separate_dirs) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "root" / "subdir");
  tmp.write("root/top.txt", "top1");
  tmp.write("root/subdir/nested.txt", "nested");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  // -b (--apparent-size --block-size=1) keeps the -S accounting check on
  // logical sizes so it is independent of the volume cluster rounding.
  p.add(L"du.exe", {L"-S", L"-b", L"root"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  auto root_usage = usage_for_path(r.stdout_text, "root");
  auto subdir_usage = usage_for_path(r.stdout_text, "root/subdir");
  EXPECT_TRUE(root_usage.has_value());
  EXPECT_TRUE(subdir_usage.has_value());
  if (!root_usage.has_value() || !subdir_usage.has_value()) return;
  EXPECT_EQ(*root_usage, 4u);
  EXPECT_EQ(*subdir_usage, 6u);
}

TEST(du, du_dereference) {
  TempDir tmp;
  tmp.write("file.txt", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-L", L"."});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // -L should follow symlinks
}

TEST(du, du_time) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "root" / "subdir");
  tmp.write("root/older.txt", "abc");
  tmp.write("root/subdir/newer.txt", "def");

  auto older = local_filetime(2020, 1, 1, 8, 15);
  auto newer = local_filetime(2021, 2, 3, 14, 25);
  EXPECT_TRUE(older.has_value());
  EXPECT_TRUE(newer.has_value());
  if (!older.has_value() || !newer.has_value()) return;

  EXPECT_TRUE(set_file_times(tmp.path / "root" / "older.txt", std::nullopt,
                             std::nullopt, older));
  EXPECT_TRUE(set_file_times(tmp.path / "root" / "subdir" / "newer.txt",
                             std::nullopt, std::nullopt, newer));
  auto expected_time = path_selected_time_string(
      tmp.path / "root" / "subdir" / "newer.txt", "mtime");
  EXPECT_TRUE(expected_time.has_value());
  if (!expected_time.has_value()) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"--time", L"root"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  auto root_line = line_for_suffix(r.stdout_text, "root");
  EXPECT_TRUE(root_line.has_value());
  if (!root_line.has_value()) return;
  EXPECT_TRUE(root_line->find(*expected_time) != std::string::npos);
}

TEST(du, du_time_access_word_uses_access_timestamp) {
  TempDir tmp;
  tmp.write("file.txt", "abc");

  auto access = local_filetime(2022, 4, 5, 6, 7);
  auto write = local_filetime(2020, 1, 2, 3, 4);
  EXPECT_TRUE(access.has_value());
  EXPECT_TRUE(write.has_value());
  if (!access.has_value() || !write.has_value()) return;

  EXPECT_TRUE(
      set_file_times(tmp.path / "file.txt", std::nullopt, access, write));
  auto expected_time =
      path_selected_time_string(tmp.path / "file.txt", "access");
  EXPECT_TRUE(expected_time.has_value());
  if (!expected_time.has_value()) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"--time=access", L"file.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(*expected_time) != std::string::npos);
}

TEST(du, du_time_last_occurrence_can_select_status_time) {
  TempDir tmp;
  tmp.write("file.txt", "abc");

  auto creation = local_filetime(2023, 7, 8, 9, 10);
  auto access = local_filetime(2021, 1, 1, 1, 1);
  auto write = local_filetime(2020, 2, 2, 2, 2);
  EXPECT_TRUE(creation.has_value());
  EXPECT_TRUE(access.has_value());
  EXPECT_TRUE(write.has_value());
  if (!creation.has_value() || !access.has_value() || !write.has_value()) {
    return;
  }

  EXPECT_TRUE(set_file_times(tmp.path / "file.txt", creation, access, write));
  auto expected_time =
      path_selected_time_string(tmp.path / "file.txt", "status");
  EXPECT_TRUE(expected_time.has_value());
  if (!expected_time.has_value()) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"--time=access", L"--time=status", L"file.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(*expected_time) != std::string::npos);
}

TEST(du, du_null_separator) {
  TempDir tmp;
  tmp.write("file.txt", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-0", L"file.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(!r.stdout_text.empty());
  EXPECT_EQ(r.stdout_text.back(), '\0');
  EXPECT_TRUE(r.stdout_text.find('\n') == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file.txt") != std::string::npos);
}

TEST(du, du_time_style_and_count_links) {
  TempDir tmp;
  tmp.write("file.txt", "data");
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe",
        {L"--time", L"--time-style=+%Y", L"--count-links", L"file.txt"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("file.txt"), std::string::npos);
}

TEST(du, du_full_iso_time_style_is_accepted) {
  TempDir tmp;
  tmp.write("file.txt", "data");
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"--time", L"--time-style=full-iso", L"file.txt"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("file.txt"), std::string::npos);
}

namespace {
bool du_test_create_symlink(const std::filesystem::path& link,
                            const std::filesystem::path& target,
                            bool target_is_directory = false) {
  DWORD flags = 0;
#ifdef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
  flags |= SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
#endif
  if (target_is_directory) {
    flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
  }
  if (CreateSymbolicLinkW(link.wstring().c_str(), target.wstring().c_str(),
                          flags)) {
    return true;
  }
  std::cout << "  SKIPPED (CreateSymbolicLinkW failed with error "
            << GetLastError() << ")\n";
  return false;
}
}  // namespace

TEST(du, du_prints_children_before_parent_and_forward_slashes) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "d3" / "d3sub");
  tmp.write("d3/d3sub/f.txt", "data");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"d3"});
  auto r = p.run();

  // [GNU] du prints in traversal post-order with '/' separators:
  // "N d3/d3sub\nN d3\n" (Savannah #13956 / issue #385).
  EXPECT_EQ(r.exit_code, 0);
  const auto sub_pos = r.stdout_text.find("d3/d3sub");
  const auto root_pos = r.stdout_text.rfind("\td3\n");
  EXPECT_TRUE(sub_pos != std::string::npos);
  EXPECT_TRUE(root_pos != std::string::npos);
  EXPECT_TRUE(sub_pos < root_pos);
  EXPECT_TRUE(r.stdout_text.find("d3\\d3sub") == std::string::npos);
}

TEST(du, du_dereference_dangling_operand_errors) {
  TempDir tmp;
  tmp.write("gone.txt", "x");

  std::filesystem::path link = tmp.path / "dangling";
  if (!du_test_create_symlink(link, std::filesystem::path(L"gone.txt"))) {
    return;
  }
  std::filesystem::remove(tmp.path / "gone.txt");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"-L", L"dangling"});
  auto r = p.run();

  // [GNU] "du -L dangling" reports "cannot access" and exits 1 (#1059).
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("cannot access") != std::string::npos);
}

TEST(du, du_dangling_operand_without_dereference_lists_link) {
  TempDir tmp;
  tmp.write("gone.txt", "x");

  std::filesystem::path link = tmp.path / "dangling";
  if (!du_test_create_symlink(link, std::filesystem::path(L"gone.txt"))) {
    return;
  }
  std::filesystem::remove(tmp.path / "gone.txt");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"du.exe", {L"dangling"});
  auto r = p.run();

  // [GNU] Default du lstats the operand: a dangling link counts as a
  // zero-size entry, rc=0.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("dangling") != std::string::npos);
}

TEST(du, du_block_size_env_is_honored) {
  TempDir tmp;
  tmp.write("file.bin", std::string(2048, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"DU_BLOCK_SIZE", L"512");
  p.add(L"du.exe", {L"file.bin"});
  auto r = p.run();

  // [GNU] DU_BLOCK_SIZE=512 makes du report 512B blocks (#964): the
  // 2048-byte file needs at least 4 of them (default 1K blocks would be 2).
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::regex_search(
      r.stdout_text, std::regex(R"(^([4-9]|[0-9][0-9]+)\tfile\.bin)")));
}
