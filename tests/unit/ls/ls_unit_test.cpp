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
 *  - File: ls_unit_test.cpp
 *  - CopyrightYear: 2026
 */
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <regex>
#include <thread>

#include "framework/winuxtest.h"

namespace {

bool create_symlink_or_skip(const std::filesystem::path& link,
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

}  // namespace

TEST(ls, ls_basic) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");
  tmp.write("file2.txt", "content2");

  TEST_LOG_FILE_CONTENT("file1.txt", "content1");
  TEST_LOG_FILE_CONTENT("file2.txt", "content2");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {});

  TEST_LOG_CMD_LIST("ls.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ls.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Verify the output contains the expected files
  EXPECT_TRUE(r.stdout_text.find("file1.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file2.txt") != std::string::npos);
}

TEST(ls, ls_hide_pattern_hides_matching_entries_in_default_mode) {
  TempDir tmp;
  tmp.write("a.txt", "a");
  tmp.write("b.log", "b");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-1", L"--hide=*.log"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a.txt\n");
}

TEST(ls, ls_hide_pattern_is_overridden_by_all_mode) {
  TempDir tmp;
  tmp.write("a.txt", "a");
  tmp.write("b.log", "b");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-1", L"-a", L"--hide=*.log"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(".\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("..\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("a.txt\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b.log\n") != std::string::npos);
}

TEST(ls, ls_ignore_pattern_still_applies_in_all_mode) {
  TempDir tmp;
  tmp.write("a.txt", "a");
  tmp.write("b.log", "b");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-1", L"-a", L"--ignore=*.log"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(".\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("..\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("a.txt\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b.log\n") == std::string::npos);
}

TEST(ls, ls_long_format) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");

  TEST_LOG_FILE_CONTENT("file1.txt", "content1");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-l"});

  TEST_LOG_CMD_LIST("ls.exe", L"-l");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ls.exe -l output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Verify the output contains the expected file in long format
  EXPECT_TRUE(r.stdout_text.find("file1.txt") != std::string::npos);
}

TEST(ls, ls_long_format_reports_hardlink_count) {
  TempDir tmp;
  tmp.write("original.txt", "content");

  std::filesystem::path original = tmp.path / "original.txt";
  std::filesystem::path alias = tmp.path / "alias.txt";
  bool created = CreateHardLinkW(alias.wstring().c_str(),
                                 original.wstring().c_str(), nullptr) != FALSE;
  EXPECT_TRUE(created);
  if (!created) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-l", L"original.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(
      std::regex_search(r.stdout_text, std::regex(R"([dl-][rwx-]{9}\s+2\s+)")));
}

TEST(ls, ls_long_format_uses_windows_writable_permission_shape_for_regular_file) {
  TempDir tmp;
  tmp.write("plain.txt", "content");

  Pipeline writable;
  writable.set_cwd(tmp.wpath());
  writable.add(L"ls.exe", {L"-l", L"plain.txt"});
  auto writable_result = writable.run();

  EXPECT_EQ(writable_result.exit_code, 0);
  EXPECT_TRUE(std::regex_search(
      writable_result.stdout_text,
      std::regex(R"(^-rwxrwxrwx\s+\d+\s+.*plain\.txt\n?$)")));

  DWORD attrs = GetFileAttributesW((tmp.path / "plain.txt").wstring().c_str());
  EXPECT_NE(attrs, INVALID_FILE_ATTRIBUTES);
  if (attrs == INVALID_FILE_ATTRIBUTES) return;
  bool readonly_set = SetFileAttributesW(
                          (tmp.path / "plain.txt").wstring().c_str(),
                          attrs | FILE_ATTRIBUTE_READONLY) != FALSE;
  EXPECT_TRUE(readonly_set);
  if (!readonly_set) return;

  Pipeline readonly;
  readonly.set_cwd(tmp.wpath());
  readonly.add(L"ls.exe", {L"-l", L"plain.txt"});
  auto readonly_result = readonly.run();

  EXPECT_EQ(readonly_result.exit_code, 0);
  EXPECT_TRUE(std::regex_search(
      readonly_result.stdout_text,
      std::regex(R"(^-r-xr-xr-x\s+\d+\s+.*plain\.txt\n?$)")));
}

TEST(ls, ls_long_format_uses_windows_writable_permission_shape_for_directory) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "dir");

  Pipeline writable;
  writable.set_cwd(tmp.wpath());
  writable.add(L"ls.exe", {L"-ld", L"dir"});
  auto writable_result = writable.run();

  EXPECT_EQ(writable_result.exit_code, 0);
  EXPECT_TRUE(std::regex_search(
      writable_result.stdout_text,
      std::regex(R"(^drwxrwxrwx\s+\d+\s+.*dir\n?$)")));

  DWORD attrs = GetFileAttributesW((tmp.path / "dir").wstring().c_str());
  EXPECT_NE(attrs, INVALID_FILE_ATTRIBUTES);
  if (attrs == INVALID_FILE_ATTRIBUTES) return;
  bool readonly_set = SetFileAttributesW(
                          (tmp.path / "dir").wstring().c_str(),
                          attrs | FILE_ATTRIBUTE_READONLY) != FALSE;
  EXPECT_TRUE(readonly_set);
  if (!readonly_set) return;

  Pipeline readonly;
  readonly.set_cwd(tmp.wpath());
  readonly.add(L"ls.exe", {L"-ld", L"dir"});
  auto readonly_result = readonly.run();

  EXPECT_EQ(readonly_result.exit_code, 0);
  EXPECT_TRUE(std::regex_search(
      readonly_result.stdout_text,
      std::regex(R"(^dr-xr-xr-x\s+\d+\s+.*dir\n?$)")));
}

TEST(ls, ls_long_format_shows_symlink_target) {
  TempDir tmp;
  tmp.write("target.txt", "content");

  std::filesystem::path link = tmp.path / "link.txt";
  if (!create_symlink_or_skip(link, std::filesystem::path(L"target.txt"))) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-la"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(
      std::regex_search(r.stdout_text, std::regex(R"(lrwxrwxrwx\s+\d+\s+.*link\.txt -> target\.txt)")));
}

TEST(ls, ls_long_format_command_line_dangling_symlink_operand_is_listed) {
  TempDir tmp;
  tmp.write("target.txt", "content");

  std::filesystem::path link = tmp.path / "dangling.txt";
  if (!create_symlink_or_skip(link, std::filesystem::path(L"target.txt"))) {
    return;
  }
  std::filesystem::remove(tmp.path / "target.txt");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-l", L"dangling.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::regex_search(
      r.stdout_text,
      std::regex(R"(lrwxrwxrwx\s+\d+\s+.*dangling\.txt -> target\.txt)")));
}

TEST(ls, ls_long_format_shows_directory_symlink_as_link) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "targetdir");

  std::filesystem::path link = tmp.path / "dirlink";
  if (!create_symlink_or_skip(link, std::filesystem::path(L"targetdir"),
                              true)) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-la"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::regex_search(
      r.stdout_text,
      std::regex(R"(lrwxrwxrwx\s+\d+\s+.*dirlink -> targetdir)")));
}

TEST(ls, ls_long_format_shows_directory_junction_absolute_target) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "targetdir");

  std::filesystem::path link = tmp.path / "dirlink";
  bool created = create_directory_junction(link, tmp.path / "targetdir");
  EXPECT_TRUE(created);
  if (!created) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-la"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  const std::string expected_target = (tmp.path / "targetdir").string();
  EXPECT_TRUE(r.stdout_text.find("dirlink -> " + expected_target) !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("\\??\\") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("\\\\?\\") == std::string::npos);
}

TEST(ls, ls_long_format_reports_directory_junction_size_as_zero) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "targetdir");

  std::filesystem::path link = tmp.path / "dirlink";
  bool created = create_directory_junction(link, tmp.path / "targetdir");
  EXPECT_TRUE(created);
  if (!created) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-l", L"dirlink"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);

  const std::string expected_target = (tmp.path / "targetdir").string();
  EXPECT_TRUE(r.stdout_text.find("dirlink -> " + expected_target) !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("\\??\\") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("\\\\?\\") == std::string::npos);
  EXPECT_TRUE(std::regex_search(
      r.stdout_text, std::regex(R"(lrwxrwxrwx\s+\d+\s+.*\s0\s+.*dirlink -> )")));
}

TEST(ls, ls_directory_listing_reports_directory_junction_size_as_zero) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "targetdir");

  std::filesystem::path link = tmp.path / "dirlink";
  bool created = create_directory_junction(link, tmp.path / "targetdir");
  EXPECT_TRUE(created);
  if (!created) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-la"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::regex_search(
      r.stdout_text,
      std::regex(R"(lrwxrwxrwx\s+\d+\s+.*\s0\s+.*dirlink -> )")));
}

TEST(ls, ls_long_format_command_line_symlink_dereference_option_shows_target) {
  TempDir tmp;
  tmp.write("target.txt", "content");

  std::filesystem::path link = tmp.path / "link.txt";
  if (!create_symlink_or_skip(link, std::filesystem::path(L"target.txt"))) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-lH", L"link.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("link.txt -> target.txt") == std::string::npos);
  EXPECT_TRUE(std::regex_search(
      r.stdout_text,
      std::regex(R"(^-[rwx-]{9}\s+\d+\s+.*\s7\s+.*link\.txt\n?$)")));
}

TEST(ls, ls_long_format_dereference_option_shows_target_for_symlink_operand) {
  TempDir tmp;
  tmp.write("target.txt", "content");

  std::filesystem::path link = tmp.path / "link.txt";
  if (!create_symlink_or_skip(link, std::filesystem::path(L"target.txt"))) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-lL", L"link.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("link.txt -> target.txt") == std::string::npos);
  EXPECT_TRUE(std::regex_search(
      r.stdout_text,
      std::regex(R"(^-[rwx-]{9}\s+\d+\s+.*\s7\s+.*link\.txt\n?$)")));
}

TEST(ls,
     ls_long_format_dirs_only_dereference_last_keeps_file_symlink_operand_as_link) {
  TempDir tmp;
  tmp.write("target.txt", "content");

  std::filesystem::path link = tmp.path / "link.txt";
  if (!create_symlink_or_skip(link, std::filesystem::path(L"target.txt"))) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe",
        {L"-l", L"-H", L"--dereference-command-line-symlinks-to-dir",
         L"link.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("link.txt -> target.txt") != std::string::npos);
}

TEST(ls, ls_long_format_H_keeps_directory_entry_symlink_as_link) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "targetdir");

  std::filesystem::path link = tmp.path / "dirlink";
  bool created = create_directory_junction(link, tmp.path / "targetdir");
  EXPECT_TRUE(created);
  if (!created) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-lH", L"."});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("dirlink ->") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("targetdir") != std::string::npos);
}

TEST(ls, ls_long_format_L_dereferences_directory_entry_symlink) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "targetdir");

  std::filesystem::path link = tmp.path / "dirlink";
  bool created = create_directory_junction(link, tmp.path / "targetdir");
  EXPECT_TRUE(created);
  if (!created) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-lL", L"."});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("dirlink -> targetdir") == std::string::npos);
  EXPECT_TRUE(std::regex_search(
      r.stdout_text,
      std::regex(R"(^d[rwx-]{9}\s+\d+\s+.*dirlink$)",
                 std::regex_constants::multiline)));
}

TEST(ls, ls_long_format_last_dereference_option_wins_for_directory_entry_symlink) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "targetdir");

  std::filesystem::path link = tmp.path / "dirlink";
  bool created = create_directory_junction(link, tmp.path / "targetdir");
  EXPECT_TRUE(created);
  if (!created) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-l", L"-L", L"-H", L"."});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("dirlink ->") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("targetdir") != std::string::npos);
}

TEST(ls, ls_recursive_does_not_follow_directory_symlinks_by_default) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "targetdir");
  tmp.write("targetdir\\nested.txt", "content");

  std::filesystem::path link = tmp.path / "dirlink";
  if (!create_symlink_or_skip(link, std::filesystem::path(L"targetdir"),
                              true)) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-R"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("dirlink") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("./targetdir:") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("./dirlink:") == std::string::npos);
}

TEST(ls, ls_recursive_dereference_follows_directory_symlinks) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "targetdir");
  tmp.write("targetdir\\nested.txt", "content");

  std::filesystem::path link = tmp.path / "dirlink";
  if (!create_symlink_or_skip(link, std::filesystem::path(L"targetdir"),
                              true)) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-RL"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("./targetdir:") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("./dirlink:") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("nested.txt") != std::string::npos);
}

TEST(ls, ls_recursive_last_dereference_option_wins_to_stop_following_directory_symlinks) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "targetdir");
  tmp.write("targetdir\\nested.txt", "content");

  std::filesystem::path link = tmp.path / "dirlink";
  if (!create_symlink_or_skip(link, std::filesystem::path(L"targetdir"),
                              true)) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-R", L"-L", L"-H"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("./targetdir:") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("./dirlink:") == std::string::npos);
}

TEST(ls, ls_recursive_does_not_follow_directory_junctions_by_default) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "targetdir");
  tmp.write("targetdir\\nested.txt", "content");

  std::filesystem::path link = tmp.path / "dirlink";
  bool created = create_directory_junction(link, tmp.path / "targetdir");
  EXPECT_TRUE(created);
  if (!created) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-R"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("dirlink") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("./targetdir:") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("./dirlink:") == std::string::npos);
}

TEST(ls, ls_recursive_dereference_follows_directory_junctions) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "targetdir");
  tmp.write("targetdir\\nested.txt", "content");

  std::filesystem::path link = tmp.path / "dirlink";
  bool created = create_directory_junction(link, tmp.path / "targetdir");
  EXPECT_TRUE(created);
  if (!created) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-RL"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("./targetdir:") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("./dirlink:") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("nested.txt") != std::string::npos);
}

TEST(ls,
     ls_recursive_last_dereference_option_wins_to_stop_following_directory_junctions) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "targetdir");
  tmp.write("targetdir\\nested.txt", "content");

  std::filesystem::path link = tmp.path / "dirlink";
  bool created = create_directory_junction(link, tmp.path / "targetdir");
  EXPECT_TRUE(created);
  if (!created) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-R", L"-L", L"-H"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("./targetdir:") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("./dirlink:") == std::string::npos);
}

TEST(ls, ls_command_line_directory_symlink_lists_contents_in_default_mode) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "targetdir");
  tmp.write("targetdir\\inside.txt", "content");

  std::filesystem::path link = tmp.path / "dirlink";
  if (!create_symlink_or_skip(link, std::filesystem::path(L"targetdir"),
                              true)) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"dirlink"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("inside.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("dirlink -> targetdir") == std::string::npos);
}

TEST(ls, ls_long_format_command_line_directory_symlink_shows_link_by_default) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "targetdir");
  tmp.write("targetdir\\inside.txt", "content");

  std::filesystem::path link = tmp.path / "dirlink";
  if (!create_symlink_or_skip(link, std::filesystem::path(L"targetdir"),
                              true)) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-l", L"dirlink"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::regex_search(
      r.stdout_text,
      std::regex(R"(lrwxrwxrwx\s+\d+\s+.*dirlink -> targetdir)")));
  EXPECT_TRUE(r.stdout_text.find("inside.txt") == std::string::npos);
}

TEST(ls, ls_dereference_command_line_symlink_to_dir_option_follows_in_long_mode) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "targetdir");
  tmp.write("targetdir\\inside.txt", "content");

  std::filesystem::path link = tmp.path / "dirlink";
  if (!create_symlink_or_skip(link, std::filesystem::path(L"targetdir"),
                              true)) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe",
        {L"-l", L"--dereference-command-line-symlinks-to-dir", L"dirlink"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("inside.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("dirlink -> targetdir") == std::string::npos);
}

TEST(ls, ls_dereference_command_line_symlink_to_dir_singular_alias_follows_in_long_mode) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "targetdir");
  tmp.write("targetdir\\inside.txt", "content");

  std::filesystem::path link = tmp.path / "dirlink";
  if (!create_symlink_or_skip(link, std::filesystem::path(L"targetdir"),
                              true)) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe",
        {L"-l", L"--dereference-command-line-symlink-to-dir", L"dirlink"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("inside.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("dirlink -> targetdir") == std::string::npos);
}

TEST(ls, ls_missing_operand_reports_cannot_access_on_stderr) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_EQ(r.stdout_text, "");
  EXPECT_TRUE(
      r.stderr_text.find("ls: cannot access 'missing.txt': No such file or directory") !=
      std::string::npos);
}

TEST(ls, ls_missing_operand_keeps_directory_junction_header_for_other_operand) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "targetdir");
  tmp.write("targetdir\\inside.txt", "content");

  std::filesystem::path link = tmp.path / "dirjunc";
  bool created = create_directory_junction(link, tmp.path / "targetdir");
  EXPECT_TRUE(created);
  if (!created) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-lH", L"missing", L"dirjunc"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(
      r.stderr_text.find("ls: cannot access 'missing': No such file or directory") !=
      std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("dirjunc:\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("inside.txt") != std::string::npos);
}

TEST(ls, ls_long_format_classify_marks_directory_symlink_name_not_target) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "targetdir");

  std::filesystem::path link = tmp.path / "dirlink";
  bool created = create_directory_junction(link, tmp.path / "targetdir");
  EXPECT_TRUE(created);
  if (!created) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-ldF", L"dirlink"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("dirlink@ ->") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("targetdir/") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("targetdir") != std::string::npos);
}

TEST(ls, ls_long_format_H_dereferences_directory_junction_operand) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "targetdir");

  std::filesystem::path link = tmp.path / "dirlink";
  bool created = create_directory_junction(link, tmp.path / "targetdir");
  EXPECT_TRUE(created);
  if (!created) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-ldH", L"dirlink"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("dirlink ->") == std::string::npos);
  EXPECT_TRUE(std::regex_search(
      r.stdout_text, std::regex(R"(^d[rwx-]{9}\s+\d+\s+.*dirlink\n?$)")));
}

TEST(ls, ls_long_format_L_dereferences_directory_junction_operand) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "targetdir");

  std::filesystem::path link = tmp.path / "dirlink";
  bool created = create_directory_junction(link, tmp.path / "targetdir");
  EXPECT_TRUE(created);
  if (!created) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-ldL", L"dirlink"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("dirlink ->") == std::string::npos);
  EXPECT_TRUE(std::regex_search(
      r.stdout_text, std::regex(R"(^d[rwx-]{9}\s+\d+\s+.*dirlink\n?$)")));
}

TEST(ls, ls_long_format_L_directory_junction_contents_dereference_dot_entry) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "targetdir");
  tmp.write("targetdir/inside.txt", "inside");

  std::filesystem::path link = tmp.path / "dirlink";
  bool created = create_directory_junction(link, tmp.path / "targetdir");
  EXPECT_TRUE(created);
  if (!created) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-laL", L"dirlink"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(". ->") == std::string::npos);
  EXPECT_TRUE(std::regex_search(
      r.stdout_text, std::regex(R"((^|\n)d[rwx-]{9}\s+\d+\s+.*\s\.\n)")));
}

TEST(ls, ls_long_directory_prints_total_line) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");
  tmp.write("file2.txt", "content2");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-l"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(
      std::regex_search(r.stdout_text, std::regex(R"(^total\s+\d+\n)")));
  EXPECT_TRUE(r.stdout_text.find("file1.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file2.txt") != std::string::npos);
}

TEST(ls, ls_size_directory_prints_total_line) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-1s"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(
      std::regex_search(r.stdout_text, std::regex(R"(^total\s+\d+\n)")));
  EXPECT_TRUE(r.stdout_text.find("file1.txt") != std::string::npos);
}

TEST(ls, ls_size_uses_allocated_bytes_by_default_on_windows) {
  TempDir tmp;
  tmp.write("sample.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-ls", L"sample.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::regex_search(
      r.stdout_text,
      std::regex(R"(^7\s+-[rwx-]{9}\s+\d+\s+.*\s7\s+.*sample\.txt\n?$)")));
}

TEST(ls, ls_long_directory_total_uses_allocated_bytes_by_default_on_windows) {
  TempDir tmp;
  tmp.write("sample.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-l"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("total 7\n") == 0);
  EXPECT_TRUE(r.stdout_text.find("sample.txt") != std::string::npos);
}

TEST(ls, ls_long_format_dotdot_uses_parent_directory_metadata) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "child");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-la"});

  auto r = p.run();

  Pipeline parent;
  parent.set_cwd(tmp.wpath());
  parent.add(L"ls.exe", {L"-ld", L".."});

  auto parent_result = parent.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(parent_result.exit_code, 0);

  std::smatch match;
  EXPECT_TRUE(std::regex_search(
      parent_result.stdout_text, match,
      std::regex(R"(^drwx[rwx-]*\s+\d+\s+\S+\s+\S+\s+(\d+)\s+.*\.\.\n?$)")));
  if (match.size() < 2) return;
  const auto expected_parent_size = match[1].str();

  EXPECT_TRUE(std::regex_search(
      r.stdout_text,
      std::regex("^drwx[rwx-]*\\s+\\d+\\s+\\S+\\s+\\S+\\s+" +
                     expected_parent_size + "\\s+.*\\.\\.$",
                 std::regex::multiline)));
  if (expected_parent_size != "0") {
    EXPECT_TRUE(r.stdout_text.find("total 0\n") == std::string::npos);
  }
}

TEST(ls, ls_all) {
  TempDir tmp;
  tmp.write("file.txt", "content");
  tmp.write(".hidden.txt", "hidden content");

  TEST_LOG_FILE_CONTENT("file.txt", "content");
  TEST_LOG_FILE_CONTENT(".hidden.txt", "hidden content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-a"});

  TEST_LOG_CMD_LIST("ls.exe", L"-a");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ls.exe -a output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Verify the output contains both regular and hidden files
  EXPECT_TRUE(r.stdout_text.find("file.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find(".hidden.txt") != std::string::npos);
}

TEST(ls, ls_default_hides_dot_prefixed_names) {
  TempDir tmp;
  tmp.write("file.txt", "content");
  tmp.write(".hidden.txt", "hidden content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("file.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find(".hidden.txt") == std::string::npos);
}

TEST(ls, ls_almost_all_shows_dot_prefixed_names_but_not_dots) {
  TempDir tmp;
  tmp.write("file.txt", "content");
  tmp.write(".hidden.txt", "hidden content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-A"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("file.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find(".hidden.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("\n.\n") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("\n..\n") == std::string::npos);
}

TEST(ls, ls_single_file) {
  TempDir tmp;
  tmp.write("myfile.txt", "test content");

  TEST_LOG_FILE_CONTENT("myfile.txt", "test content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"myfile.txt"});

  TEST_LOG_CMD_LIST("ls.exe", L"myfile.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ls.exe myfile.txt output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("myfile.txt") != std::string::npos);
}

TEST(ls, ls_wildcard) {
  TempDir tmp;
  tmp.write("test1.txt", "content1");
  tmp.write("test2.txt", "content2");
  tmp.write("other.log", "log content");

  TEST_LOG_FILE_CONTENT("test1.txt", "content1");
  TEST_LOG_FILE_CONTENT("test2.txt", "content2");
  TEST_LOG_FILE_CONTENT("other.log", "log content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"*.txt"});

  TEST_LOG_CMD_LIST("ls.exe", L"*.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ls.exe *.txt output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("test1.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("test2.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("other.log") == std::string::npos);
}

TEST(ls, ls_multiple_file_operands_do_not_print_headers) {
  TempDir tmp;
  tmp.write("one.txt", "one");
  tmp.write("two.txt", "two");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"one.txt", L"two.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("one.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("two.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("one.txt:") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("two.txt:") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("\n\n") == std::string::npos);
}

TEST(ls, ls_mixed_file_and_directory_prints_directory_header) {
  TempDir tmp;
  tmp.write("top.txt", "top");
  std::filesystem::create_directories(tmp.path / "sub");
  tmp.write("sub/inside.txt", "inside");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"top.txt", L"sub"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("top.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("sub:") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("inside.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("top.txt:") == std::string::npos);
}

TEST(ls, ls_wildcard_directory_lists_contents_unless_directory_option) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "dirA");
  tmp.write("dirA/inside.txt", "inside");

  Pipeline contents;
  contents.set_cwd(tmp.wpath());
  contents.add(L"ls.exe", {L"dir*"});
  auto contents_result = contents.run();

  EXPECT_EQ(contents_result.exit_code, 0);
  EXPECT_TRUE(contents_result.stdout_text.find("inside.txt") !=
              std::string::npos);

  Pipeline directory_only;
  directory_only.set_cwd(tmp.wpath());
  directory_only.add(L"ls.exe", {L"-d", L"dir*"});
  auto directory_result = directory_only.run();

  EXPECT_EQ(directory_result.exit_code, 0);
  EXPECT_TRUE(directory_result.stdout_text.find("dirA") != std::string::npos);
  EXPECT_TRUE(directory_result.stdout_text.find("inside.txt") ==
              std::string::npos);
}

TEST(ls, ls_char_class_wildcard) {
  TempDir tmp;
  tmp.write("a1.txt", "a1");
  tmp.write("a2.txt", "a2");
  tmp.write("b1.txt", "b1");
  tmp.write("c.log", "c");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"a?.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("a1.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("a2.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b1.txt") == std::string::npos);
}

TEST(ls, ls_char_class_range_wildcard) {
  TempDir tmp;
  tmp.write("a.txt", "a");
  tmp.write("b.txt", "b");
  tmp.write("c.txt", "c");
  tmp.write("d.txt", "d");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"[ab]*.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("a.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("c.txt") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("d.txt") == std::string::npos);
}

TEST(ls, ls_negated_char_class_wildcard) {
  TempDir tmp;
  tmp.write("apple.txt", "a");
  tmp.write("banana.txt", "b");
  tmp.write("cherry.txt", "c");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"[!b]*.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("apple.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("cherry.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("banana.txt") == std::string::npos);
}

TEST(ls, ls_char_class_wildcard_in_parent_directory_segment) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "d1");
  std::filesystem::create_directories(tmp.path / "d2");
  std::filesystem::create_directories(tmp.path / "d3");
  tmp.write("d1/a.txt", "a");
  tmp.write("d2/b.txt", "b");
  tmp.write("d3/c.txt", "c");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"d[12]\\*.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("a.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("c.txt") == std::string::npos);
}

TEST(ls, ls_char_class_allows_literal_closing_bracket) {
  TempDir tmp;
  tmp.write("].txt", "]");
  tmp.write("a.txt", "a");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"[]].txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("].txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("a.txt") == std::string::npos);
}

TEST(ls, ls_negated_char_class_allows_literal_closing_bracket) {
  TempDir tmp;
  tmp.write("].txt", "]");
  tmp.write("a.txt", "a");
  tmp.write("b.txt", "b");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"[!]].txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("a.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("].txt") == std::string::npos);
}

TEST(ls, ls_char_class_prefers_glob_expansion_over_existing_literal_path) {
  TempDir tmp;
  tmp.write("[ab].txt", "literal");
  tmp.write("a.txt", "a");
  tmp.write("b.txt", "b");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"[ab].txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("a.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("[ab].txt") == std::string::npos);
}

TEST(ls, ls_directory_only) {
  TempDir tmp;
  tmp.write("file.txt", "content");

  TEST_LOG_FILE_CONTENT("file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-d", L"."});

  TEST_LOG_CMD_LIST("ls.exe", L"-d", L".");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ls.exe -d . output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should display the directory itself, not its contents
  EXPECT_TRUE(r.stdout_text.find(".") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file.txt") == std::string::npos);
}

TEST(ls, ls_directory_only_preserves_current_directory_operand_spelling) {
  TempDir tmp;
  tmp.write("file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-dR", L"."});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, ".\n");
}

TEST(ls, ls_directory_only_accepts_trailing_slash_operand) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "dir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-ld", L"dir/"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_TRUE(std::regex_search(
      r.stdout_text, std::regex(R"(^d[rwx-]{9}\s+\d+\s+.*dir/\n?$)")));
}

TEST(ls, ls_directory_only_accepts_trailing_slash_junction_operand) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "targetdir");

  std::filesystem::path link = tmp.path / "dirjunc";
  bool created = create_directory_junction(link, tmp.path / "targetdir");
  EXPECT_TRUE(created);
  if (!created) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-ld", L"dirjunc/"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_TRUE(r.stdout_text.find("dirjunc/ -> ") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("\\??\\") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("\\\\?\\") == std::string::npos);
}

TEST(ls, ls_file_operand_preserves_relative_path_spelling) {
  TempDir tmp;
  tmp.write("nested/file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L".\\nested\\file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, ".\\nested\\file.txt\n");
}

TEST(ls, ls_time_sort) {
  TempDir tmp;
  tmp.write("old.txt", "old content");
  tmp.write("new.txt", "new content");

  TEST_LOG_FILE_CONTENT("old.txt", "old content");
  TEST_LOG_FILE_CONTENT("new.txt", "new content");

  // Use touch -d to set different file times
  // Set old.txt to an older time: 2025-01-01 10:00
  Pipeline touch_old;
  touch_old.set_cwd(tmp.wpath());
  touch_old.add(L"touch.exe", {L"-d", L"202501011000", L"old.txt"});
  touch_old.run();

  // Set new.txt to a newer time: 2025-01-01 12:00
  Pipeline touch_new;
  touch_new.set_cwd(tmp.wpath());
  touch_new.add(L"touch.exe", {L"-d", L"202501011200", L"new.txt"});
  touch_new.run();

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-lt"});

  TEST_LOG_CMD_LIST("ls.exe", L"-lt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ls.exe -lt output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Check that both files are in the output
  bool has_new = r.stdout_text.find("new.txt") != std::string::npos;
  bool has_old = r.stdout_text.find("old.txt") != std::string::npos;
  EXPECT_TRUE(has_new);
  EXPECT_TRUE(has_old);

  // In -t mode, newer files should appear before older ones
  // Since new.txt is set to 12:00 and old.txt to 10:00,
  // new.txt should appear first in the output
  size_t new_pos = r.stdout_text.find("new.txt");
  size_t old_pos = r.stdout_text.find("old.txt");

  if (has_new && has_old) {
    EXPECT_LT(new_pos, old_pos);
  }
}

TEST(ls, ls_size_sort) {
  TempDir tmp;
  tmp.write("small.txt", "x");
  tmp.write("large.txt", std::string(1000, 'x'));

  TEST_LOG_FILE_CONTENT("small.txt", "x");
  TEST_LOG_FILE_CONTENT("large.txt", std::string(1000, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-lS"});

  TEST_LOG_CMD_LIST("ls.exe", L"-lS");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ls.exe -lS output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("small.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("large.txt") != std::string::npos);

  // In -S mode, larger files should appear before smaller ones
  size_t large_pos = r.stdout_text.find("large.txt");
  size_t small_pos = r.stdout_text.find("small.txt");
  EXPECT_LT(large_pos, small_pos);
}

TEST(ls, ls_sort_option_last_wins) {
  TempDir tmp;
  tmp.write("big_old.txt", std::string(1000, 'x'));
  tmp.write("small_new.txt", "x");

  Pipeline touch_old;
  touch_old.set_cwd(tmp.wpath());
  touch_old.add(L"touch.exe", {L"-d", L"202501011000", L"big_old.txt"});
  touch_old.run();

  Pipeline touch_new;
  touch_new.set_cwd(tmp.wpath());
  touch_new.add(L"touch.exe", {L"-d", L"202501011200", L"small_new.txt"});
  touch_new.run();

  Pipeline size_last;
  size_last.set_cwd(tmp.wpath());
  size_last.add(L"ls.exe", {L"-1", L"-tS"});
  auto size_last_result = size_last.run();

  EXPECT_EQ(size_last_result.exit_code, 0);
  EXPECT_LT(size_last_result.stdout_text.find("big_old.txt"),
            size_last_result.stdout_text.find("small_new.txt"));

  Pipeline time_last;
  time_last.set_cwd(tmp.wpath());
  time_last.add(L"ls.exe", {L"-1", L"-St"});
  auto time_last_result = time_last.run();

  EXPECT_EQ(time_last_result.exit_code, 0);
  EXPECT_LT(time_last_result.stdout_text.find("small_new.txt"),
            time_last_result.stdout_text.find("big_old.txt"));
}

TEST(ls, ls_invalid_sort_and_time_options_fail) {
  TempDir tmp;
  tmp.write("sample.txt", "sample");

  Pipeline bad_sort;
  bad_sort.set_cwd(tmp.wpath());
  bad_sort.add(L"ls.exe", {L"--sort=bad"});
  auto bad_sort_result = bad_sort.run();

  EXPECT_NE(bad_sort_result.exit_code, 0);

  Pipeline bad_time;
  bad_time.set_cwd(tmp.wpath());
  bad_time.add(L"ls.exe", {L"--time=bad"});
  auto bad_time_result = bad_time.run();

  EXPECT_NE(bad_time_result.exit_code, 0);
}

TEST(ls, ls_comma_format) {
  TempDir tmp;
  tmp.write("a.txt", "a");
  tmp.write("b.txt", "b");
  tmp.write("c.txt", "c");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-m"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("a.txt, b.txt, c.txt") != std::string::npos);
}

TEST(ls, ls_comma_format_wraps_like_gnu_width_logic) {
  TempDir tmp;
  tmp.write("a", "a");
  tmp.write("bb", "bb");
  tmp.write("ccc", "ccc");
  tmp.write("dddd", "dddd");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-m", L"-w", L"10"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a, bb,\nccc, dddd\n");
}

TEST(ls, ls_horizontal_layout) {
  TempDir tmp;
  tmp.write("a", "a");
  tmp.write("b", "b");
  tmp.write("c", "c");
  tmp.write("d", "d");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-x", L"-w", L"7"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  auto newline = r.stdout_text.find('\n');
  EXPECT_NE(newline, std::string::npos);
  if (newline == std::string::npos) return;
  std::string first_line = r.stdout_text.substr(0, newline);
  EXPECT_TRUE(first_line.find("a") != std::string::npos);
  EXPECT_TRUE(first_line.find("b") != std::string::npos);
  EXPECT_TRUE(first_line.find("c") != std::string::npos);
}

TEST(ls, ls_horizontal_layout_fits_boundary_width_like_gnu) {
  TempDir tmp;
  tmp.write("a", "a");
  tmp.write("b", "b");
  tmp.write("c", "c");
  tmp.write("d", "d");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-x", L"-w", L"7"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a  b  c\nd\n");
}

TEST(ls, ls_horizontal_layout_uses_tab_stops_for_wide_columns) {
  TempDir tmp;
  tmp.write("aa", "aa");
  tmp.write("bb", "bb");
  tmp.write("cc", "cc");
  tmp.write("dd", "dd");
  tmp.write("ee", "ee");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-x", L"-w", L"12"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "aa  bb\tcc\ndd  ee\n");
}

TEST(ls, ls_column_layout_uses_tab_stops_for_wide_columns) {
  TempDir tmp;
  tmp.write("aa", "aa");
  tmp.write("bb", "bb");
  tmp.write("cc", "cc");
  tmp.write("dd", "dd");
  tmp.write("ee", "ee");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-C", L"-w", L"12"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "aa  cc\tee\nbb  dd\n");
}

TEST(ls, ls_horizontal_layout_honors_custom_tabsize) {
  TempDir tmp;
  tmp.write("aa", "aa");
  tmp.write("bb", "bb");
  tmp.write("cc", "cc");
  tmp.write("dd", "dd");
  tmp.write("ee", "ee");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-x", L"-w", L"20", L"-T", L"3"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "aa\t bb  cc\tdd\t ee\n");
}

TEST(ls, ls_column_layout_honors_custom_tabsize) {
  TempDir tmp;
  tmp.write("aa", "aa");
  tmp.write("bb", "bb");
  tmp.write("cc", "cc");
  tmp.write("dd", "dd");
  tmp.write("ee", "ee");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-C", L"-w", L"20", L"--tabsize", L"5"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "aa  bb  cc  dd\t ee\n");
}

TEST(ls, ls_horizontal_layout_tabsize_last_occurrence_wins_across_aliases) {
  TempDir tmp;
  tmp.write("aa", "aa");
  tmp.write("bb", "bb");
  tmp.write("cc", "cc");
  tmp.write("dd", "dd");
  tmp.write("ee", "ee");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe",
        {L"-x", L"-w", L"20", L"--tabsize", L"8", L"-T", L"3"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "aa\t bb  cc\tdd\t ee\n");
}

TEST(ls, ls_column_layout_tabsize_last_occurrence_wins_across_aliases) {
  TempDir tmp;
  tmp.write("aa", "aa");
  tmp.write("bb", "bb");
  tmp.write("cc", "cc");
  tmp.write("dd", "dd");
  tmp.write("ee", "ee");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe",
        {L"-C", L"-w", L"20", L"-T", L"3", L"--tabsize", L"5"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "aa  bb  cc  dd\t ee\n");
}

TEST(ls, ls_format_options_last_occurrence_wins) {
  TempDir tmp;
  tmp.write("a.txt", "a");
  tmp.write("b.txt", "b");
  tmp.write("c.txt", "c");

  Pipeline short_last_wins;
  short_last_wins.set_cwd(tmp.wpath());
  short_last_wins.add(L"ls.exe", {L"-1", L"-m"});

  auto short_last_wins_result = short_last_wins.run();

  EXPECT_EQ(short_last_wins_result.exit_code, 0);
  EXPECT_TRUE(short_last_wins_result.stdout_text.find("a.txt, b.txt, c.txt") !=
              std::string::npos);

  Pipeline long_last_wins;
  long_last_wins.set_cwd(tmp.wpath());
  long_last_wins.add(L"ls.exe", {L"--format=single-column", L"-m"});

  auto long_last_wins_result = long_last_wins.run();

  EXPECT_EQ(long_last_wins_result.exit_code, 0);
  EXPECT_TRUE(long_last_wins_result.stdout_text.find("a.txt, b.txt, c.txt") !=
              std::string::npos);

  Pipeline later_long_wins;
  later_long_wins.set_cwd(tmp.wpath());
  later_long_wins.add(L"ls.exe", {L"-m", L"--format=single-column"});

  auto later_long_wins_result = later_long_wins.run();

  EXPECT_EQ(later_long_wins_result.exit_code, 0);
  EXPECT_EQ_TEXT(later_long_wins_result.stdout_text, "a.txt\nb.txt\nc.txt\n");
}

TEST(ls, ls_default_non_tty_output_uses_single_column) {
  TempDir tmp;
  tmp.write("a.txt", "a");
  tmp.write("b.txt", "b");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a.txt\nb.txt\n");
}

TEST(ls, ls_explicit_columns_override_non_tty_default) {
  TempDir tmp;
  tmp.write("a.txt", "a");
  tmp.write("b.txt", "b");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-C"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a.txt  b.txt\n");
}

TEST(ls,
     ls_format_single_column_overrides_long_for_command_line_directory_symlink) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "targetdir");
  tmp.write("targetdir\\inside.txt", "content");

  std::filesystem::path link = tmp.path / "dirlink";
  bool created = create_directory_junction(link, tmp.path / "targetdir");
  EXPECT_TRUE(created);
  if (!created) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-l", L"--format=single-column", L"dirlink"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "inside.txt\n");
  EXPECT_TRUE(r.stdout_text.find("dirlink -> targetdir") == std::string::npos);
}

TEST(ls, ls_invalid_format_fails) {
  TempDir tmp;
  tmp.write("a.txt", "a");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"--format=bogus"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
}

TEST(ls, ls_format_verbose_alias_matches_long) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"--format=verbose", L"file1.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::regex_search(
      r.stdout_text,
      std::regex(R"(^-[rwx-]{9}\s+\d+\s+.*file1\.txt\n?$)")));
}

TEST(ls, ls_g_implies_long_format_without_owner) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-g", L"-n", L"file1.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::regex_search(
      r.stdout_text,
      std::regex(R"(^-[rwx-]{9}\s+\d+\s+\d+\s+\d+\s+[A-Z][a-z]{2}\s+.*file1\.txt\n?$)")));
  EXPECT_FALSE(std::regex_search(
      r.stdout_text,
      std::regex(R"(^-[rwx-]{9}\s+\d+\s+\d+\s+\d+\s+\d+\s+[A-Z][a-z]{2}\s+.*file1\.txt\n?$)")));
}

TEST(ls, ls_o_implies_long_format_without_group) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-o", L"-n", L"file1.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::regex_search(
      r.stdout_text,
      std::regex(R"(^-[rwx-]{9}\s+\d+\s+\d+\s+\d+\s+[A-Z][a-z]{2}\s+.*file1\.txt\n?$)")));
  EXPECT_FALSE(std::regex_search(
      r.stdout_text,
      std::regex(R"(^-[rwx-]{9}\s+\d+\s+\d+\s+\d+\s+\d+\s+[A-Z][a-z]{2}\s+.*file1\.txt\n?$)")));
}

TEST(ls, ls_n_implies_long_format_with_numeric_owner_group) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-n", L"file1.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::regex_search(
      r.stdout_text,
      std::regex(R"(^-[rwx-]{9}\s+\d+\s+\d+\s+\d+\s+\d+\s+.*file1\.txt\n?$)")));
}

TEST(ls, ls_author_is_accepted_without_long_format) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"--author", L"file1.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "file1.txt\n");
}

TEST(ls, ls_author_adds_author_column_in_long_format) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-lo", L"--author", L"file1.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::regex_search(
      r.stdout_text,
      std::regex(
          R"(^-[rwx-]{9}\s+\d+\s+(\S+)\s+\1\s+\d+\s+[A-Z][a-z]{2}\s+.*file1\.txt\n?$)")));
}

TEST(ls, ls_access_time_sort) {
  TempDir tmp;
  tmp.write("old.txt", "old");
  tmp.write("new.txt", "new");

  Pipeline touch_old;
  touch_old.set_cwd(tmp.wpath());
  touch_old.add(L"touch.exe", {L"-a", L"-d", L"202501011000", L"old.txt"});
  touch_old.run();

  Pipeline touch_new;
  touch_new.set_cwd(tmp.wpath());
  touch_new.add(L"touch.exe", {L"-a", L"-d", L"202501011200", L"new.txt"});
  touch_new.run();

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-1", L"-u"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  size_t new_pos = r.stdout_text.find("new.txt");
  size_t old_pos = r.stdout_text.find("old.txt");
  EXPECT_TRUE(new_pos != std::string::npos);
  EXPECT_TRUE(old_pos != std::string::npos);
  EXPECT_LT(new_pos, old_pos);
}

TEST(ls, ls_birth_time_sort) {
  TempDir tmp;
  tmp.write("old.txt", "old");
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  tmp.write("new.txt", "new");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-1", L"-c"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  size_t new_pos = r.stdout_text.find("new.txt");
  size_t old_pos = r.stdout_text.find("old.txt");
  EXPECT_TRUE(new_pos != std::string::npos);
  EXPECT_TRUE(old_pos != std::string::npos);
  EXPECT_LT(new_pos, old_pos);
}

TEST(ls, ls_recursive) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "subdir1");
  std::filesystem::create_directory(tmp.path / "subdir2");
  tmp.write("subdir1/file1.txt", "content1");
  tmp.write("subdir2/file2.txt", "content2");
  tmp.write("root.txt", "root content");

  TEST_LOG_FILE_CONTENT("subdir1/file1.txt", "content1");
  TEST_LOG_FILE_CONTENT("subdir2/file2.txt", "content2");
  TEST_LOG_FILE_CONTENT("root.txt", "root content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-R"});

  TEST_LOG_CMD_LIST("ls.exe", L"-R");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ls.exe -R output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should contain root directory contents
  EXPECT_TRUE(r.stdout_text.find("root.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("subdir1") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("subdir2") != std::string::npos);
  // Should contain subdirectory contents
  EXPECT_TRUE(r.stdout_text.find("file1.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file2.txt") != std::string::npos);
}

TEST(ls, ls_reverse_sort) {
  TempDir tmp;
  tmp.write("aaa.txt", "a");
  tmp.write("bbb.txt", "b");
  tmp.write("ccc.txt", "c");

  TEST_LOG_FILE_CONTENT("aaa.txt", "a");
  TEST_LOG_FILE_CONTENT("bbb.txt", "b");
  TEST_LOG_FILE_CONTENT("ccc.txt", "c");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-r"});

  TEST_LOG_CMD_LIST("ls.exe", L"-r");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ls.exe -r output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("aaa.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("bbb.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("ccc.txt") != std::string::npos);

  // In reverse mode, files should appear in reverse alphabetical order
  size_t aaa_pos = r.stdout_text.find("aaa.txt");
  size_t bbb_pos = r.stdout_text.find("bbb.txt");
  size_t ccc_pos = r.stdout_text.find("ccc.txt");
  EXPECT_GT(aaa_pos, bbb_pos);
  EXPECT_GT(bbb_pos, ccc_pos);
}

TEST(ls, ls_extension_sort) {
  TempDir tmp;
  tmp.write("README", "readme");
  tmp.write("alpha.log", "log");
  tmp.write("beta.txt", "txt");
  tmp.write("gamma.txt", "txt2");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-1", L"-X"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  size_t readme_pos = r.stdout_text.find("README");
  size_t log_pos = r.stdout_text.find("alpha.log");
  size_t beta_pos = r.stdout_text.find("beta.txt");
  size_t gamma_pos = r.stdout_text.find("gamma.txt");
  EXPECT_TRUE(readme_pos != std::string::npos);
  EXPECT_TRUE(log_pos != std::string::npos);
  EXPECT_TRUE(beta_pos != std::string::npos);
  EXPECT_TRUE(gamma_pos != std::string::npos);
  EXPECT_LT(readme_pos, log_pos);
  EXPECT_LT(log_pos, beta_pos);
  EXPECT_LT(beta_pos, gamma_pos);
}

TEST(ls, ls_extension_sort_long_option) {
  TempDir tmp;
  tmp.write("README", "readme");
  tmp.write("alpha.log", "log");
  tmp.write("beta.txt", "txt");
  tmp.write("gamma.txt", "txt2");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-1", L"--sort=extension"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  size_t readme_pos = r.stdout_text.find("README");
  size_t log_pos = r.stdout_text.find("alpha.log");
  size_t beta_pos = r.stdout_text.find("beta.txt");
  size_t gamma_pos = r.stdout_text.find("gamma.txt");
  EXPECT_TRUE(readme_pos != std::string::npos);
  EXPECT_TRUE(log_pos != std::string::npos);
  EXPECT_TRUE(beta_pos != std::string::npos);
  EXPECT_TRUE(gamma_pos != std::string::npos);
  EXPECT_LT(readme_pos, log_pos);
  EXPECT_LT(log_pos, beta_pos);
  EXPECT_LT(beta_pos, gamma_pos);
}

TEST(ls, ls_group_directories_first) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "zdir");
  std::filesystem::create_directory(tmp.path / "adir");
  tmp.write("b.txt", "b");
  tmp.write("a.txt", "a");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-1", L"--group-directories-first"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  size_t adir_pos = r.stdout_text.find("adir");
  size_t zdir_pos = r.stdout_text.find("zdir");
  size_t a_pos = r.stdout_text.find("a.txt");
  size_t b_pos = r.stdout_text.find("b.txt");
  EXPECT_TRUE(adir_pos != std::string::npos);
  EXPECT_TRUE(zdir_pos != std::string::npos);
  EXPECT_TRUE(a_pos != std::string::npos);
  EXPECT_TRUE(b_pos != std::string::npos);
  EXPECT_LT(adir_pos, a_pos);
  EXPECT_LT(zdir_pos, a_pos);
  EXPECT_LT(adir_pos, zdir_pos);
  EXPECT_LT(a_pos, b_pos);
}

TEST(ls, ls_group_directories_first_respects_reverse_within_groups) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "zdir");
  std::filesystem::create_directory(tmp.path / "adir");
  tmp.write("b.txt", "b");
  tmp.write("a.txt", "a");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-1", L"--group-directories-first", L"-r"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  size_t zdir_pos = r.stdout_text.find("zdir");
  size_t adir_pos = r.stdout_text.find("adir");
  size_t b_pos = r.stdout_text.find("b.txt");
  size_t a_pos = r.stdout_text.find("a.txt");
  EXPECT_TRUE(zdir_pos != std::string::npos);
  EXPECT_TRUE(adir_pos != std::string::npos);
  EXPECT_TRUE(b_pos != std::string::npos);
  EXPECT_TRUE(a_pos != std::string::npos);
  EXPECT_LT(zdir_pos, adir_pos);
  EXPECT_LT(adir_pos, b_pos);
  EXPECT_LT(b_pos, a_pos);
}

TEST(ls, ls_group_directories_first_is_disabled_by_sort_none) {
  TempDir tmp;
  tmp.write("a.txt", "a");
  std::filesystem::create_directory(tmp.path / "adir");
  tmp.write("b.txt", "b");
  std::filesystem::create_directory(tmp.path / "zdir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-1", L"-U", L"--group-directories-first"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a.txt\nadir\nb.txt\nzdir\n");
}

TEST(ls, ls_zero_uses_nul_terminated_single_column_output) {
  TempDir tmp;
  tmp.write("a.txt", "a");
  tmp.write("b.txt", "b");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"--zero"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, std::string("a.txt\0b.txt\0", 12));
}

TEST(ls, ls_zero_with_one_per_line_matches_nul_terminators) {
  TempDir tmp;
  tmp.write("a.txt", "a");
  tmp.write("b.txt", "b");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-1", L"--zero"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, std::string("a.txt\0b.txt\0", 12));
}

TEST(ls, ls_long_zero_uses_nul_record_terminators) {
  TempDir tmp;
  tmp.write("a.txt", "a");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-l", L"--zero", L"a.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("a.txt\0") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find('\n') == std::string::npos);
}

TEST(ls, ls_long_directory_zero_uses_nul_for_total_and_entries) {
  TempDir tmp;
  tmp.write("a.txt", "a");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-l", L"--zero"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("total ") == 0);
  EXPECT_TRUE(r.stdout_text.find('\0') != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find('\n') == std::string::npos);
}

TEST(ls, ls_size_directory_zero_uses_nul_for_total_and_entries) {
  TempDir tmp;
  tmp.write("a.txt", "a");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-1s", L"--zero"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("total ") == 0);
  EXPECT_TRUE(r.stdout_text.find('\0') != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find('\n') == std::string::npos);
}

TEST(ls, ls_time_style_full_iso_formats_long_output) {
  TempDir tmp;
  tmp.write("a.txt", "a");
  EXPECT_TRUE(
      set_last_write_time(tmp.path / "a.txt", 2025, 1, 2, 13, 4, 5));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-l", L"--time-style=full-iso", L"a.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::regex_search(
      r.stdout_text,
      std::regex(R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{9} [+-]\d{4} a\.txt)")));
}

TEST(ls, ls_time_style_long_iso_formats_long_output) {
  TempDir tmp;
  tmp.write("a.txt", "a");
  EXPECT_TRUE(
      set_last_write_time(tmp.path / "a.txt", 2025, 1, 2, 13, 4, 5));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-l", L"--time-style=long-iso", L"a.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::regex_search(
      r.stdout_text, std::regex(R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2} a\.txt)")));
}

TEST(ls, ls_full_time_implies_long_full_iso_output) {
  TempDir tmp;
  tmp.write("a.txt", "a");
  EXPECT_TRUE(
      set_last_write_time(tmp.path / "a.txt", 2025, 1, 2, 13, 4, 5));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"--full-time", L"a.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::regex_search(
      r.stdout_text,
      std::regex(R"(^-[rwx-]{9}\s+\d+\s+.*\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{9} [+-]\d{4} a\.txt\n?$)")));
}

TEST(ls, ls_long_alias_implies_long_format) {
  TempDir tmp;
  tmp.write("a.txt", "a");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"--long", L"a.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::regex_search(
      r.stdout_text,
      std::regex(R"(^-[rwx-]{9}\s+\d+\s+.*a\.txt\n?$)")));
}

TEST(ls, ls_time_style_last_occurrence_overrides_full_time) {
  TempDir tmp;
  tmp.write("a.txt", "a");
  EXPECT_TRUE(
      set_last_write_time(tmp.path / "a.txt", 2025, 1, 2, 13, 4, 5));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"--full-time", L"--time-style=long-iso", L"a.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::regex_search(
      r.stdout_text, std::regex(R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2} a\.txt)")));
  EXPECT_FALSE(std::regex_search(
      r.stdout_text,
      std::regex(R"(\d{2}:\d{2}:\d{2}\.\d{9} [+-]\d{4} a\.txt)")));
}

TEST(ls, ls_time_style_iso_uses_year_for_old_timestamps) {
  TempDir tmp;
  tmp.write("a.txt", "a");
  EXPECT_TRUE(
      set_last_write_time(tmp.path / "a.txt", 2023, 1, 2, 3, 4, 5));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-l", L"--time-style=iso", L"a.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("2023-01-02  a.txt") != std::string::npos);
}

TEST(ls, ls_invalid_time_style_fails) {
  TempDir tmp;
  tmp.write("a.txt", "a");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-l", L"--time-style=bogus", L"a.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(
      r.stderr_text.find("invalid --time-style argument 'bogus'") !=
      std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("[posix-]full-iso") != std::string::npos);
}

TEST(ls, ls_time_style_locale_uses_default_gnu_shape) {
  TempDir tmp;
  tmp.write("a.txt", "a");
  EXPECT_TRUE(
      set_last_write_time(tmp.path / "a.txt", 2023, 1, 2, 3, 4, 5));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-l", L"--time-style=locale", L"a.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("Jan  2  2023 a.txt") != std::string::npos);
}

TEST(ls, ls_time_style_custom_format_supports_strftime_tokens) {
  TempDir tmp;
  tmp.write("a.txt", "a");
  EXPECT_TRUE(
      set_last_write_time(tmp.path / "a.txt", 2023, 1, 2, 3, 4, 5));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-l", L"--time-style=+%Y/%m/%d-%H:%M", L"a.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(
      r.stdout_text.find("2023/01/02-03:04 a.txt") != std::string::npos);
}

TEST(ls, ls_time_style_custom_format_supports_epoch_seconds) {
  TempDir tmp;
  tmp.write("a.txt", "a");
  EXPECT_TRUE(
      set_last_write_time(tmp.path / "a.txt", 2023, 1, 2, 3, 4, 5));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-l", L"--time-style=+%s", L"a.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("1672599845 a.txt") != std::string::npos);
}

TEST(ls, ls_time_style_custom_format_uses_old_format_before_newline) {
  TempDir tmp;
  tmp.write("a.txt", "a");
  EXPECT_TRUE(
      set_last_write_time(tmp.path / "a.txt", 2023, 1, 2, 3, 4, 5));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe",
        {L"-l", L"--time-style=+OLD-%Y\nRECENT-%H:%M", L"a.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("OLD-2023 a.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("RECENT-03:04") == std::string::npos);
}

TEST(ls, ls_time_style_custom_format_uses_recent_format_after_newline) {
  TempDir tmp;
  tmp.write("a.txt", "a");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe",
        {L"-l", L"--time-style=+OLD-%Y\nRECENT-%H:%M", L"a.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("RECENT-") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("OLD-") == std::string::npos);
}

TEST(ls, ls_indicator_style_and_file_type) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "subdir");
  tmp.write("plain.txt", "plain");
  tmp.write("script.bat", "@echo off");

  Pipeline classify;
  classify.set_cwd(tmp.wpath());
  classify.add(L"ls.exe", {L"-1", L"--indicator-style=classify"});

  auto classify_result = classify.run();

  EXPECT_EQ(classify_result.exit_code, 0);
  EXPECT_TRUE(classify_result.stdout_text.find("subdir/") != std::string::npos);
  EXPECT_TRUE(classify_result.stdout_text.find("script.bat*") !=
              std::string::npos);

  Pipeline file_type;
  file_type.set_cwd(tmp.wpath());
  file_type.add(L"ls.exe", {L"-1", L"--file-type"});

  auto file_type_result = file_type.run();

  EXPECT_EQ(file_type_result.exit_code, 0);
  EXPECT_TRUE(file_type_result.stdout_text.find("subdir/") !=
              std::string::npos);
  EXPECT_TRUE(file_type_result.stdout_text.find("script.bat*") ==
              std::string::npos);
}

TEST(ls, ls_quote_name_uses_c_quoting_and_suffix_outside_quotes) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "dir");
  tmp.write("two words.txt", "content");

  Pipeline quoted_file;
  quoted_file.set_cwd(tmp.wpath());
  quoted_file.add(L"ls.exe", {L"-1", L"-Q", L"two words.txt"});
  auto quoted_file_result = quoted_file.run();

  EXPECT_EQ(quoted_file_result.exit_code, 0);
  EXPECT_EQ_TEXT(quoted_file_result.stdout_text, "\"two words.txt\"\n");

  Pipeline quoted_dir;
  quoted_dir.set_cwd(tmp.wpath());
  quoted_dir.add(L"ls.exe", {L"-d", L"-Q", L"-F", L"dir"});
  auto quoted_dir_result = quoted_dir.run();

  EXPECT_EQ(quoted_dir_result.exit_code, 0);
  EXPECT_EQ_TEXT(quoted_dir_result.stdout_text, "\"dir\"/\n");
}

TEST(ls, ls_quote_name_quotes_long_format_symlink_target_too) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "target dir");

  std::filesystem::path link = tmp.path / "junc";
  bool created = create_directory_junction(link, tmp.path / "target dir");
  EXPECT_TRUE(created);
  if (!created) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-lQ", L"junc"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  const std::string expected_target = (tmp.path / "target dir").string();
  std::string escaped_target = "\"";
  for (char ch : expected_target) {
    if (ch == '\\') {
      escaped_target += "\\\\";
    } else {
      escaped_target.push_back(ch);
    }
  }
  escaped_target += "\"";

  EXPECT_TRUE(r.stdout_text.find("\"junc\" -> " + escaped_target) !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("\"junc\" -> " + expected_target) ==
              std::string::npos);
}

TEST(ls, ls_long_format_colorizes_symlink_target_separately) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "targetdir");

  std::filesystem::path link = tmp.path / "junc";
  bool created = create_directory_junction(link, tmp.path / "targetdir");
  EXPECT_TRUE(created);
  if (!created) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"--color=always", L"-ld", L"junc/"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_TRUE(
      r.stdout_text.find("\x1b[01;36mjunc/\x1b[0m -> \x1b[01;34m") !=
      std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("targetdir\x1b[0m") != std::string::npos);
  EXPECT_TRUE(
      r.stdout_text.find("\x1b[01;36mjunc/ -> C:\\") == std::string::npos);
}

TEST(ls, ls_quoting_style_aliases) {
  TempDir tmp;
  tmp.write("two words.txt", "content");
  tmp.write("quote's.txt", "content");

  Pipeline c_style;
  c_style.set_cwd(tmp.wpath());
  c_style.add(L"ls.exe", {L"-1", L"--quoting-style=c", L"two words.txt"});
  auto c_style_result = c_style.run();

  EXPECT_EQ(c_style_result.exit_code, 0);
  EXPECT_EQ_TEXT(c_style_result.stdout_text, "\"two words.txt\"\n");

  Pipeline escape_style;
  escape_style.set_cwd(tmp.wpath());
  escape_style.add(L"ls.exe",
                   {L"-1", L"--quoting-style=escape", L"two words.txt"});
  auto escape_style_result = escape_style.run();

  EXPECT_EQ(escape_style_result.exit_code, 0);
  EXPECT_EQ_TEXT(escape_style_result.stdout_text, "two words.txt\n");

  Pipeline shell_style;
  shell_style.set_cwd(tmp.wpath());
  shell_style.add(L"ls.exe", {L"-1", L"--quoting-style=shell-escape-always",
                              L"quote's.txt"});
  auto shell_style_result = shell_style.run();

  EXPECT_EQ(shell_style_result.exit_code, 0);
  EXPECT_EQ_TEXT(shell_style_result.stdout_text, "'quote'\\''s.txt'\n");
}

TEST(ls, ls_quoting_style_last_wins_and_invalid_fails) {
  TempDir tmp;
  tmp.write("two words.txt", "content");

  Pipeline last_wins;
  last_wins.set_cwd(tmp.wpath());
  last_wins.add(L"ls.exe", {L"-1", L"--quoting-style=c",
                            L"--quoting-style=literal", L"two words.txt"});
  auto last_wins_result = last_wins.run();

  EXPECT_EQ(last_wins_result.exit_code, 0);
  EXPECT_EQ_TEXT(last_wins_result.stdout_text, "two words.txt\n");

  Pipeline invalid;
  invalid.set_cwd(tmp.wpath());
  invalid.add(L"ls.exe", {L"--quoting-style=bad", L"two words.txt"});
  auto invalid_result = invalid.run();

  EXPECT_NE(invalid_result.exit_code, 0);
}

TEST(ls, ls_ignore_pattern) {
  TempDir tmp;
  tmp.write("keep.txt", "keep");
  tmp.write("skip.tmp", "skip");
  tmp.write("also.txt", "also");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-1", L"-I", L"*.tmp"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("keep.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("also.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("skip.tmp") == std::string::npos);
}

TEST(ls, ls_version_sort) {
  TempDir tmp;
  tmp.write("file1.txt", "1");
  tmp.write("file10.txt", "10");
  tmp.write("file2.txt", "2");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-1", L"-v"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  size_t file1_pos = r.stdout_text.find("file1.txt");
  size_t file2_pos = r.stdout_text.find("file2.txt");
  size_t file10_pos = r.stdout_text.find("file10.txt");
  EXPECT_TRUE(file1_pos != std::string::npos);
  EXPECT_TRUE(file2_pos != std::string::npos);
  EXPECT_TRUE(file10_pos != std::string::npos);
  EXPECT_LT(file1_pos, file2_pos);
  EXPECT_LT(file2_pos, file10_pos);
}

TEST(ls, ls_long_with_file) {
  TempDir tmp;
  tmp.write("testfile.txt", "test content for long format");

  TEST_LOG_FILE_CONTENT("testfile.txt", "test content for long format");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-l", L"testfile.txt"});

  TEST_LOG_CMD_LIST("ls.exe", L"-l", L"testfile.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ls.exe -l testfile.txt output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("testfile.txt") != std::string::npos);
  // Long format should include file permissions, size, date
  EXPECT_TRUE(r.stdout_text.find("-rw") != std::string::npos ||
              r.stdout_text.find("-r-") != std::string::npos);
}

TEST(ls, ls_inode_and_blocks_prefixes) {
  TempDir tmp;
  tmp.write("sample.txt", "sample");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-lis", L"sample.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::regex_search(
      r.stdout_text, std::regex(R"(^\s*\d+\s+\d+\s+[dl-][rwx-]{9}\s+\d+\s+)")));
  EXPECT_TRUE(r.stdout_text.find("sample.txt") != std::string::npos);
}

TEST(ls, ls_human_readable_file_sizes_match_gnu_shape) {
  TempDir tmp;
  tmp.write("tiny.txt", "12345678");
  tmp.write("two_k.bin", std::string(2048, 'x'));

  Pipeline tiny;
  tiny.set_cwd(tmp.wpath());
  tiny.add(L"ls.exe", {L"-lh", L"tiny.txt"});
  auto tiny_result = tiny.run();

  EXPECT_EQ(tiny_result.exit_code, 0);
  EXPECT_TRUE(tiny_result.stdout_text.find("8.0B") == std::string::npos);
  EXPECT_TRUE(std::regex_search(
      tiny_result.stdout_text,
      std::regex(
          R"(\s8\s+[A-Z][a-z][a-z]\s+\d{1,2}\s+\d{2}:\d{2}\s+tiny\.txt)")));

  Pipeline two_k;
  two_k.set_cwd(tmp.wpath());
  two_k.add(L"ls.exe", {L"-lh", L"two_k.bin"});
  auto two_k_result = two_k.run();

  EXPECT_EQ(two_k_result.exit_code, 0);
  EXPECT_TRUE(two_k_result.stdout_text.find("2.0K") != std::string::npos);
}

TEST(ls, ls_si_uses_decimal_units_and_last_occurrence_wins) {
  TempDir tmp;
  tmp.write("thousand.bin", std::string(1000, 'x'));

  Pipeline si;
  si.set_cwd(tmp.wpath());
  si.add(L"ls.exe", {L"-l", L"--si", L"thousand.bin"});
  auto si_result = si.run();

  EXPECT_EQ(si_result.exit_code, 0);
  EXPECT_TRUE(si_result.stdout_text.find("1.0K") != std::string::npos);

  Pipeline last_wins;
  last_wins.set_cwd(tmp.wpath());
  last_wins.add(L"ls.exe", {L"-l", L"--si", L"-h", L"thousand.bin"});
  auto last_wins_result = last_wins.run();

  EXPECT_EQ(last_wins_result.exit_code, 0);
  EXPECT_TRUE(last_wins_result.stdout_text.find("1000") != std::string::npos);
  EXPECT_TRUE(last_wins_result.stdout_text.find("1.0K") == std::string::npos);
}

TEST(ls, ls_block_size_scales_long_size_column) {
  TempDir tmp;
  tmp.write("big.bin", std::string(2049, 'x'));

  Pipeline bytes;
  bytes.set_cwd(tmp.wpath());
  bytes.add(L"ls.exe", {L"-l", L"--block-size=1", L"big.bin"});
  auto bytes_result = bytes.run();

  EXPECT_EQ(bytes_result.exit_code, 0);
  EXPECT_TRUE(bytes_result.stdout_text.find("2049") != std::string::npos);

  Pipeline kib;
  kib.set_cwd(tmp.wpath());
  kib.add(L"ls.exe", {L"-l", L"--block-size=K", L"big.bin"});
  auto kib_result = kib.run();

  EXPECT_EQ(kib_result.exit_code, 0);
  EXPECT_TRUE(std::regex_search(
      kib_result.stdout_text,
      std::regex(
          R"(\s3\s+[A-Z][a-z][a-z]\s+\d{1,2}\s+\d{2}:\d{2}\s+big\.bin)")));
}

TEST(ls, ls_block_size_does_not_rescale_s_or_total_on_windows) {
  TempDir tmp;
  tmp.write("f1000.bin", std::string(1000, 'x'));
  tmp.write("f2049.bin", std::string(2049, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-ls", L"--block-size=1024"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("total 3049\n") == 0);
  EXPECT_TRUE(r.stdout_text.find("1000 -rwxrwxrwx") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("2049 -rwxrwxrwx") != std::string::npos);
  EXPECT_TRUE(std::regex_search(
      r.stdout_text, std::regex(R"(\s1\s+[A-Z][a-z][a-z]\s+\d{1,2}\s+\d{2}:\d{2}\s+f1000\.bin)")));
  EXPECT_TRUE(std::regex_search(
      r.stdout_text, std::regex(R"(\s3\s+[A-Z][a-z][a-z]\s+\d{1,2}\s+\d{2}:\d{2}\s+f2049\.bin)")));
}

TEST(ls, ls_k_option_does_not_rescale_s_or_total_on_windows) {
  TempDir tmp;
  tmp.write("f1000.bin", std::string(1000, 'x'));
  tmp.write("f2049.bin", std::string(2049, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-ls", L"-k"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("total 3049\n") == 0);
  EXPECT_TRUE(r.stdout_text.find("1000 -rwxrwxrwx") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("2049 -rwxrwxrwx") != std::string::npos);
  EXPECT_TRUE(std::regex_search(
      r.stdout_text, std::regex(R"(\s1\s+[A-Z][a-z][a-z]\s+\d{1,2}\s+\d{2}:\d{2}\s+f1000\.bin)")));
  EXPECT_TRUE(std::regex_search(
      r.stdout_text, std::regex(R"(\s3\s+[A-Z][a-z][a-z]\s+\d{1,2}\s+\d{2}:\d{2}\s+f2049\.bin)")));
}

TEST(ls, ls_block_size_humanizes_blocks_and_total) {
  TempDir tmp;
  tmp.write("sample.txt", std::string(2048, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"-lsh"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::regex_search(r.stdout_text, std::regex(R"(^total\s+\S+)")));
  EXPECT_TRUE(r.stdout_text.find("2.0K") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("sample.txt") != std::string::npos);
}

TEST(ls, ls_invalid_block_size_fails) {
  TempDir tmp;
  tmp.write("sample.txt", "sample");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"--block-size=not-a-size", L"sample.txt"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
}
