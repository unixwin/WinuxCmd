// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(date, military_t_zone_is_utc_minus_seven) {
  for (const auto *input : {L"1024t", L"1024T", L"10:24:00 T"}) {
    Pipeline p;
    p.add(L"date.exe", {L"-u", L"-d", input, L"+%H:%M:%S"});
    const auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ_TEXT(r.stdout_text, "17:24:00\n");
    EXPECT_TRUE(r.stderr_text.empty());
  }
}

TEST(date, date_basic) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe", {});

  TEST_LOG_CMD_LIST("date.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("date output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(date, date_relative_day_is_accepted) {
  Pipeline p;
  p.add(L"date.exe", {L"-u", L"--date", L"+1 day", L"+%s"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(date, date_format) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe", {L"+%Y-%m-%d"});

  TEST_LOG_CMD_LIST("date.exe", L"+%Y-%m-%d");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("date format output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  // Check format YYYY-MM-DD
  EXPECT_TRUE(r.stdout_text.find('-') != std::string::npos);
}

TEST(date, date_utc) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe", {L"-u"});

  TEST_LOG_CMD_LIST("date.exe", L"-u");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("date UTC output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(date, date_rfc2822) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe", {L"-R"});

  TEST_LOG_CMD_LIST("date.exe", L"-R");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("date RFC2822 output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(date, date_date_option_formats_fixed_utc_time) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe", {L"-u", L"--date", L"2024-01-02 03:04:05 UTC",
                      L"+%Y-%m-%dT%H:%M:%S%z"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "2024-01-02T03:04:05+0000\n");
}

TEST(date, date_common_format_specifiers) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe", {L"-u", L"--date", L"@0", L"+%F %T %s %j %u"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "1970-01-01 00:00:00 0 001 4\n");
}

TEST(date, date_iso8601_seconds) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe", {L"-u", L"--date", L"@0", L"--iso-8601=seconds"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "1970-01-01T00:00:00+00:00\n");
}

TEST(date, date_rfc3339_seconds) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe",
        {L"-u", L"--date", L"2024-01-02 03:04:05 UTC", L"--rfc-3339=seconds"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "2024-01-02 03:04:05+00:00\n");
}

TEST(date, date_reference_uses_file_modification_time) {
  TempDir tmp;

  Pipeline touch;
  touch.set_cwd(tmp.wpath());
  touch.add(L"touch.exe",
            {L"-d", L"2024-01-02 03:04:05 UTC", L"reference.txt"});
  EXPECT_EQ(touch.run().exit_code, 0);

  Pipeline date;
  date.set_cwd(tmp.wpath());
  date.add(L"date.exe",
           {L"-u", L"-r", L"reference.txt", L"+%Y-%m-%dT%H:%M:%S%z"});

  auto r = date.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "2024-01-02T03:04:05+0000\n");

  Pipeline long_date;
  long_date.set_cwd(tmp.wpath());
  long_date.add(L"date.exe",
                {L"-u", L"--reference=reference.txt", L"+%Y-%m-%dT%H:%M:%S%z"});

  auto long_result = long_date.run();
  EXPECT_EQ(long_result.exit_code, 0);
  EXPECT_EQ(long_result.stdout_text, "2024-01-02T03:04:05+0000\n");
}

TEST(date, date_rfc_email_fixed_utc_time) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe", {L"-u", L"-R", L"--date", L"2024-01-02 03:04:05 UTC"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "Tue, 02 Jan 2024 03:04:05 +0000\n");
}

TEST(date, date_invalid_positional_operand_is_rejected) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe", {L"notadate"});

  auto r = p.run();
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("date invalid positional stderr", r.stderr_text);
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("invalid date"), std::string::npos);
  EXPECT_NE(r.stderr_text.find("notadate"), std::string::npos);
}

TEST(date, date_extra_operand_is_rejected) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  // Invalid POSIX date so no system clock mutation can occur even if the
  // operand parsing ever changed.
  p.add(L"date.exe", {L"99999999999999", L"99999999999999"});

  auto r = p.run();
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("date extra operand stderr", r.stderr_text);
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("extra operand"), std::string::npos);
}

TEST(date, date_operand_without_plus_with_date_option) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe", {L"-d", L"now", L"xyz"});

  auto r = p.run();
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("date lacks plus stderr", r.stderr_text);
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("lacks a leading '+'"), std::string::npos);
}

TEST(date, date_multiple_output_formats_are_rejected) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe", {L"-R", L"+%Y"});

  auto r = p.run();
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("date multiple formats stderr", r.stderr_text);
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("multiple output formats specified"),
            std::string::npos);
}

TEST(date, date_mutually_exclusive_date_sources) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe", {L"-d", L"now", L"-r", L"somefile"});

  auto r = p.run();
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("date exclusive stderr", r.stderr_text);
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("mutually exclusive"), std::string::npos);
}

TEST(date, date_set_and_print_options_conflict) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe", {L"-s", L"now", L"-d", L"now"});

  auto r = p.run();
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("date set/print conflict stderr", r.stderr_text);
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("may not be used together"), std::string::npos);
}

TEST(date, date_resolution_prints_nanosecond_tick) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe", {L"--resolution"});

  auto r = p.run();
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("date resolution output", r.stdout_text);
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "0.000000100\n");
}

TEST(date, date_invalid_posix_clock_operand) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  // Month 13 -> invalid MMDDhhmm[[CC]YY][.ss] POSIX set-date syntax.
  p.add(L"date.exe", {L"130203042026"});

  auto r = p.run();
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("date invalid posix stderr", r.stderr_text);
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("invalid date"), std::string::npos);
}

TEST(date, date_utc_parses_zoneless_string_as_utc) {
  Pipeline p;
  p.add(L"date.exe",
        {L"-u", L"-d", L"2024-01-02 03:04:05", L"+%Y-%m-%dT%H:%M:%S"});

  auto r = p.run();
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("date -u zoneless stdout", r.stdout_text);
  // [GNU] with -u a date string without a timezone is interpreted as UTC.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "2024-01-02T03:04:05\n");
}

TEST(date, date_local_parses_zoneless_string_as_local) {
  Pipeline p;
  p.add(L"date.exe", {L"-d", L"2024-01-02 03:04:05", L"+%Y-%m-%dT%H:%M:%S%z"});

  auto r = p.run();
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("date local zoneless stdout", r.stdout_text);
  // Without -u the same string must render the local wall-clock fields
  // (the numeric offset is whatever the machine's timezone is).
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text.substr(0, 11), "2024-01-02T");
  EXPECT_EQ(r.stdout_text.substr(11, 9), "03:04:05+");
}

TEST(date, date_file_directory_reports_read_error) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "adir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe", {L"-u", L"-f", L"adir", L"+%Y-%m-%d"});

  auto r = p.run();
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("date -f dir stderr", r.stderr_text);
  // [GNU] date: adir: read error: Is a directory, exit 1 (uutils #14434).
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("read error: Is a directory"),
            std::string::npos);
  EXPECT_NE(r.stderr_text.find("adir"), std::string::npos);
}

TEST(date, date_file_missing_reports_open_error) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe", {L"-u", L"--file=no-such-file.txt", L"+%Y-%m-%d"});

  auto r = p.run();
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("date -f missing stderr", r.stderr_text);
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("No such file or directory"), std::string::npos);
  EXPECT_NE(r.stderr_text.find("no-such-file.txt"), std::string::npos);
}

TEST(date, date_file_formats_each_line_and_continues_after_invalid) {
  TempDir tmp;
  tmp.write("dates.txt",
            "2024-01-02 03:04:05\nnot a date\n2025-06-07 08:09:10\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe", {L"-u", L"-f", L"dates.txt", L"+%Y-%m-%d"});

  auto r = p.run();
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("date -f mixed stdout", r.stdout_text);
  TEST_LOG("date -f mixed stderr", r.stderr_text);
  // [GNU] batch mode reports the invalid line, keeps processing, and exits 1.
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ(r.stdout_text, "2024-01-02\n2025-06-07\n");
  EXPECT_NE(r.stderr_text.find("invalid date 'not a date'"), std::string::npos);
}

TEST(date, date_file_valid_file_prints_every_line) {
  TempDir tmp;
  tmp.write("dates.txt", "2024-01-02 03:04:05\n2025-06-07 08:09:10\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe", {L"-u", L"-f", L"dates.txt", L"+%Y-%m-%dT%H:%M:%S"});

  auto r = p.run();
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("date -f valid stdout", r.stdout_text);
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "2024-01-02T03:04:05\n2025-06-07T08:09:10\n");
}

// ===== #1239 regression: POSIX drive-dialect operands =====
//
// `date -f /e/Data/file` used to hand the raw dialect text to
// is_directory/std::ifstream, and Win32 resolves a leading "/e" against the
// CURRENT drive - from a foreign cwd every POSIX spelling reported ENOENT
// for a file that exists on the operand's drive (MSYS date reads the same
// spelling from anywhere). The probes below synthesize the dialect
// spellings from the TempDir's own drive, so the assertions work on any
// host regardless of which drives exist.
namespace date_dialect_detail {

// The MSYS single-letter drive spellings of a native path
// ("D:\repo\f.txt"): "/d/repo/f.txt", "/cygdrive/d/repo/f.txt",
// "/mnt/d/repo/f.txt".
auto slash_drive_form(const std::filesystem::path &native) -> std::wstring {
  const std::wstring text = native.wstring();
  wchar_t drive = text[0];
  if (drive >= L'A' && drive <= L'Z') {
    drive = static_cast<wchar_t>(drive - L'A' + L'a');
  }
  std::wstring dialect;
  dialect.push_back(L'/');
  dialect.push_back(drive);
  for (size_t i = 2; i < text.size(); ++i) {
    dialect.push_back(text[i] == L'\\' ? L'/' : text[i]);
  }
  return dialect;
}

auto cygdrive_form(const std::filesystem::path &native) -> std::wstring {
  return L"/cygdrive" + slash_drive_form(native);
}

auto mnt_form(const std::filesystem::path &native) -> std::wstring {
  return L"/mnt" + slash_drive_form(native);
}

// First fixed drive whose letter differs from OTHER, or L'\0' when none.
auto other_fixed_drive(wchar_t other) -> wchar_t {
  wchar_t buf[512];
  const DWORD len =
      GetLogicalDriveStringsW(static_cast<DWORD>(std::size(buf)), buf);
  for (DWORD i = 0; i + 1 < len;) {
    const wchar_t *entry = buf + i;
    const size_t entry_len = std::wcslen(entry);
    if (entry_len == 0) break;
    i += static_cast<DWORD>(entry_len) + 1;
    if (entry_len < 3 || entry[1] != L':' || entry[2] != L'\\') continue;
    wchar_t drive = entry[0];
    if (drive >= L'A' && drive <= L'Z') {
      drive = static_cast<wchar_t>(drive - L'A' + L'a');
    }
    if (drive == other) continue;
    if (GetDriveTypeW(entry) != DRIVE_FIXED) continue;
    return drive;
  }
  return L'\0';
}

void clear_shell_root_env(Pipeline &pipeline) {
  for (const auto *name :
       {L"__RUBASH_SHELL_ROOT", L"NIU_ROOT", L"WINUXSH_ROOT", L"RUBASH_ROOT"}) {
    pipeline.set_env(name, L"");
  }
}

// UTF-8 form of an operand for stderr substring comparisons.
auto utf8_operand(const std::wstring &text) -> std::string {
  if (text.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                       static_cast<int>(text.size()), nullptr,
                                       0, nullptr, nullptr);
  std::string out(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                      out.data(), size, nullptr, nullptr);
  return out;
}

}  // namespace date_dialect_detail

TEST(date, date_file_drive_dialect_spellings_read_from_foreign_cwd) {
  TempDir dir;
  TempDir cwd;
  dir.write("dates1239.txt", "2024-01-02 03:04:05\n2025-06-07 08:09:10\n");

  for (const auto &operand :
       {date_dialect_detail::slash_drive_form(dir.path / L"dates1239.txt"),
        date_dialect_detail::cygdrive_form(dir.path / L"dates1239.txt"),
        date_dialect_detail::mnt_form(dir.path / L"dates1239.txt")}) {
    Pipeline p;
    p.set_cwd(cwd.wpath());
    date_dialect_detail::clear_shell_root_env(p);
    p.add(L"date.exe", {L"-u", L"-f", operand, L"+%Y-%m-%dT%H:%M:%S"});

    auto r = p.run();

    TEST_LOG_CMD_LIST("date.exe", L"-u", L"-f", operand);
    TEST_LOG_EXIT_CODE(r);
    TEST_LOG("date -f dialect stdout", r.stdout_text);
    TEST_LOG("date -f dialect stderr", r.stderr_text);

    // Before #1239 the raw dialect text stranded on the cwd drive and the
    // open reported ENOENT even though the file exists.  MSYS date reads
    // the same spelling from any cwd.
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ_TEXT(r.stdout_text, "2024-01-02T03:04:05\n2025-06-07T08:09:10\n");
    EXPECT_TRUE(r.stderr_text.empty());
  }
}

TEST(date, date_file_drive_dialect_cwd_on_other_drive_reads_file) {
  TempDir tmp;
  tmp.write("dates1239.txt", "2024-01-02 03:04:05\n2025-06-07 08:09:10\n");
  const wchar_t tmp_drive = tmp.path.wstring()[0];
  const wchar_t other = date_dialect_detail::other_fixed_drive(
      tmp_drive >= L'A' && tmp_drive <= L'Z'
          ? static_cast<wchar_t>(tmp_drive - L'A' + L'a')
          : tmp_drive);
  if (other == L'\0') {
    std::cout << "  SKIPPED (no second fixed drive to host the cwd)\n";
    return;
  }

  Pipeline p;
  p.set_cwd(std::wstring(1, other) + L":\\");
  date_dialect_detail::clear_shell_root_env(p);
  p.add(L"date.exe",
        {L"-u", L"-f",
         date_dialect_detail::slash_drive_form(tmp.path / L"dates1239.txt"),
         L"+%Y-%m-%d"});

  auto r = p.run();

  TEST_LOG_CMD_LIST(
      "date.exe", L"-u", L"-f",
      date_dialect_detail::slash_drive_form(tmp.path / L"dates1239.txt"));
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("date -f dialect stdout", r.stdout_text);
  TEST_LOG("date -f dialect stderr", r.stderr_text);

  // The audit's exact shape: cwd on one drive, operand spelled for another.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "2024-01-02\n2025-06-07\n");
}

TEST(date, date_file_bare_drive_root_reports_is_a_directory) {
  TempDir cwd;
  const wchar_t drive = cwd.path.wstring()[0];
  const std::wstring operand = std::wstring(L"/") + drive;

  Pipeline p;
  p.set_cwd(std::wstring(1, drive) + L":\\");
  date_dialect_detail::clear_shell_root_env(p);
  p.add(L"date.exe", {L"-u", L"-f", operand, L"+%Y-%m-%d"});

  auto r = p.run();

  TEST_LOG_CMD_LIST("date.exe", L"-u", L"-f", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("date -f drive root stderr", r.stderr_text);

  // Bare "/x" names the drive root: a directory operand keeps the [GNU]
  // "read error: Is a directory" report (same as `date -f adir`), with the
  // operand quoted as typed.  Pre-#1239 the spelling was probed against
  // the cwd drive and reported ENOENT instead.
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("read error: Is a directory"),
            std::string::npos);
  EXPECT_NE(r.stderr_text.find(date_dialect_detail::utf8_operand(operand)),
            std::string::npos);
}

// ===== #1256 regression: POSIX drive-dialect -r/--reference operands =====
//
// `date -r /e/Data/file` probed the raw dialect text with
// GetFileAttributesExW, and Win32 resolves a leading "/e" against the
// CURRENT drive - from a foreign cwd -r reported "failed to get
// modification time" for a file that exists on the operand's drive (MSYS
// date reads the same spelling from anywhere).  The probes below
// synthesize the dialect spellings from the TempDir's own drive, so the
// assertions work on any host regardless of which drives exist.

// Stamp NAME inside DIR with a fixed UTC mtime, like the plain -r
// regression above; the exact stamp is what the -r probes assert on.
void stamp_reference_mtime(const TempDir &dir, const wchar_t *name) {
  Pipeline touch;
  touch.set_cwd(dir.wpath());
  touch.add(L"touch.exe", {L"-d", L"2024-01-02 03:04:05 UTC", name});
  EXPECT_EQ(touch.run().exit_code, 0);
}

TEST(date, date_reference_drive_dialect_spellings_read_from_foreign_cwd) {
  TempDir dir;
  TempDir cwd;
  dir.write("ref1256.txt", "x\n");
  stamp_reference_mtime(dir, L"ref1256.txt");

  for (const auto &operand :
       {date_dialect_detail::slash_drive_form(dir.path / L"ref1256.txt"),
        date_dialect_detail::cygdrive_form(dir.path / L"ref1256.txt"),
        date_dialect_detail::mnt_form(dir.path / L"ref1256.txt")}) {
    Pipeline p;
    p.set_cwd(cwd.wpath());
    date_dialect_detail::clear_shell_root_env(p);
    p.add(L"date.exe", {L"-u", L"-r", operand, L"+%Y-%m-%dT%H:%M:%S"});

    auto r = p.run();

    TEST_LOG_CMD_LIST("date.exe", L"-u", L"-r", operand);
    TEST_LOG_EXIT_CODE(r);
    TEST_LOG("date -r dialect stdout", r.stdout_text);
    TEST_LOG("date -r dialect stderr", r.stderr_text);

    // Before #1256 the raw dialect text stranded on the cwd drive and the
    // attribute probe failed even though the file exists.  MSYS date reads
    // the same spelling from any cwd.
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ_TEXT(r.stdout_text, "2024-01-02T03:04:05\n");
    EXPECT_TRUE(r.stderr_text.empty());
  }
}

TEST(date, date_reference_drive_dialect_cwd_on_other_drive_reads_file) {
  TempDir tmp;
  tmp.write("ref1256.txt", "x\n");
  stamp_reference_mtime(tmp, L"ref1256.txt");
  const wchar_t tmp_drive = tmp.path.wstring()[0];
  const wchar_t other = date_dialect_detail::other_fixed_drive(
      tmp_drive >= L'A' && tmp_drive <= L'Z'
          ? static_cast<wchar_t>(tmp_drive - L'A' + L'a')
          : tmp_drive);
  if (other == L'\0') {
    std::cout << "  SKIPPED (no second fixed drive to host the cwd)\n";
    return;
  }

  const std::wstring operand =
      date_dialect_detail::slash_drive_form(tmp.path / L"ref1256.txt");

  Pipeline p;
  p.set_cwd(std::wstring(1, other) + L":\\");
  date_dialect_detail::clear_shell_root_env(p);
  p.add(L"date.exe", {L"-u", L"-r", operand, L"+%Y-%m-%d"});

  auto r = p.run();

  TEST_LOG_CMD_LIST("date.exe", L"-u", L"-r", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("date -r dialect stdout", r.stdout_text);
  TEST_LOG("date -r dialect stderr", r.stderr_text);

  // The audit's exact shape: cwd on one drive, operand spelled for another.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "2024-01-02\n");
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(date, date_reference_missing_dialect_operand_quotes_as_typed) {
  TempDir cwd;
  const std::wstring operand =
      date_dialect_detail::slash_drive_form(cwd.path / L"absent1256.txt");

  Pipeline p;
  p.set_cwd(cwd.wpath());
  date_dialect_detail::clear_shell_root_env(p);
  p.add(L"date.exe", {L"-u", L"-r", operand, L"+%Y-%m-%d"});

  auto r = p.run();

  TEST_LOG_CMD_LIST("date.exe", L"-u", L"-r", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("date -r missing operand stderr", r.stderr_text);

  // A missing reference still fails, but the message quotes the operand
  // exactly as typed - the dialect fold happens only at the Win32 probe
  // boundary, like MSYS which echoes the spelling the user used.
  // [GNU] date.c:609-612 (9.4): the diagnostic is the quotef form,
  // error (EXIT_FAILURE, errno, "%s", quotef (reference)).
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("': No such file or directory"),
            std::string::npos);
  EXPECT_NE(r.stderr_text.find(date_dialect_detail::utf8_operand(operand)),
            std::string::npos);
}

// [GNU] %^ forces upper case; %# swaps the natural case per specifier
// (upper for weekday/month names, lower for meridiem and zone name),
// matching gnulib strftime semantics (uutils #14351).
TEST(date, date_case_flags_match_gnu) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe",
        {L"-u", L"--date", L"@0",
         L"+%a|%#a|%^a|%A|%#A|%b|%#b|%B|%#B|%p|%#p|%^p|%P|%#P|%^P|%Z|%#Z|%^Z"});

  auto r = p.run();
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("date case flags stdout", r.stdout_text);
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text,
            "Thu|THU|THU|Thursday|THURSDAY|Jan|JAN|January|JANUARY|"
            "AM|am|AM|am|am|am|UTC|utc|UTC\n");
}

// [GNU] %# leaves %c and numeric/time-only composites unchanged while
// %^ uppercases their full expansion (uutils #14351).
TEST(date, date_case_flags_composites) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe",
        {L"-u", L"--date", L"@0", L"+%c|%#c|%^c|%r|%#r|%^r|%x|%#x|%^x"});

  auto r = p.run();
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("date case flags composites stdout", r.stdout_text);
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text,
            "Thu Jan  1 00:00:00 1970|Thu Jan  1 00:00:00 1970|"
            "THU JAN  1 00:00:00 1970|"
            "12:00:00 AM|12:00:00 AM|12:00:00 AM|"
            "01/01/70|01/01/70|01/01/70\n");
}

// [GNU] a dangling '%' with flags is emitted verbatim, and unknown
// specifiers keep their raw text; %q is the quarter of year.
TEST(date, date_case_flags_dangling_percent) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe", {L"-u", L"--date", L"@0", L"+%^|100%%|%^q|%q"});

  auto r = p.run();
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("date case flags dangling stdout", r.stdout_text);
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "%^|100%|1|1\n");
}

// [GNU] %E/%O select the locale's alternative representation; with no
// alternate digits/eras available they fall back to the base conversion
// (issue #1088).
TEST(date, date_eo_modifiers_fall_back_to_base_conversion) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe",
        {L"-u", L"--date", L"@0", L"+%EY|%Oy|%Od|%Om|%Ec|%OH|%OM|%OS"});

  auto r = p.run();
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("date %E/%O stdout", r.stdout_text);
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "1970|70|01|01|Thu Jan  1 00:00:00 1970|00|00|00\n");
}

// [GNU] --uct and --rfc-822 are deprecated hidden aliases accepted for
// back-compat: --uct == --utc, --rfc-822 == -R/--rfc-email (issue #1073).
TEST(date, date_deprecated_uct_and_rfc822_aliases) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe", {L"--uct", L"--date", L"@0", L"+%s"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "0\n");

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"date.exe", {L"--rfc-822", L"--date", L"@0"});

  Pipeline p3;
  p3.set_cwd(tmp.wpath());
  p3.add(L"date.exe", {L"-R", L"--date", L"@0"});

  auto r2 = p2.run();
  auto r3 = p3.run();
  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_EQ_TEXT(r2.stdout_text, r3.stdout_text);
}

// [#1216] `date -d ''` is an explicit (empty) date operand, not "no -d":
// GNU resolves it to today at 00:00:00 in the effective zone. WinuxCmd used
// to print the current time because the empty value made the option look
// absent.
TEST(date, date_empty_d_argument_is_today_midnight_issue1216) {
  Pipeline p;
  p.add(L"date.exe", {L"-u", L"-d", L"", L"+%H:%M:%S"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  EXPECT_EXIT_CODE(r, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "00:00:00\n");
}

TEST(date, date_blank_d_argument_is_today_midnight_issue1216) {
  Pipeline p;
  p.add(L"date.exe", {L"-u", L"-d", L" ", L"+%H:%M:%S"});

  auto r = p.run();

  EXPECT_EXIT_CODE(r, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "00:00:00\n");
}

// [#1216] The date part must stay today (not 1970-01-01); GNU `date -u -d ''
// +%F` is the same day as `date -u +%F`.
TEST(date, date_empty_d_argument_keeps_todays_date_issue1216) {
  Pipeline today;
  today.add(L"date.exe", {L"-u", L"+%F"});
  auto rt = today.run();

  Pipeline empty;
  empty.add(L"date.exe", {L"-u", L"-d", L"", L"+%F"});
  auto re = empty.run();

  EXPECT_EXIT_CODE(re, 0);
  EXPECT_EQ_TEXT(re.stdout_text, rt.stdout_text);
}
