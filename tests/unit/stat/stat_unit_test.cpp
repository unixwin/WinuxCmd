// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(stat, stat_file) {
  TempDir tmp;
  tmp.write("test.txt", "hello world\n");

  TEST_LOG_FILE_CONTENT("test.txt", "hello world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"stat.exe", {L"test.txt"});

  TEST_LOG_CMD_LIST("stat.exe", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("stat output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(stat, stat_directory) {
  TempDir tmp;
  tmp.write("test.txt", "test");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"stat.exe", {L"test.txt"});

  TEST_LOG_CMD_LIST("stat.exe", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("stat directory output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(stat, stat_terse) {
  TempDir tmp;
  tmp.write("test.txt", "test");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"stat.exe", {L"-t", L"test.txt"});

  TEST_LOG_CMD_LIST("stat.exe", L"-t", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("stat terse output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(stat, stat_wildcard) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");
  tmp.write("file2.txt", "content2");
  tmp.write("other.log", "log");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"stat.exe", {L"*.txt"});

  TEST_LOG_CMD_LIST("stat.exe", L"*.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("stat wildcard output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("file1.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file2.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("other.log") == std::string::npos);
}

TEST(stat, stat_printf_format) {
  TempDir tmp;
  tmp.write("test.txt", "data");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"stat.exe", {L"--printf", L"%n:%s\\n", L"test.txt"});

  TEST_LOG_CMD_LIST("stat.exe", L"--printf", L"%n:%s\\n", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("stat printf output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "test.txt:4\n");
}

TEST(stat, stat_format_appends_newline) {
  TempDir tmp;
  tmp.write("test.txt", "data");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"stat.exe", {L"-c", L"%n:%s", L"test.txt"});

  TEST_LOG_CMD_LIST("stat.exe", L"-c", L"%n:%s", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("stat format output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "test.txt:4\n");
}

TEST(stat, stat_format_common_fields) {
  TempDir tmp;
  tmp.write("test.txt", "data");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"stat.exe", {L"-c", L"%F|%a|%A|%h|%Y", L"test.txt"});

  TEST_LOG_CMD_LIST("stat.exe", L"-c", L"%F|%a|%A|%h|%Y", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("stat common fields output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.starts_with("regular file|644|-rw-r--r--|"));
  EXPECT_TRUE(r.stdout_text.ends_with("\n"));

  auto last_separator = r.stdout_text.find_last_of('|');
  EXPECT_TRUE(last_separator != std::string::npos);
  if (last_separator != std::string::npos) {
    auto timestamp = r.stdout_text.substr(last_separator + 1);
    if (!timestamp.empty() && timestamp.back() == '\n') {
      timestamp.pop_back();
    }
    EXPECT_FALSE(timestamp.empty());
    EXPECT_TRUE(std::ranges::all_of(timestamp, [](char ch) {
      return std::isdigit(static_cast<unsigned char>(ch)) != 0;
    }));
  }
}

TEST(stat, stat_file_system_format) {
  TempDir tmp;
  tmp.write("test.txt", "data");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"stat.exe", {L"-f", L"-c", L"%n|%s|%S|%T", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.starts_with("test.txt|"));
  EXPECT_TRUE(r.stdout_text.ends_with("\n"));

  auto first_separator = r.stdout_text.find('|');
  auto second_separator = r.stdout_text.find('|', first_separator + 1);
  auto third_separator = r.stdout_text.find('|', second_separator + 1);
  EXPECT_TRUE(first_separator != std::string::npos);
  EXPECT_TRUE(second_separator != std::string::npos);
  EXPECT_TRUE(third_separator != std::string::npos);
  if (first_separator != std::string::npos &&
      second_separator != std::string::npos &&
      third_separator != std::string::npos) {
    auto block_size = r.stdout_text.substr(
        first_separator + 1, second_separator - first_separator - 1);
    auto fundamental_size = r.stdout_text.substr(
        second_separator + 1, third_separator - second_separator - 1);
    EXPECT_FALSE(block_size.empty());
    EXPECT_EQ_TEXT(block_size, fundamental_size);
  }
}

TEST(stat, stat_printf_interprets_common_backslash_escapes) {
  TempDir tmp;
  tmp.write("test.txt", "data");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"stat.exe", {L"--printf", L"%n\\t\\011\\010", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_BYTES(r.stdout_text, "test.txt\t\t\b");
}

TEST(stat, stat_format_last_occurrence_wins_over_printf) {
  TempDir tmp;
  tmp.write("test.txt", "data");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"stat.exe", {L"--printf", L"%n", L"-c", L"%s", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "4\n");
}

TEST(stat, stat_printf_last_occurrence_wins_over_format) {
  TempDir tmp;
  tmp.write("test.txt", "data");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"stat.exe", {L"-c", L"%s", L"--printf", L"%n", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "test.txt");
}

// [GNU] An explicit -c/--printf format beats -t regardless of option
// order: `stat -c %n -t f` prints just the format result (probed on GNU
// for #1334); -t only supplies the default when no format was given.
TEST(stat, stat_format_beats_terse_regardless_of_order) {
  TempDir tmp;
  tmp.write("test.txt", "data");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"stat.exe", {L"-c", L"%n", L"-t", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "test.txt\n");
}

TEST(stat, stat_format_last_occurrence_wins_over_terse) {
  TempDir tmp;
  tmp.write("test.txt", "data");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"stat.exe", {L"-t", L"-c", L"%n", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "test.txt\n");
}

TEST(stat, stat_missing_operand_reports_help_hint) {
  Pipeline p;
  p.add(L"stat.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "stat: missing operand\nTry 'stat --help' for more information.\n");
}

namespace {
bool stat_test_create_symlink(const std::filesystem::path& link,
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

TEST(stat, stat_dangling_symlink_lstats_link_successfully) {
  TempDir tmp;

  std::filesystem::path link = tmp.path / "dang";
  if (!stat_test_create_symlink(link, std::filesystem::path(L"z_absent_tgt"))) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"stat.exe", {L"dang"});
  auto r = p.run();

  // [GNU] stat (no -L) lstats: a dangling link reports the link's own
  // metadata, "File: dang -> z_absent_tgt" and "symbolic link" type (#1060).
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("dang -> z_absent_tgt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("symbolic link") != std::string::npos);
}

TEST(stat, stat_dereference_dangling_symlink_fails) {
  TempDir tmp;

  std::filesystem::path link = tmp.path / "dang";
  if (!stat_test_create_symlink(link, std::filesystem::path(L"z_absent_tgt"))) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"stat.exe", {L"-L", L"dang"});
  auto r = p.run();

  // [GNU] stat -L follows the link; a dangling target fails with rc=1.
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("cannot stat") != std::string::npos);
}

namespace {

// #1231: the MSYS single-letter drive spelling of a native path
// ("D:\repo\x" -> "/d/repo/x"): lowercase drive letter, forward separators.
auto drive_dialect_spelling(const std::filesystem::path& native)
    -> std::wstring {
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

void clear_shell_root_env(Pipeline& pipeline) {
  for (const auto* name :
       {L"__RUBASH_SHELL_ROOT", L"NIU_ROOT", L"WINUXSH_ROOT", L"RUBASH_ROOT"}) {
    pipeline.set_env(name, L"");
  }
}

bool create_symlink_or_skip(const std::filesystem::path& link,
                            const std::filesystem::path& target) {
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

}  // namespace

TEST(stat, stat_drive_dialect_operand_resolves_from_foreign_cwd) {
  TempDir dir;
  TempDir cwd;
  dir.write("f.txt", "data");

  Pipeline p;
  p.set_cwd(cwd.wpath());
  clear_shell_root_env(p);
  p.add(L"stat.exe", {drive_dialect_spelling(dir.path) + L"/f.txt"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("stat drive dialect output", r.stdout_text);

  // Before #1231 the raw dialect text was probed with GetFullPathNameW,
  // which stranded it on the cwd's drive and reported a bogus ENOENT.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("f.txt") != std::string::npos);
}

TEST(stat, stat_dereference_drive_dialect_operand_resolves) {
  TempDir dir;
  TempDir cwd;
  dir.write("f.txt", "data");

  Pipeline p;
  p.set_cwd(cwd.wpath());
  clear_shell_root_env(p);
  p.add(L"stat.exe", {L"-L", drive_dialect_spelling(dir.path) + L"/f.txt"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("stat -L drive dialect output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("f.txt") != std::string::npos);
}

TEST(stat, stat_dereference_follows_drive_dialect_symlink) {
  TempDir dir;
  TempDir cwd;
  dir.write("f.txt", "data");

  std::filesystem::path link = dir.path / "link";
  if (!create_symlink_or_skip(link, std::filesystem::path(L"f.txt"))) {
    return;
  }

  Pipeline p;
  p.set_cwd(cwd.wpath());
  clear_shell_root_env(p);
  p.add(L"stat.exe", {L"-L", drive_dialect_spelling(dir.path) + L"/link"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("stat -L drive dialect symlink output", r.stdout_text);

  // -L resolves the link through the canonicalize layer: the dialect
  // operand must fold before the component walk, and the link's target
  // must be reached.  [GNU] stat echoes the operand as typed in the
  // File: line ("link", never the target name) and reports the target's
  // metadata: the regular file's size ("data" = 4 bytes), not the reparse
  // point's, and no lstat "-> target" display.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("link") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("Size: 4") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("regular file") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("->") == std::string::npos);
}

TEST(stat, stat_lstat_drive_dialect_symlink_reports_link) {
  TempDir dir;
  TempDir cwd;
  dir.write("f.txt", "data");

  std::filesystem::path link = dir.path / "link";
  if (!create_symlink_or_skip(link, std::filesystem::path(L"f.txt"))) {
    return;
  }

  Pipeline p;
  p.set_cwd(cwd.wpath());
  clear_shell_root_env(p);
  p.add(L"stat.exe", {drive_dialect_spelling(dir.path) + L"/link"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("stat (lstat) drive dialect symlink output", r.stdout_text);

  // Default stat() is lstat(): the symlink operand reports the link itself
  // and displays its target.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("link") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("f.txt") != std::string::npos);
}

// ===== #1245 CR NIT-2 regression pin + #1246 argv/resolver agreement =====
//
// With a shell root configured, stat routes its operand through the shared
// argv boundary, so a shell-namespace spelling ("/usr/bin") resolves through
// the shell tree exactly like ls and readlink -f. Before #1245 the raw
// "/usr/bin" text was probed directly and failed with ENOENT even though the
// sibling commands resolved the same operand.

TEST(stat, stat_shell_root_usr_bin_resolves_through_shell_tree) {
  TempDir root;
  TempDir cwd;
  root.write("usr/bin/tool.txt", "data");

  Pipeline p;
  p.set_cwd(cwd.wpath());
  p.set_env(L"__RUBASH_SHELL_ROOT", root.wpath());
  p.add(L"stat.exe", {L"/usr/bin"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("stat /usr/bin with shell root output", r.stdout_text);

  // [GNU] the File: line echoes the operand as typed; the operand resolves
  // through the shell tree, so the directory is stat'ed, not ENOENT.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("/usr/bin") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("directory") != std::string::npos);
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

TEST(stat, stat_cygdrive_and_mnt_prefix_operands_resolve) {
  TempDir dir;
  TempDir cwd;
  dir.write("f.txt", "data");

  const std::wstring cygdrive =
      L"/cygdrive" + drive_dialect_spelling(dir.path) + L"/f.txt";
  const std::wstring mnt =
      L"/mnt" + drive_dialect_spelling(dir.path) + L"/f.txt";

  Pipeline cyg;
  cyg.set_cwd(cwd.wpath());
  clear_shell_root_env(cyg);
  cyg.add(L"stat.exe", {cygdrive});
  auto rc = cyg.run();

  TEST_LOG_EXIT_CODE(rc);
  TEST_LOG("stat cygdrive prefix output", rc.stdout_text);

  // stat folded this spelling at the argv boundary since #1145; the pin
  // keeps the #1246 resolver fold agreeing with it (realpath/readlink -f
  // must answer the same spelling the same way).
  EXPECT_EQ(rc.exit_code, 0);
  EXPECT_TRUE(rc.stdout_text.find("f.txt") != std::string::npos);
  EXPECT_EQ_TEXT(rc.stderr_text, "");

  Pipeline m;
  m.set_cwd(cwd.wpath());
  clear_shell_root_env(m);
  m.add(L"stat.exe", {mnt});
  auto rm = m.run();

  TEST_LOG_EXIT_CODE(rm);
  TEST_LOG("stat mnt prefix output", rm.stdout_text);

  EXPECT_EQ(rm.exit_code, 0);
  EXPECT_TRUE(rm.stdout_text.find("f.txt") != std::string::npos);
  EXPECT_EQ_TEXT(rm.stderr_text, "");
}

TEST(stat, issue_1283_drive_colon_operand_stats_carrier_file) {
  TempDir tmp;
  const wchar_t drive = tmp.wpath()[0];
  const std::wstring carrier =
      std::wstring(1, drive) + L'\uF03A' + L"wux.txt";
  temp_dir_detail::write_all(tmp.path / carrier, "hello\n", 6);
  const std::wstring operand = std::wstring(1, drive) + L":wux.txt";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"stat.exe", {operand});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("wux.txt"), std::string::npos);
}
