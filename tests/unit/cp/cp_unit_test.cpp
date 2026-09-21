// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include <chrono>

#include "framework/winuxtest.h"

TEST(cp, cp_basic_copy) {
  TempDir tmp;
  tmp.write("source.txt", "hello world");

  TEST_LOG_FILE_CONTENT("source.txt", "hello world");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"source.txt", L"dest.txt"});

  TEST_LOG_CMD_LIST("cp.exe", L"source.txt", L"dest.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("cp.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify the destination file was created and has the correct content
  std::string dest_content = tmp.read("dest.txt");
  TEST_LOG("dest.txt content", dest_content);
  EXPECT_EQ(dest_content, "hello world");
}

TEST(cp, cp_empty_file_succeeds) {
  TempDir tmp;
  tmp.write("empty.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"empty.txt", L"copy.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "copy.txt"));
  EXPECT_EQ(tmp.read("copy.txt"), "");
}

TEST(cp, cp_link_creates_hard_link) {
  TempDir tmp;
  tmp.write("source.txt", "payload");
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"-l", L"source.txt", L"dest.txt"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("dest.txt"), "payload");
  tmp.write("source.txt", "changed");
  EXPECT_EQ(tmp.read("dest.txt"), "changed");
}

TEST(cp, cp_copy_multiple_files) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");
  tmp.write("file2.txt", "content2");
  std::filesystem::create_directory(tmp.path / "dest_dir");

  TEST_LOG_FILE_CONTENT("file1.txt", "content1");
  TEST_LOG_FILE_CONTENT("file2.txt", "content2");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"file1.txt", L"file2.txt", L"dest_dir"});

  TEST_LOG_CMD_LIST("cp.exe", L"file1.txt", L"file2.txt", L"dest_dir");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("cp.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify the files were copied to the destination directory
  std::string dest1_content = tmp.read("dest_dir/file1.txt");
  std::string dest2_content = tmp.read("dest_dir/file2.txt");
  TEST_LOG("dest_dir/file1.txt content", dest1_content);
  TEST_LOG("dest_dir/file2.txt content", dest2_content);
  EXPECT_EQ(dest1_content, "content1");
  EXPECT_EQ(dest2_content, "content2");
}

TEST(cp, cp_recursive_copy) {
  TempDir tmp;

  // Create source directory structure
  std::filesystem::create_directory(tmp.path / "src_dir");
  std::filesystem::create_directory(tmp.path / "src_dir/sub_dir");
  tmp.write("src_dir/file1.txt", "content1");
  tmp.write("src_dir/sub_dir/file2.txt", "content2");

  TEST_LOG_FILE_CONTENT("src_dir/file1.txt", "content1");
  TEST_LOG_FILE_CONTENT("src_dir/sub_dir/file2.txt", "content2");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"-r", L"src_dir", L"dest_dir"});

  TEST_LOG_CMD_LIST("cp.exe", L"-r", L"src_dir", L"dest_dir");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("cp.exe -r output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify the directory structure was copied recursively
  bool dest_dir_exists = std::filesystem::exists(tmp.path / "dest_dir") &&
                         std::filesystem::is_directory(tmp.path / "dest_dir");
  bool sub_dir_exists =
      std::filesystem::exists(tmp.path / "dest_dir" / "sub_dir") &&
      std::filesystem::is_directory(tmp.path / "dest_dir" / "sub_dir");
  EXPECT_TRUE(dest_dir_exists);
  EXPECT_TRUE(sub_dir_exists);

  // Verify the files were copied correctly
  std::string dest1_content = tmp.read("dest_dir/file1.txt");
  std::string dest2_content = tmp.read("dest_dir/sub_dir/file2.txt");
  TEST_LOG("dest_dir/file1.txt content", dest1_content);
  TEST_LOG("dest_dir/sub_dir/file2.txt content", dest2_content);
  EXPECT_EQ(dest1_content, "content1");
  EXPECT_EQ(dest2_content, "content2");
}

TEST(cp, cp_archive_copies_directories_recursively) {
  TempDir tmp;

  std::filesystem::create_directory(tmp.path / "src_dir");
  std::filesystem::create_directory(tmp.path / "src_dir/sub_dir");
  tmp.write("src_dir/file1.txt", "content1");
  tmp.write("src_dir/sub_dir/file2.txt", "content2");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"-a", L"src_dir", L"dest_dir"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::is_directory(tmp.path / "dest_dir"));
  EXPECT_TRUE(std::filesystem::is_directory(tmp.path / "dest_dir/sub_dir"));
  EXPECT_EQ(tmp.read("dest_dir/file1.txt"), "content1");
  EXPECT_EQ(tmp.read("dest_dir/sub_dir/file2.txt"), "content2");
}

TEST(cp, cp_preserve_timestamps_with_p) {
  TempDir tmp;
  tmp.write("source.txt", "content");

  auto old_time =
      std::filesystem::file_time_type::clock::now() - std::chrono::hours(24);
  std::filesystem::last_write_time(tmp.path / "source.txt", old_time);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"-p", L"source.txt", L"dest.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  auto source_time = std::filesystem::last_write_time(tmp.path / "source.txt");
  auto dest_time = std::filesystem::last_write_time(tmp.path / "dest.txt");
  auto delta = source_time > dest_time ? source_time - dest_time
                                       : dest_time - source_time;
  EXPECT_TRUE(delta <= std::chrono::seconds(2));
}

TEST(cp, cp_verbose) {
  TempDir tmp;
  tmp.write("source.txt", "content");

  TEST_LOG_FILE_CONTENT("source.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"-v", L"source.txt", L"dest.txt"});

  TEST_LOG_CMD_LIST("cp.exe", L"-v", L"source.txt", L"dest.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("cp.exe -v output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify the output contains verbose information
  EXPECT_TRUE(r.stdout_text.find("'source.txt' -> 'dest.txt'") !=
              std::string::npos);

  // Verify the file was copied correctly
  std::string dest_content = tmp.read("dest.txt");
  TEST_LOG("dest.txt content", dest_content);
  EXPECT_EQ(dest_content, "content");
}

TEST(cp, cp_target_directory) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");
  tmp.write("file2.txt", "content2");
  std::filesystem::create_directory(tmp.path / "dest_dir");

  TEST_LOG_FILE_CONTENT("file1.txt", "content1");
  TEST_LOG_FILE_CONTENT("file2.txt", "content2");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe",
        {L"--target-directory", L"dest_dir", L"file1.txt", L"file2.txt"});

  TEST_LOG_CMD_LIST("cp.exe", L"--target-directory", L"dest_dir", L"file1.txt",
                    L"file2.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("cp.exe --target-directory output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify the files were copied to the destination directory
  std::string dest1_content = tmp.read("dest_dir/file1.txt");
  std::string dest2_content = tmp.read("dest_dir/file2.txt");
  TEST_LOG("dest_dir/file1.txt content", dest1_content);
  TEST_LOG("dest_dir/file2.txt content", dest2_content);
  EXPECT_EQ(dest1_content, "content1");
  EXPECT_EQ(dest2_content, "content2");
}

TEST(cp, cp_target_directory_requires_directory) {
  TempDir tmp;
  tmp.write("source.txt", "content");
  tmp.write("not_dir", "plain file");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"-t", L"not_dir", L"source.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_EQ(tmp.read("not_dir"), "plain file");
}

TEST(cp, cp_target_directory_and_no_target_directory_conflict) {
  TempDir tmp;
  tmp.write("source.txt", "content");
  std::filesystem::create_directory(tmp.path / "dest_dir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"-t", L"dest_dir", L"-T", L"source.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
}

TEST(cp, cp_strip_trailing_slashes_allows_file_source_with_trailing_separator) {
  TempDir tmp;
  tmp.write("source.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"--strip-trailing-slashes", L"source.txt/", L"dest.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("dest.txt"), "content");
}

TEST(cp,
     cp_file_source_with_trailing_separator_still_fails_without_strip_option) {
  TempDir tmp;
  tmp.write("source.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"source.txt/", L"dest.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("cannot stat 'source.txt/'") !=
              std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "dest.txt"));
}

TEST(cp, cp_parents_preserves_forward_slash_path) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "src" / "nested");
  std::filesystem::create_directory(tmp.path / "dest");
  tmp.write("src/nested/file.txt", "payload");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"--parents", L"src/nested/file.txt", L"dest"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("dest/src/nested/file.txt"), "payload");
}

TEST(cp, cp_no_clobber) {
  TempDir tmp;
  tmp.write("source.txt", "new content");
  tmp.write("dest.txt", "old content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"-n", L"source.txt", L"dest.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("dest.txt"), "old content");
}

TEST(cp, cp_backup_creates_tilde_file) {
  TempDir tmp;
  tmp.write("source.txt", "new content");
  tmp.write("dest.txt", "old content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"-b", L"source.txt", L"dest.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("dest.txt"), "new content");
  EXPECT_EQ(tmp.read("dest.txt~"), "old content");
}

TEST(cp, cp_backup_suffix_uses_custom_suffix) {
  TempDir tmp;
  tmp.write("source.txt", "new content");
  tmp.write("dest.txt", "old content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"--backup", L"-S", L".bak", L"source.txt", L"dest.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("dest.txt"), "new content");
  EXPECT_EQ(tmp.read("dest.txt.bak"), "old content");
}

TEST(cp, cp_backup_numbered_control_creates_next_numbered_file) {
  TempDir tmp;
  tmp.write("source.txt", "new content");
  tmp.write("dest.txt", "old content");
  tmp.write("dest.txt.~1~", "first backup");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"--backup=numbered", L"source.txt", L"dest.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("dest.txt"), "new content");
  EXPECT_EQ(tmp.read("dest.txt.~1~"), "first backup");
  EXPECT_EQ(tmp.read("dest.txt.~2~"), "old content");
}

TEST(cp, cp_backup_existing_control_uses_numbered_when_numbered_exists) {
  TempDir tmp;
  tmp.write("source.txt", "new content");
  tmp.write("dest.txt", "old content");
  tmp.write("dest.txt.~1~", "first backup");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"--backup=existing", L"source.txt", L"dest.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("dest.txt"), "new content");
  EXPECT_EQ(tmp.read("dest.txt.~1~"), "first backup");
  EXPECT_EQ(tmp.read("dest.txt.~2~"), "old content");
}

TEST(cp, cp_backup_none_control_overwrites_without_backup) {
  TempDir tmp;
  tmp.write("source.txt", "new content");
  tmp.write("dest.txt", "old content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"--backup=none", L"source.txt", L"dest.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("dest.txt"), "new content");
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "dest.txt~"));
}

TEST(cp, cp_refuses_copy_onto_self) {
  TempDir tmp;
  tmp.write("file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"file.txt", L"file.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_EQ(tmp.read("file.txt"), "content");
}

TEST(cp, cp_force_backup_allows_self_backup) {
  TempDir tmp;
  tmp.write("file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"--force", L"--backup", L"file.txt", L"file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("file.txt"), "content");
  EXPECT_EQ(tmp.read("file.txt~"), "content");
}

TEST(cp, cp_missing_dest_parent_fails) {
  TempDir tmp;
  tmp.write("source.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"source.txt", L"missing\\dest.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "missing" / "dest.txt"));
}

TEST(cp, cp_update_skips_newer_destination) {
  TempDir tmp;
  tmp.write("source.txt", "source content");
  tmp.write("dest.txt", "newer destination");

  const auto now = std::filesystem::file_time_type::clock::now();
  std::filesystem::last_write_time(tmp.path / "source.txt",
                                   now - std::chrono::hours(2));
  std::filesystem::last_write_time(tmp.path / "dest.txt", now);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"-u", L"source.txt", L"dest.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("dest.txt"), "newer destination");
}

TEST(cp, cp_update_overwrites_older_destination) {
  TempDir tmp;
  tmp.write("source.txt", "newer source");
  tmp.write("dest.txt", "old destination");

  const auto now = std::filesystem::file_time_type::clock::now();
  std::filesystem::last_write_time(tmp.path / "dest.txt",
                                   now - std::chrono::hours(2));
  std::filesystem::last_write_time(tmp.path / "source.txt", now);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"--update", L"source.txt", L"dest.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("dest.txt"), "newer source");
}

TEST(cp, cp_wildcard_sources_expand) {
  TempDir tmp;
  tmp.write("a.txt", "alpha");
  tmp.write("b.txt", "beta");
  std::filesystem::create_directory(tmp.path / "dest_dir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"*.txt", L"dest_dir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("dest_dir/a.txt"), "alpha");
  EXPECT_EQ(tmp.read("dest_dir/b.txt"), "beta");
}

TEST(cp, cp_attributes_only_creates_missing_destination) {
  TempDir tmp;
  tmp.write("source.txt", "source content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"--attributes-only", L"source.txt", L"dest.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "dest.txt"));
  EXPECT_EQ(tmp.read("dest.txt"), "");
}

// [GNU 9.4] operand diagnostics must be byte-exact (uutils #995 family).
TEST(cp, cp_no_operands_reports_missing_file_operand) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "cp: missing file operand\n"
                 "Try 'cp --help' for more information.\n");
}

TEST(cp, cp_single_operand_reports_missing_destination) {
  TempDir tmp;
  tmp.write("only.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"only.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "cp: missing destination file operand after 'only.txt'\n"
                 "Try 'cp --help' for more information.\n");
}

TEST(cp, cp_multi_source_missing_target_reports_errno) {
  TempDir tmp;
  tmp.write("a.txt", "a");
  tmp.write("b.txt", "b");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"a.txt", L"b.txt", L"notdir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "cp: target 'notdir': No such file or directory\n");
}

TEST(cp, cp_directory_without_recursive_uses_gnu_wording) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "srcdir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"srcdir", L"dest"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "cp: -r not specified; omitting directory 'srcdir'\n");
}

// [GNU 9.4] -n prints the deprecation warning and still copies.
TEST(cp, cp_no_clobber_emits_deprecation_warning) {
  TempDir tmp;
  tmp.write("src.txt", "data");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"-n", L"src.txt", L"out.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "cp: warning: behavior of -n is non-portable and may change in "
      "future; use --update=none instead\n");
  EXPECT_EQ(tmp.read("out.txt"), "data");
}

// [GNU] -Z/--context is a silent no-op on non-SELinux systems (#995).
TEST(cp, cp_context_option_is_silent_noop) {
  TempDir tmp;
  tmp.write("src.txt", "data");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"-Z", L"src.txt", L"out.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ(tmp.read("out.txt"), "data");
}

// [GNU copy.c] -s with a relative SOURCE refuses when DEST lands outside
// the current directory (#218/#274).
TEST(cp, cp_symbolic_link_relative_source_outside_cwd_fails) {
  TempDir tmp;
  tmp.write("f.txt", "data");
  std::filesystem::create_directory(tmp.path / "sub");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"-s", L"f.txt", L"sub"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find(
                "can make relative symbolic links only in current directory"),
            std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "sub" / "f.txt"));
}

// --- Issue #1066 regression coverage ---

// [GNU] -d (--no-dereference --preserve=links) and -P recreate a symlink
// source as a link at the destination instead of copying the referenced
// file's contents (#1066).
TEST(cp, cp_no_dereference_recreates_symlink) {
  TempDir tmp;
  tmp.write("target.txt", "payload");

  std::error_code ec;
  std::filesystem::create_symlink(tmp.path / "target.txt", tmp.path / "link",
                                  ec);
  if (ec) {
    // Same skip policy as the ln symlink tests.
    std::cout
        << "  SKIPPED (requires administrator privileges for symbolic links)\n";
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"-d", L"link", L"dest_d"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::is_symlink(
      std::filesystem::symlink_status(tmp.path / "dest_d", ec)));

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"cp.exe", {L"-P", L"link", L"dest_p"});
  auto r2 = p2.run();
  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_TRUE(std::filesystem::is_symlink(
      std::filesystem::symlink_status(tmp.path / "dest_p", ec)));
}

// [GNU] symlinks inside a recursively copied tree stay symlinks under
// -d/-a (and plain -r) (#1066).
TEST(cp, cp_archive_preserves_symlinks_in_tree) {
  TempDir tmp;
  tmp.write("srcdir/real.txt", "content");

  std::error_code ec;
  std::filesystem::create_symlink(tmp.path / "srcdir" / "real.txt",
                                  tmp.path / "srcdir" / "link", ec);
  if (ec) {
    std::cout
        << "  SKIPPED (requires administrator privileges for symbolic links)\n";
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"-a", L"srcdir", L"destdir"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("destdir/real.txt"), "content");
  EXPECT_TRUE(std::filesystem::is_symlink(
      std::filesystem::symlink_status(tmp.path / "destdir" / "link", ec)));
}

// [GNU] a dangling symlink source is recreated as a dangling link under -d.
TEST(cp, cp_no_dereference_recreates_dangling_symlink) {
  TempDir tmp;

  std::error_code ec;
  std::filesystem::create_symlink(tmp.path / "missing-target",
                                  tmp.path / "dangling", ec);
  if (ec) {
    std::cout
        << "  SKIPPED (requires administrator privileges for symbolic links)\n";
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"-d", L"dangling", L"dest"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::is_symlink(
      std::filesystem::symlink_status(tmp.path / "dest", ec)));
}

// [GNU] --reflink=auto silently falls back to a normal copy when the
// filesystem cannot clone (NTFS); bare --reflink means auto (#1066).
TEST(cp, cp_reflink_auto_falls_back_to_copy) {
  TempDir tmp;
  tmp.write("src.txt", "payload");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"--reflink=auto", L"src.txt", L"dest.txt"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("dest.txt"), "payload");

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"cp.exe", {L"--reflink", L"src.txt", L"dest2.txt"});
  auto r2 = p2.run();
  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_EQ(tmp.read("dest2.txt"), "payload");
}

// [GNU] --reflink=always must either clone (ReFS) or fail with the
// GNU-style "failed to clone 'd' from 's': Not supported" (#1066).
TEST(cp, cp_reflink_always_clones_or_reports) {
  TempDir tmp;
  tmp.write("src.txt", "payload");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"--reflink=always", L"src.txt", L"dest.txt"});
  auto r = p.run();

  if (r.exit_code != 0) {
    EXPECT_TRUE(
        r.stderr_text.find("failed to clone 'dest.txt' from 'src.txt'") !=
        std::string::npos);
  } else {
    EXPECT_EQ(tmp.read("dest.txt"), "payload");
  }
}

TEST(cp, cp_reflink_invalid_value_rejected) {
  TempDir tmp;
  tmp.write("src.txt", "payload");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"--reflink=bogus", L"src.txt", L"dest.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("invalid argument 'bogus' for '--reflink'") !=
              std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "dest.txt"));
}

// [GNU] --sparse=auto on a dense source performs a normal copy (#1066).
TEST(cp, cp_sparse_auto_copies_normally) {
  TempDir tmp;
  tmp.write("src.bin", "payload");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"--sparse=auto", L"src.bin", L"dest.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("dest.bin"), "payload");
}

TEST(cp, cp_sparse_never_copies_normally) {
  TempDir tmp;
  tmp.write("src.bin", "payload");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"--sparse=never", L"src.bin", L"dest.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("dest.bin"), "payload");
}

// [GNU] --sparse=always marks the destination sparse and skips zero runs;
// on filesystems without sparse support the copy still succeeds (#1066).
TEST(cp, cp_sparse_always_writes_only_nonzero_extents) {
  TempDir tmp;

  std::string expected = "head";
  expected.append(128 * 1024, '\0');
  expected.append("tail");
  tmp.write("src.bin", expected);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"--sparse=always", L"src.bin", L"dest.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("dest.bin"), expected);

  DWORD attrs = tmp.attrs("dest.bin");
  EXPECT_NE(attrs, INVALID_FILE_ATTRIBUTES);
  if (attrs != INVALID_FILE_ATTRIBUTES &&
      (attrs & FILE_ATTRIBUTE_SPARSE_FILE) == 0) {
    // Filesystem cannot mark files sparse (e.g. FAT32): the copy itself is
    // still correct, only the space saving is absent.
    std::cout << "  SKIPPED (filesystem does not support sparse files)\n";
    return;
  }

  // The zero run must not consume allocated clusters.
  ULARGE_INTEGER alloc{};
  alloc.LowPart = GetCompressedFileSizeW(
      (tmp.path / "dest.bin").wstring().c_str(), &alloc.HighPart);
  EXPECT_TRUE(alloc.QuadPart < expected.size());
}

TEST(cp, cp_sparse_invalid_value_rejected) {
  TempDir tmp;
  tmp.write("src.bin", "payload");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"--sparse=bogus", L"src.bin", L"dest.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("invalid argument 'bogus' for '--sparse'") !=
              std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "dest.bin"));
}

// [GNU] --copy-contents reads a special file's contents; for our
// pseudo-devices that yields an empty destination (#1066).
TEST(cp, cp_copy_contents_dev_null_creates_empty_file) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"--copy-contents", L"/dev/null", L"dest.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "dest.txt"));
  EXPECT_EQ(tmp.read("dest.txt"), "");
}

// [GNU] --path is a deprecated alias for --parents (#1066).
TEST(cp, cp_path_alias_behaves_like_parents) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "src" / "nested");
  std::filesystem::create_directory(tmp.path / "dest");
  tmp.write("src/nested/file.txt", "payload");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"--path", L"src/nested/file.txt", L"dest"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("dest/src/nested/file.txt"), "payload");
}

// [GNU] with --parents the destination must be an existing directory
// (#1066).
TEST(cp, cp_parents_requires_directory_destination) {
  TempDir tmp;
  tmp.write("src.txt", "payload");
  tmp.write("notdir", "plain file");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"--parents", L"src.txt", L"notdir"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "with --parents, the destination must be a directory") !=
              std::string::npos);
  EXPECT_EQ(tmp.read("notdir"), "plain file");

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"cp.exe", {L"--parents", L"src.txt", L"missing"});
  auto r2 = p2.run();

  EXPECT_EQ(r2.exit_code, 1);
  EXPECT_TRUE(r2.stderr_text.find(
                  "with --parents, the destination must be a directory") !=
              std::string::npos);
}

// [DIFFERS] #1101: NT resolves reparse-point link text with the Win32 path
// parser, which only accepts backslash separators. A relative -s source
// written with forward slashes (e.g. "./src.txt") used to be stored
// verbatim and failed native resolution with ERROR_INVALID_NAME.
TEST(cp, cp_symbolic_forward_slash_source_resolves_natively) {
  TempDir tmp;
  tmp.write("src.txt", "payload");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cp.exe", {L"-s", L"./src.txt", L"dest.txt"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("cp -s stderr", r.stderr_text);

  if (r.exit_code != 0) {
    std::cout
        << "  SKIPPED (requires administrator privileges for symbolic links)\n";
    return;
  }
  EXPECT_EQ(r.exit_code, 0);

  std::error_code ec;
  auto stored =
      std::filesystem::read_symlink(tmp.path / L"dest.txt", ec).wstring();
  EXPECT_FALSE(ec);
  EXPECT_TRUE(stored.find(L'/') == std::wstring::npos);

  // CreateFileW on the plain path exercises native reparse resolution.
  HANDLE file = CreateFileW((tmp.path / L"dest.txt").wstring().c_str(),
                            GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  EXPECT_NE(file, INVALID_HANDLE_VALUE);
  if (file != INVALID_HANDLE_VALUE) {
    char buffer[64] = {};
    DWORD read = 0;
    ReadFile(file, buffer, sizeof(buffer) - 1, &read, nullptr);
    CloseHandle(file);
    EXPECT_EQ_TEXT(std::string(buffer, read), "payload");
  }
}
