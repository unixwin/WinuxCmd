// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include <fstream>

#include "framework/winuxtest.h"

namespace {

auto write_db(TempDir& tmp, const std::string& name, const std::string& body) {
  std::ofstream out(tmp.path / name, std::ios::binary);
  out << body;
}

}  // namespace

TEST(locate, locate_substring_match_prints_and_exits_zero) {
  TempDir tmp;
  write_db(tmp, "db.txt",
           "C:/work/main.cpp\nC:/work/readme.md\nC:/work/src/util.cpp\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"locate.exe", {L"-d", L"db.txt", L"cpp"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("C:/work/main.cpp") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("C:/work/src/util.cpp") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("readme.md") == std::string::npos);
}

// [GNU] locate.c:1361: locate exits with status 1 when nothing matched.
TEST(locate, locate_no_match_exits_one) {
  TempDir tmp;
  write_db(tmp, "db.txt", "C:/work/main.cpp\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"locate.exe", {L"-d", L"db.txt", L"nosuchthing"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(locate, locate_missing_operand_reports_error) {
  TempDir tmp;
  write_db(tmp, "db.txt", "C:/work/main.cpp\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"locate.exe", {L"-d", L"db.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("missing operand") != std::string::npos);
}

// [GNU] locate.c: pattern containing any of *?[]\ uses fnmatch (flags 0, no
// FNM_PATHNAME, so '*' also matches '/').
TEST(locate, locate_glob_pattern_matches_across_slashes) {
  TempDir tmp;
  write_db(tmp, "db.txt", "C:/work/src/util.cpp\nC:/work/readme.md\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"locate.exe", {L"-d", L"db.txt", L"*.cpp"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("C:/work/src/util.cpp") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("readme.md") == std::string::npos);
}

// Literal glob-free patterns stay substring searches even when they contain
// characters like dots.
TEST(locate, locate_plain_pattern_stays_substring) {
  TempDir tmp;
  write_db(tmp, "db.txt", "C:/work/a.cpp\nC:/work/b.cpp\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"locate.exe", {L"-d", L"db.txt", L"/work/a"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("C:/work/a.cpp") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b.cpp") == std::string::npos);
}

TEST(locate, locate_ignore_case) {
  TempDir tmp;
  write_db(tmp, "db.txt", "C:/Work/README.md\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"locate.exe", {L"-d", L"db.txt", L"-i", L"readme"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("README.md") != std::string::npos);
}

// [GNU] locate.c:1405: "-l, --limit=N" limits results.  GNU has no -n.
TEST(locate, locate_limit_caps_results) {
  TempDir tmp;
  write_db(tmp, "db.txt", "C:/w/a.cpp\nC:/w/b.cpp\nC:/w/c.cpp\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"locate.exe", {L"-d", L"db.txt", L"-l", L"2", L"cpp"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text.find("a.cpp") != std::string::npos, true);
  EXPECT_TRUE(r.stdout_text.find("b.cpp") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("c.cpp") == std::string::npos);
}

// [GNU] -r/--regex treats the pattern as a POSIX basic regular expression
// searched anywhere in the path.
TEST(locate, locate_regex_match) {
  TempDir tmp;
  write_db(tmp, "db.txt", "C:/work/doc1.md\nC:/work/doc12.md\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"locate.exe", {L"-d", L"db.txt", L"-r", L"doc1\\.md"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("doc1.md") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("doc12.md") == std::string::npos);
}

TEST(locate, locate_basename_scope) {
  TempDir tmp;
  write_db(tmp, "db.txt", "C:/src/build/main.cpp\nC:/src/main.cpp\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  // A basename never contains '/', so "-b build/main" cannot match even
  // though the substring occurs in the whole path.
  p.add(L"locate.exe", {L"-d", L"db.txt", L"-b", L"build/main"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
}

// [GNU] -w/--wholename matches against the whole path and is the default;
// a later -w cancels an earlier -b.
TEST(locate, locate_wholename_overrides_basename) {
  TempDir tmp;
  write_db(tmp, "db.txt", "C:/src/build/main.cpp\nC:/src/main.cpp\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"locate.exe", {L"-d", L"db.txt", L"-b", L"-w", L"build/main"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("C:/src/build/main.cpp") != std::string::npos);
}

TEST(locate, locate_count) {
  TempDir tmp;
  write_db(tmp, "db.txt", "C:/w/a.cpp\nC:/w/b.cpp\nC:/w/readme.md\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"locate.exe", {L"-d", L"db.txt", L"-c", L"cpp"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "2\n");
}

TEST(locate, locate_null_separator_output) {
  TempDir tmp;
  write_db(tmp, "db.txt", "C:/w/a.cpp\nC:/w/b.cpp\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"locate.exe", {L"-d", L"db.txt", L"-0", L"cpp"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, std::string("C:/w/a.cpp\0C:/w/b.cpp\0", 22));
}

TEST(locate, locate_statistics_prints_entries_to_stderr) {
  TempDir tmp;
  write_db(tmp, "db.txt", "C:/w/a.cpp\nC:/w/b.cpp\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"locate.exe", {L"-d", L"db.txt", L"-S", L"a"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("Entries: 2") != std::string::npos);
}

TEST(locate, locate_all_prints_every_entry) {
  TempDir tmp;
  write_db(tmp, "db.txt", "C:/w/a.cpp\nC:/w/b.cpp\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"locate.exe", {L"-d", L"db.txt", L"-A"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("C:/w/a.cpp") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("C:/w/b.cpp") != std::string::npos);
}

TEST(locate, locate_missing_database_reports_and_exits_one) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"locate.exe", {L"-d", L"no-such-db.txt", L"anything"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("cannot open database") != std::string::npos);
}

TEST(locate, locate_limit_zero_means_unlimited) {
  TempDir tmp;
  write_db(tmp, "db.txt", "C:/w/a.cpp\nC:/w/b.cpp\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"locate.exe", {L"-d", L"db.txt", L"-l", L"0", L"cpp"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("a.cpp") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b.cpp") != std::string::npos);
}
