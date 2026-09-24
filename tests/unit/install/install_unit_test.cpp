// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

auto install_is_readonly(const std::filesystem::path& path) -> bool {
  DWORD attrs = GetFileAttributesW(path.wstring().c_str());
  return attrs != INVALID_FILE_ATTRIBUTES &&
         (attrs & FILE_ATTRIBUTE_READONLY) != 0;
}

auto install_set_readonly(const std::filesystem::path& path, bool readonly)
    -> bool {
  DWORD attrs = GetFileAttributesW(path.wstring().c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return false;
  }
  if (readonly) {
    attrs |= FILE_ATTRIBUTE_READONLY;
  } else {
    attrs &= ~FILE_ATTRIBUTE_READONLY;
  }
  return SetFileAttributesW(path.wstring().c_str(), attrs) != 0;
}

TEST(install, install_basic) {
  TempDir tmp;
  tmp.write("source.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"source.txt", L"dest.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "dest.txt"));
  std::string content = tmp.read("dest.txt");
  EXPECT_EQ(content, "hello\n");
}

TEST(install, install_directory) {
  TempDir tmp;
  tmp.write("source.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"-d", L"parent\\newdir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "parent" / "newdir"));
}

TEST(install, install_target_directory) {
  TempDir tmp;
  tmp.write("source.txt", "hello\n");
  std::filesystem::create_directory(tmp.path / "dest_dir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"-t", L"dest_dir", L"source.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "dest_dir" / "source.txt"));
}

TEST(install, install_target_directory_requires_existing_directory) {
  TempDir tmp;
  tmp.write("source.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"-t", L"missing_dir", L"source.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "missing_dir"));
}

TEST(install, install_default_mode_clears_source_readonly_like_gnu_755) {
  TempDir tmp;
  tmp.write("source.txt", "hello\n");
  EXPECT_TRUE(install_set_readonly(tmp.path / "source.txt", true));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"source.txt", L"dest.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(install_is_readonly(tmp.path / "dest.txt"));
}

TEST(install, install_mode_numeric_644_keeps_owner_writable) {
  TempDir tmp;
  tmp.write("source.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"-m", L"644", L"source.txt", L"dest.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(install_is_readonly(tmp.path / "dest.txt"));
}

TEST(install, install_mode_numeric_444_sets_readonly) {
  TempDir tmp;
  tmp.write("source.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"-m", L"444", L"source.txt", L"dest.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(install_is_readonly(tmp.path / "dest.txt"));
}

TEST(install, install_mode_symbolic_owner_write_clears_readonly) {
  TempDir tmp;
  tmp.write("source.txt", "hello\n");
  EXPECT_TRUE(install_set_readonly(tmp.path / "source.txt", true));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"-m", L"u+w", L"source.txt", L"dest.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(install_is_readonly(tmp.path / "dest.txt"));
}

TEST(install, install_compare_reapplies_mode_when_content_matches) {
  TempDir tmp;
  tmp.write("source.txt", "same\n");
  tmp.write("dest.txt", "same\n");
  EXPECT_TRUE(install_set_readonly(tmp.path / "dest.txt", false));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"-C", L"-m", L"444", L"source.txt", L"dest.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(install_is_readonly(tmp.path / "dest.txt"));
}

TEST(install, install_target_directory_and_no_target_directory_conflict) {
  TempDir tmp;
  tmp.write("source.txt", "hello\n");
  std::filesystem::create_directory(tmp.path / "dest_dir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"-t", L"dest_dir", L"-T", L"source.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "dest_dir" / "source.txt"));
}

TEST(install, install_create_leading_dirs) {
  TempDir tmp;
  tmp.write("source.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"-D", L"source.txt", L"nested\\dir\\dest.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(
      std::filesystem::exists(tmp.path / "nested" / "dir" / "dest.txt"));
}

TEST(install, install_create_target_directory_with_D_and_t) {
  TempDir tmp;
  tmp.write("source.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"-D", L"-t", L"nested\\dir", L"source.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(
      std::filesystem::exists(tmp.path / "nested" / "dir" / "source.txt"));
}

TEST(install, install_no_target_directory) {
  TempDir tmp;
  tmp.write("source.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"-T", L"source.txt", L"dest.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "dest.txt"));
}

TEST(install, install_compare_skips_identical_destination) {
  TempDir tmp;
  tmp.write("source.txt", "hello\n");
  tmp.write("dest.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"-C", L"-b", L"source.txt", L"dest.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "dest.txt~"));
  EXPECT_EQ(tmp.read("dest.txt"), "hello\n");
}

TEST(install, install_preserve_timestamps) {
  TempDir tmp;
  tmp.write("source.txt", "hello\n");

  auto old_time =
      std::filesystem::file_time_type::clock::now() - std::chrono::hours(24);
  std::filesystem::last_write_time(tmp.path / "source.txt", old_time);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"-p", L"source.txt", L"dest.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  auto source_time = std::filesystem::last_write_time(tmp.path / "source.txt");
  auto dest_time = std::filesystem::last_write_time(tmp.path / "dest.txt");
  EXPECT_EQ(dest_time, source_time);
}

TEST(install, install_destination_operand_is_literal_not_glob) {
  TempDir tmp;
  tmp.write("source.txt", "new\n");
  tmp.write("desta.txt", "old\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"source.txt", L"dest[abc].txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("desta.txt"), "old\n");
  EXPECT_EQ(tmp.read("dest[abc].txt"), "new\n");
}

TEST(install, install_directory_operand_is_literal_not_glob) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "dira");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"-d", L"dir[abc]"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "dir[abc]"));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "dira"));
}

TEST(install, install_multiple_sources_require_existing_directory) {
  TempDir tmp;
  tmp.write("file1.txt", "one\n");
  tmp.write("file2.txt", "two\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"file1.txt", L"file2.txt", L"missing_dir"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "missing_dir"));
}

// [GNU 9.4] single copy-mode operand is a missing destination.
TEST(install, install_single_operand_reports_missing_destination) {
  TempDir tmp;
  tmp.write("only.txt", "x\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"only.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "install: missing destination file operand after "
                 "'only.txt'\n"
                 "Try 'install --help' for more information.\n");
}

// [GNU quoteaf] control bytes in -m are octal-escaped (uutils#13834).
TEST(install, install_invalid_mode_octal_escapes_control_byte) {
  TempDir tmp;
  tmp.write("a.txt", "x\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"-m", L"\x01", L"a.txt", L"b.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text, "install: invalid mode '\\001'\n");
}

// [GNU] -T onto an existing directory is refused.
TEST(install, install_no_target_directory_refuses_existing_directory) {
  TempDir tmp;
  tmp.write("a.txt", "x\n");
  std::filesystem::create_directory(tmp.path / "destdir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"-T", L"a.txt", L"destdir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "install: cannot overwrite directory 'destdir' with "
                 "non-directory\n");
}

// [GNU] -d -v announces every component it creates (uutils#8963 family).
TEST(install, install_directory_verbose_announces_each_component) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"-d", L"-v", L"aa/bb"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "install: creating directory 'aa'\n"
                 "install: creating directory 'aa/bb'\n");
}

// [GNU] -d -m applies the mode to the named directory only; ancestors use
// the default mode (mkdir-p semantics, uutils#9302).
TEST(install, install_directory_mode_applies_to_final_dir_only) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"-d", L"-m", L"555", L"pp/qq"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  auto parent = tmp.path / "pp";
  auto leaf = tmp.path / "pp" / "qq";
  DWORD pattrs = GetFileAttributesW(parent.wstring().c_str());
  DWORD lattrs = GetFileAttributesW(leaf.wstring().c_str());
  EXPECT_TRUE((pattrs & FILE_ATTRIBUTE_READONLY) == 0);
  EXPECT_TRUE((lattrs & FILE_ATTRIBUTE_READONLY) != 0);
}

TEST(install, install_wildcard_multiple_sources_require_existing_directory) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\n");
  tmp.write("b.txt", "beta\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"install.exe", {L"*.txt", L"missing_dir"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "missing_dir"));
}
