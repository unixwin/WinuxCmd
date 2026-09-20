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

TEST(stat, stat_terse_last_occurrence_wins_over_format) {
  TempDir tmp;
  tmp.write("test.txt", "data");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"stat.exe", {L"-c", L"%n", L"-t", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text, "test.txt\n");
  EXPECT_TRUE(r.stdout_text.starts_with("test.txt "));
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
