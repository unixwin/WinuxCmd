// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include <algorithm>
#include <ctime>
#include <iterator>
#include <regex>

#include "framework/winuxtest.h"

TEST(winuxcmd, winuxcmd_help_alias_shows_toplevel_help) {
  Pipeline p;
  p.add(L"winuxcmd.exe", {L"help"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(
      r.stdout_text.find("WinuxCmd - Windows Compatible Linux Command Set") !=
      std::string::npos);
}

TEST(winuxcmd, winuxcmd_dash_h_is_not_help_alias) {
  Pipeline p;
  p.add(L"winuxcmd.exe", {L"-h"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 127);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.find("winuxcmd: command not found: -h") !=
              std::string::npos);
}

TEST(winuxcmd, winuxcmd_help_command_topic_shows_command_help) {
  Pipeline p;
  p.add(L"winuxcmd.exe", {L"help", L"sort"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("Usage: sort [OPTION]... [FILE]...") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("--compress-program") != std::string::npos);
}

TEST(winuxcmd, winuxcmd_help_works_for_positional_sentinel_options) {
  // Commands whose only OptionMeta is a positional sentinel (empty short and
  // long names, non-empty description) used to make print_help throw
  // std::out_of_range on opt.short_name.substr(1) and print nothing (#1065).
  for (const wchar_t* cmd : {L"printf", L"yes", L"tsort", L"sleep"}) {
    Pipeline p;
    p.add(cmd, {L"--help"});
    auto r = p.run();

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_FALSE(r.stdout_text.empty());
    EXPECT_TRUE(r.stdout_text.find("Usage:") != std::string::npos);
  }
}

TEST(winuxcmd, winuxcmd_trailing_separator_on_file_fails_safely) {
  // #1052: a trailing separator requires a directory target. On a regular
  // file it must fail without opening/truncating the file (data loss).
  TempDir tmp;
  tmp.write("reg.txt", "CONTENT\n");
  tmp.write("src.txt", "SRC\n");

  {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"cat.exe", {L"reg.txt/"});
    auto r = p.run();
    EXPECT_NE(r.exit_code, 0);
    EXPECT_TRUE(r.stderr_text.find("Not a directory") != std::string::npos);
  }
  {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.set_stdin("x\n");
    p.add(L"tee.exe", {L"reg.txt/"});
    auto r = p.run();
    EXPECT_NE(r.exit_code, 0);
    EXPECT_FALSE(r.stderr_text.empty());
  }
  {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"cp.exe", {L"src.txt", L"reg.txt/"});
    auto r = p.run();
    EXPECT_NE(r.exit_code, 0);
    EXPECT_FALSE(r.stderr_text.empty());
  }
  {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"rm.exe", {L"reg.txt/"});
    auto r = p.run();
    EXPECT_NE(r.exit_code, 0);
  }

  EXPECT_EQ(tmp.read("reg.txt"), "CONTENT\n");
}

TEST(winuxcmd, winuxcmd_trailing_separator_on_directory_still_works) {
  // #1052 must not break valid directory operands: cp into "dir/" copies
  // inside it, and "mkdir newdir/" still creates the directory.
  TempDir tmp;
  tmp.write("src.txt", "SRC\n");
  std::filesystem::create_directory(tmp.path / "realdir");

  {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"cp.exe", {L"src.txt", L"realdir/"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(tmp.read("realdir/src.txt"), "SRC\n");
  }
  {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"mkdir.exe", {L"newdir/"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_TRUE(std::filesystem::is_directory(tmp.path / "newdir"));
  }
}

TEST(winuxcmd, winuxcmd_unknown_command_does_not_fallback_to_shell) {
  Pipeline p;
  p.add(L"winuxcmd.exe", {L"definitely-not-a-winuxcmd-command"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 127);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(
      r.stderr_text.find(
          "winuxcmd: command not found: definitely-not-a-winuxcmd-command") !=
      std::string::npos);
}

// #1020: %z/%Z report the status-change time (NTFS ChangeTime), not the
// last-access time.
TEST(winuxcmd, stat_z_directive_reports_change_time) {
  TempDir tmp;
  tmp.write("f.txt", "x\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"stat.exe", {L"-c", L"%z|%Z", L"f.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  const std::string out = r.stdout_text;
  const auto sep = out.find('|');
  ASSERT_NE(sep, std::string::npos);
  // %z is a human-readable timestamp with nanoseconds and a numeric zone.
  EXPECT_TRUE(std::regex_match(
      out.substr(0, sep),
      std::regex("\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}\\.\\d{9} "
                 "[+-]\\d{4}")));
  // %Z is the same instant as epoch seconds.
  EXPECT_TRUE(std::regex_match(out.substr(sep + 1), std::regex("-?\\d+\n?")));
}

// #1021: previously-unimplemented format directives must not echo
// literally; GNU meanings and fallbacks apply.
TEST(winuxcmd, stat_format_directives_implemented) {
  TempDir tmp;
  tmp.write("f.txt", "x\n");

  // %f: raw mode in hex — regular writable file is 0100644 = 0x81a4.
  {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"stat.exe", {L"-c", L"%f", L"f.txt"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_text, "81a4\n");
  }
  // %c: SELinux context string is '?' on Windows.
  {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"stat.exe", {L"-c", L"%c", L"f.txt"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_text, "?\n");
  }
  // %r/%t/%T: device type fields are 0 for regular files.
  {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"stat.exe", {L"-c", L"%r|%t|%T", L"f.txt"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_text, "0|0|0\n");
  }
  // Unknown directives print '?' like GNU.
  {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"stat.exe", {L"-c", L"%Q", L"f.txt"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_text, "?\n");
  }
  // %C: prints '?' but reports a failure like GNU on a non-SELinux system.
  {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"stat.exe", {L"-c", L"%C", L"f.txt"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 1);
    EXPECT_EQ(r.stdout_text, "?\n");
    EXPECT_TRUE(r.stderr_text.find("failed to get security context") !=
                std::string::npos);
  }
}

// #1022: human-readable timestamps carry nanosecond precision and a
// numeric timezone offset ("YYYY-MM-DD HH:MM:SS.NNNNNNNNN +ZZZZ").
TEST(winuxcmd, stat_timestamps_have_nanoseconds_and_zone) {
  TempDir tmp;
  tmp.write("f.txt", "x\n");

  for (const wchar_t* directive : {L"%x", L"%y", L"%z", L"%w"}) {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"stat.exe", {L"-c", directive, L"f.txt"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_TRUE(std::regex_match(
        r.stdout_text,
        std::regex("\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}\\.\\d{9} "
                   "[+-]\\d{4}\n?")));
  }
}

// #1023: default listing and -t/--terse match the GNU field layout.
TEST(winuxcmd, stat_default_and_terse_layouts) {
  TempDir tmp;
  tmp.write("f.txt", "x\n");

  {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"stat.exe", {L"f.txt"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    for (const char* field :
         {"  File: ", "  Size: ", "Blocks: ", " IO Block: ", "Device: ",
          "Inode: ", " Links: ", "Access: (0", "Uid: (", "Gid: (",
          "Access: ", "Modify: ", "Change: ", " Birth: ", "regular file"}) {
      EXPECT_TRUE(r.stdout_text.find(field) != std::string::npos);
    }
  }
  {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"stat.exe", {L"-t", L"f.txt"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    // GNU terse: %n %s %b %f %u %g %D %i %h %t %T %X %Y %Z %W %o
    std::istringstream iss(r.stdout_text);
    std::vector<std::string> fields{std::istream_iterator<std::string>(iss),
                                    std::istream_iterator<std::string>()};
    EXPECT_EQ(fields.size(), 16u);
    ASSERT_GE(fields.size(), 4u);
    EXPECT_EQ(fields[0], "f.txt");
    EXPECT_EQ(fields[3], "81a4");  // %f raw mode hex
    EXPECT_EQ(fields[9], "0");     // %t
    EXPECT_EQ(fields[10], "0");    // %T
  }
}

// #1069: --cached=WHEN accepts never/always/default as a caching-hint
// no-op; other values are a GNU-style usage error.
TEST(winuxcmd, stat_cached_when_is_accepted_noop) {
  TempDir tmp;
  tmp.write("f.txt", "x\n");

  for (const wchar_t* when : {L"never", L"always", L"default"}) {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"stat.exe",
          {std::wstring(L"--cached=") + when, L"-c", L"%s", L"f.txt"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_text, "2\n");
  }
  {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"stat.exe", {L"--cached=bogus", L"f.txt"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 1);
    EXPECT_TRUE(r.stderr_text.find("invalid argument 'bogus' for '--cached'") !=
                std::string::npos);
    EXPECT_TRUE(r.stderr_text.find("- 'always'") != std::string::npos);
  }
}

// #1024: the Windows well-known "None" group is unresolvable for GNU
// purposes — never printed as a literal name.
TEST(winuxcmd, stat_and_id_do_not_print_none_group) {
  TempDir tmp;
  tmp.write("f.txt", "x\n");

  {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"stat.exe", {L"-c", L"%G", L"f.txt"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.stdout_text, "None\n");
  }
  {
    Pipeline p;
    p.add(L"id.exe", {});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_TRUE(r.stdout_text.find("(None)") == std::string::npos);
  }
}

TEST(winuxcmd, pr_header_uses_file_modification_time) {
  // #1046: GNU pr uses the file's last-modified time in the page header, not
  // the current time (current time is only for standard input).
  TempDir tmp;
  tmp.write("two.txt", "alpha\nbeta\n");
  const auto old_mtime = std::filesystem::file_time_type::clock::now() -
                         std::chrono::hours(24 * 365 * 5);
  std::filesystem::last_write_time(tmp.path / "two.txt", old_mtime);

  // Expected local-time prefix of the header timestamp (%Y-%m).
  const std::time_t t =
      std::time(nullptr) - static_cast<std::time_t>(24 * 365 * 5) * 3600;
  std::tm tmv{};
  localtime_s(&tmv, &t);
  char expected_prefix[8];
  snprintf(expected_prefix, sizeof(expected_prefix), "%04d-%02d",
           tmv.tm_year + 1900, tmv.tm_mon + 1);
  char today_prefix[8];
  std::tm now_tm{};
  const std::time_t now_t = std::time(nullptr);
  localtime_s(&now_tm, &now_t);
  snprintf(today_prefix, sizeof(today_prefix), "%04d-%02d-%02d",
           now_tm.tm_year + 1900, now_tm.tm_mon + 1, now_tm.tm_mday);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pr.exe", {L"two.txt"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(expected_prefix) != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find(today_prefix) == std::string::npos);
}

TEST(winuxcmd, pr_number_lines_uses_gnu_field_format) {
  // #1047: -n right-justifies the line number in a DIGITS-wide field
  // (default 5) followed by the SEP character (default TAB).
  TempDir tmp;
  tmp.write("two.txt", "alpha\nbeta\n");
  {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"pr.exe", {L"-n", L"two.txt"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_TRUE(r.stdout_text.find("    1\talpha\n") != std::string::npos);
    EXPECT_TRUE(r.stdout_text.find("    2\tbeta\n") != std::string::npos);
  }
  {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"pr.exe", {L"-n,3", L"two.txt"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_TRUE(r.stdout_text.find("  1,alpha\n") != std::string::npos);
    EXPECT_TRUE(r.stdout_text.find("  2,beta\n") != std::string::npos);
  }
}

TEST(winuxcmd, pr_short_page_length_disables_pagination) {
  // #1048: when -l leaves no room for a body (page length <= 10) GNU drops
  // the header block, footer margin and inter-page padding; +PAGE still
  // counts pages of page_length lines.
  TempDir tmp;
  tmp.write("two.txt", "alpha\nbeta\n");
  tmp.write("f15.txt", "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n");
  {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"pr.exe", {L"-l", L"10", L"two.txt"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_text, "alpha\nbeta\n");
  }
  {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"pr.exe", {L"-l", L"10", L"f15.txt"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_text,
              "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n");
  }
  {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"pr.exe", {L"+2", L"-l", L"10", L"f15.txt"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_text, "11\n12\n13\n14\n15\n");
  }
  {
    // -l 12 still paginates: 5-line header + 2-line body + 5-line trailer.
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"pr.exe", {L"-l", L"12", L"two.txt"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_TRUE(r.stdout_text.find("Page 1") != std::string::npos);
    EXPECT_TRUE(r.stdout_text.find("alpha\nbeta\n") != std::string::npos);
    EXPECT_EQ(std::count(r.stdout_text.begin(), r.stdout_text.end(), '\n'), 12);
  }
}

TEST(winuxcmd, fmt_goal_width_steers_line_breaking) {
  // #1049: -g sets the goal width; lines aim at the goal rather than
  // greedily filling to -w.
  TempDir tmp;
  tmp.write("para.txt",
            "The quick brown fox jumps over the lazy dog and keeps running "
            "far beyond the hills.\n");
  {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"fmt.exe", {L"-w", L"20", L"-g", L"10", L"para.txt"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_text,
              "The quick\nbrown fox jumps\nover the lazy\ndog and keeps\n"
              "running far\nbeyond the hills.\n");
  }
  {
    // A goal wider than the maximum width is rejected like GNU.
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"fmt.exe", {L"-g", L"100", L"para.txt"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 1);
    EXPECT_TRUE(r.stderr_text.find("invalid width") != std::string::npos);
  }
}

TEST(winuxcmd, fmt_obsolete_width_form_first_argument_only) {
  // #1080: GNU accepts the obsolete "-WIDTH" form as "-w WIDTH" only when it
  // is the first argument; elsewhere it is an error.
  TempDir tmp;
  tmp.write("words.txt",
            "aa bb cc dd ee ff gg hh ii jj kk ll mm nn oo pp qq rr ss tt uu "
            "vv ww xx yy zz\n");
  CommandResult via_obsolete;
  {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"fmt.exe", {L"-60", L"words.txt"});
    via_obsolete = p.run();
    EXPECT_EQ(via_obsolete.exit_code, 0);
  }
  {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"fmt.exe", {L"-w", L"60", L"words.txt"});
    auto via_w = p.run();
    EXPECT_EQ(via_obsolete.stdout_text, via_w.stdout_text);
  }
  {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"fmt.exe", {L"-s", L"-60", L"words.txt"});
    auto r = p.run();
    EXPECT_NE(r.exit_code, 0);
    EXPECT_TRUE(r.stdout_text.empty());
  }
}

namespace {

// Bounded helper process so --pid=PID gives follow-mode tests a natural end.
struct FollowSentinel {
  PROCESS_INFORMATION info{};
  bool valid = false;

  FollowSentinel() = default;
  FollowSentinel(const FollowSentinel&) = delete;
  auto operator=(const FollowSentinel&) -> FollowSentinel& = delete;

  FollowSentinel(FollowSentinel&& other) noexcept
      : info(other.info), valid(other.valid) {
    other.info = {};
    other.valid = false;
  }

  ~FollowSentinel() {
    if (!valid) return;
    if (WaitForSingleObject(info.hProcess, 5000) == WAIT_TIMEOUT) {
      TerminateProcess(info.hProcess, 1);
      WaitForSingleObject(info.hProcess, 1000);
    }
    CloseHandle(info.hProcess);
    CloseHandle(info.hThread);
  }

  [[nodiscard]] auto pid() const -> DWORD { return info.dwProcessId; }
};

auto start_follow_sentinel(int ping_count = 4) -> FollowSentinel {
  FollowSentinel process;
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  std::wstring command = L"cmd.exe /d /c ping -n ";
  command += std::to_wstring(ping_count);
  command += L" 127.0.0.1 > nul";

  if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                     CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                     &process.info)) {
    process.valid = true;
  }
  return process;
}

}  // namespace

// #237: --suppress-matched drops the line at the split boundary (the first
// line of the next output file), not the regexp-matched line itself when an
// offset is given.
TEST(winuxcmd, csplit_suppress_matched_regex_positive_offset) {
  TempDir tmp;
  tmp.write("in.txt", "1\n2\n3\n4\n5\n6\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"csplit.exe", {L"--suppress-matched", L"in.txt", L"/3/+1"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "6\n4\n");
  EXPECT_EQ_TEXT(tmp.read("xx00"), "1\n2\n3\n");
  EXPECT_EQ_TEXT(tmp.read("xx01"), "5\n6\n");
}

TEST(winuxcmd, csplit_suppress_matched_regex_negative_offset) {
  TempDir tmp;
  tmp.write("in.txt", "1\n2\n3\n4\n5\n6\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"csplit.exe", {L"--suppress-matched", L"in.txt", L"/4/-1"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "4\n6\n");
  EXPECT_EQ_TEXT(tmp.read("xx00"), "1\n2\n");
  EXPECT_EQ_TEXT(tmp.read("xx01"), "4\n5\n6\n");
}

// #237: an exhausted {*} repeat flushes the rest of the input into the
// in-progress file and must not create an extra empty split.
TEST(winuxcmd, csplit_suppress_matched_star_repeat_no_extra_empty_file) {
  TempDir tmp;
  tmp.write("in.txt", "1\n2\n3\n4\n5\n6\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"csplit.exe", {L"--suppress-matched", L"in.txt", L"/3/", L"{*}"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "4\n6\n");
  EXPECT_EQ_TEXT(tmp.read("xx00"), "1\n2\n");
  EXPECT_EQ_TEXT(tmp.read("xx01"), "4\n5\n6\n");
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "xx02"));
}

// #966: -v/--verbose always prints the ==> name <== header, even for a
// single operand.
TEST(winuxcmd, tail_verbose_single_file_prints_header) {
  TempDir tmp;
  tmp.write("one.txt", "l1\nl2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tail.exe", {L"-v", L"one.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "==> one.txt <==\nl1\nl2\n");
}

TEST(winuxcmd, tail_verbose_multiple_files_print_headers) {
  TempDir tmp;
  tmp.write("a.txt", "A\n");
  tmp.write("b.txt", "B\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tail.exe", {L"-v", L"a.txt", L"b.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "==> a.txt <==\nA\n\n==> b.txt <==\nB\n");
}

// #287: --follow=name with --retry reopens the path after it is replaced.
TEST(winuxcmd, tail_follow_name_retry_reopens_replaced_path) {
  TempDir tmp;
  tmp.write("live.txt", "old0\n");

  auto sentinel = start_follow_sentinel();
  EXPECT_TRUE(sentinel.valid);
  if (!sentinel.valid) return;

  std::thread writer([&tmp]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    std::error_code ec;
    for (int attempt = 0; attempt < 20; ++attempt) {
      std::filesystem::rename(tmp.path / "live.txt", tmp.path / "gone.txt", ec);
      if (!ec) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    tmp.write("live.txt", "new0\n");
  });

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tail.exe",
        {L"--follow=name", L"--retry", L"--sleep-interval", L"0.05", L"--pid",
         std::to_wstring(sentinel.pid()), L"live.txt"});
  auto r = p.run();
  writer.join();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("old0\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("new0\n") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("following new file") != std::string::npos);
}

// #287: without --retry, --follow=name gives up when the name disappears.
TEST(winuxcmd, tail_follow_name_without_retry_exits_when_file_vanishes) {
  TempDir tmp;
  tmp.write("del.txt", "d1\n");

  auto sentinel = start_follow_sentinel(6);
  EXPECT_TRUE(sentinel.valid);
  if (!sentinel.valid) return;

  std::thread writer([&tmp]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::error_code ec;
    std::filesystem::remove(tmp.path / "del.txt", ec);
  });

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tail.exe", {L"--follow=name", L"--sleep-interval", L"0.05", L"--pid",
                      std::to_wstring(sentinel.pid()), L"del.txt"});
  auto r = p.run();
  writer.join();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("no files remaining") != std::string::npos);
}

// #1025: an unreadable operand is diagnosed but later operands are still
// processed; the exit status is nonzero.
TEST(winuxcmd, tac_missing_operand_continues_to_next_file) {
  TempDir tmp;
  tmp.write("good.txt", "g1\ng2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tac.exe", {L"missing.txt", L"good.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "g2\ng1\n");
  EXPECT_TRUE(r.stderr_text.find("missing.txt") != std::string::npos);
}

TEST(winuxcmd, tac_error_between_operands_still_processes_rest) {
  TempDir tmp;
  tmp.write("a.txt", "a1\n");
  tmp.write("b.txt", "b1\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tac.exe", {L"a.txt", L"missing.txt", L"b.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "a1\nb1\n");
}
