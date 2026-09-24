// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include <filesystem>
#include <fstream>

#include "framework/winuxtest.h"

namespace {

auto make_tree(TempDir& tmp) -> void {
  std::filesystem::create_directories(tmp.path / L"docs");
  std::filesystem::create_directories(tmp.path / L"node_modules" / L"pkg");
  std::filesystem::create_directories(tmp.path / L"keep");
  auto touch = [&](const std::wstring& rel) {
    std::ofstream out(tmp.path / rel, std::ios::binary);
    out << "x";
  };
  touch(L"docs/readme.md");
  touch(L"node_modules/pkg/index.js");
  touch(L"keep/keep.txt");
  touch(L"top.txt");
}

}  // namespace

TEST(updatedb, updatedb_writes_sorted_path_lines) {
  TempDir tmp;
  make_tree(tmp);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"updatedb.exe", {L"-d", L".", L"-o", L"out.db"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  std::ifstream in(tmp.path / "out.db", std::ios::binary);
  std::string body((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  // Paths are stored sorted, one per line.
  EXPECT_TRUE(body.find("top.txt") != std::string::npos);
  EXPECT_TRUE(body.find("docs/readme.md") != std::string::npos);
  EXPECT_TRUE(body.find("keep/keep.txt") != std::string::npos);
  EXPECT_TRUE(body.find("node_modules/pkg/index.js") != std::string::npos);
  EXPECT_TRUE(body.find("node_modules") < body.find("top.txt"));
}

// [GNU] updatedb.sh: "--localpaths='path1 path2...'" restricts the walk to
// the given roots (semicolon separated in WinuxCmd).
TEST(updatedb, updatedb_localpaths_restricts_walk_roots) {
  TempDir tmp;
  make_tree(tmp);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"updatedb.exe",
        {L"--localpaths", std::wstring(tmp.wpath()) + L"\\docs", L"-o",
         L"out.db"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  std::ifstream in(tmp.path / "out.db", std::ios::binary);
  std::string body((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  EXPECT_TRUE(body.find("readme.md") != std::string::npos);
  EXPECT_TRUE(body.find("top.txt") == std::string::npos);
  EXPECT_TRUE(body.find("keep.txt") == std::string::npos);
}

// [GNU] updatedb.sh: "--prunepaths='/tmp /var/spool'" directories are not
// recorded and not descended into.
TEST(updatedb, updatedb_prunepaths_excludes_subtree) {
  TempDir tmp;
  make_tree(tmp);

  std::wstring prune = std::wstring(tmp.wpath()) + L"\\node_modules";
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"updatedb.exe",
        {L"--prunepaths", prune, L"-d", L".", L"-o", L"out.db"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  std::ifstream in(tmp.path / "out.db", std::ios::binary);
  std::string body((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  EXPECT_TRUE(body.find("node_modules") == std::string::npos);
  EXPECT_TRUE(body.find("docs/readme.md") != std::string::npos);
  EXPECT_TRUE(body.find("top.txt") != std::string::npos);
}

// -x/--exclude-dir never descends into directories whose name matches the
// wildcard pattern.
TEST(updatedb, updatedb_exclude_dir_skips_matching_directories) {
  TempDir tmp;
  make_tree(tmp);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"updatedb.exe",
        {L"-x", L"node_modules", L"-d", L".", L"-o", L"out.db"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  std::ifstream in(tmp.path / "out.db", std::ios::binary);
  std::string body((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  EXPECT_TRUE(body.find("node_modules") == std::string::npos);
  EXPECT_TRUE(body.find("docs/readme.md") != std::string::npos);
}

TEST(updatedb, updatedb_exclude_pattern_filters_paths) {
  TempDir tmp;
  make_tree(tmp);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"updatedb.exe",
        {L"-e", L"node_modules", L"-d", L".", L"-o", L"out.db"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  std::ifstream in(tmp.path / "out.db", std::ios::binary);
  std::string body((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  EXPECT_TRUE(body.find("node_modules") == std::string::npos);
  EXPECT_TRUE(body.find("top.txt") != std::string::npos);
}

// [GNU] updatedb.sh writes a temporary database and renames it into place;
// the temp file must not survive a successful run.
TEST(updatedb, updatedb_atomic_replace_leaves_no_temp_file) {
  TempDir tmp;
  make_tree(tmp);

  // A previous database exists and must be replaced wholesale.
  {
    std::ofstream old(tmp.path / "out.db", std::ios::binary);
    old << "stale content\n";
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"updatedb.exe", {L"-d", L".", L"-o", L"out.db"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "out.db.tmp"));
  std::ifstream in(tmp.path / "out.db", std::ios::binary);
  std::string body((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  EXPECT_TRUE(body.find("stale content") == std::string::npos);
  EXPECT_TRUE(body.find("top.txt") != std::string::npos);
}

TEST(updatedb, updatedb_output_long_option_matches_short) {
  TempDir tmp;
  make_tree(tmp);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"updatedb.exe", {L"--directory", L".", L"--output", L"long.db"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "long.db"));
}

TEST(updatedb, updatedb_verbose_reports_progress) {
  TempDir tmp;
  make_tree(tmp);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"updatedb.exe", {L"-v", L"-d", L".", L"-o", L"out.db"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("Walking") != std::string::npos ||
              r.stdout_text.find("Walking") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("Found") != std::string::npos ||
              r.stdout_text.find("Found") != std::string::npos);
}
