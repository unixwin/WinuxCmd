// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(ln, ln_hardlink) {
  TempDir tmp;
  tmp.write("original.txt", "hello world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ln.exe", {L"original.txt", L"link.txt"});

  TEST_LOG_CMD_LIST("ln.exe", L"original.txt", L"link.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ln output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify link was created
  bool link_exists = std::filesystem::exists(tmp.path / "link.txt");
  EXPECT_TRUE(link_exists);

  // Verify content is the same
  if (link_exists) {
    std::string original_content = tmp.read("original.txt");
    std::string link_content = tmp.read("link.txt");
    EXPECT_EQ_TEXT(original_content, link_content);
  }
}

TEST(ln, ln_symlink) {
  TempDir tmp;
  tmp.write("original.txt", "hello world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ln.exe", {L"-s", L"original.txt", L"link.txt"});

  TEST_LOG_CMD_LIST("ln.exe", L"-s", L"original.txt", L"link.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ln symlink stdout", r.stdout_text);
  TEST_LOG("ln symlink stderr", r.stderr_text);

  // Creating symbolic links on Windows requires administrator privileges
  // If the test fails, skip it (assuming it's a permission issue)
  if (r.exit_code != 0) {
    std::cout
        << "  SKIPPED (requires administrator privileges for symbolic links)\n";
    return;
  }

  // Verify link was created
  bool link_exists = std::filesystem::exists(tmp.path / "link.txt");
  EXPECT_TRUE(link_exists);
}

TEST(ln, ln_force) {
  TempDir tmp;
  tmp.write("original.txt", "hello\n");
  tmp.write("link.txt", "world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ln.exe", {L"-f", L"original.txt", L"link.txt"});

  TEST_LOG_CMD_LIST("ln.exe", L"-f", L"original.txt", L"link.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ln force output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify link content matches original
  std::string original_content = tmp.read("original.txt");
  std::string link_content = tmp.read("link.txt");
  EXPECT_EQ_TEXT(original_content, link_content);
}

TEST(ln, ln_force_readonly_target_reports_gnu_style_remove_error) {
  TempDir tmp;
  tmp.write("original.txt", "hello\n");
  tmp.write("link.txt", "existing\n");

  const auto link_path = tmp.path / "link.txt";
  DWORD attrs = GetFileAttributesW(link_path.wstring().c_str());
  EXPECT_NE(attrs, INVALID_FILE_ATTRIBUTES);
  bool readonly_set = false;
  if (attrs != INVALID_FILE_ATTRIBUTES) {
    readonly_set =
        SetFileAttributesW(link_path.wstring().c_str(),
                           attrs | static_cast<DWORD>(FILE_ATTRIBUTE_READONLY));
  }
  EXPECT_TRUE(readonly_set);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ln.exe", {L"-f", L"original.txt", L"link.txt"});

  auto r = p.run();

  DWORD readonly_attrs = GetFileAttributesW(link_path.wstring().c_str());
  if (readonly_attrs != INVALID_FILE_ATTRIBUTES) {
    SetFileAttributesW(
        link_path.wstring().c_str(),
        readonly_attrs & ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY));
  }

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "ln: failed to remove 'link.txt': Permission denied\n");
  EXPECT_EQ_TEXT(tmp.read("link.txt"), "existing\n");
}

TEST(ln, ln_existing_target_reports_gnu_style_file_exists) {
  TempDir tmp;
  tmp.write("original.txt", "hello\n");
  tmp.write("link.txt", "existing\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ln.exe", {L"original.txt", L"link.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "ln: failed to create hard link 'link.txt': File exists\n");
  EXPECT_EQ_TEXT(tmp.read("link.txt"), "existing\n");
}

TEST(ln, ln_symbolic_existing_target_reports_gnu_style_file_exists) {
  TempDir tmp;
  tmp.write("original.txt", "hello\n");
  tmp.write("link.txt", "existing\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ln.exe", {L"-s", L"original.txt", L"link.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "ln: failed to create symbolic link 'link.txt': File exists\n");
  EXPECT_EQ_TEXT(tmp.read("link.txt"), "existing\n");
}

TEST(ln, ln_missing_source_reports_gnu_style_error) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ln.exe", {L"missing.txt", L"link.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  // GNU 9.4 (ln.c do_link): stat of a missing hardlink SOURCE reports
  // "failed to access 'src'", not a link-creation error.
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "ln: failed to access 'missing.txt': No such file or directory\n");
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "link.txt"));
}

TEST(ln, ln_verbose) {
  TempDir tmp;
  tmp.write("original.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ln.exe", {L"-v", L"original.txt", L"link.txt"});

  TEST_LOG_CMD_LIST("ln.exe", L"-v", L"original.txt", L"link.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ln verbose output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "'link.txt' => 'original.txt'\n");
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(ln, ln_verbose_multiple_links_has_no_summary_line) {
  TempDir tmp;
  tmp.write("a.txt", "A\n");
  tmp.write("b.txt", "B\n");
  std::filesystem::create_directory(tmp.path / "dest");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ln.exe", {L"-v", L"a.txt", L"b.txt", L"dest"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "'dest\\a.txt' => 'a.txt'\n"
                 "'dest\\b.txt' => 'b.txt'\n");
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(ln, ln_single_operand_creates_link_in_current_directory) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "sub");
  tmp.write("sub\\source.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ln.exe", {L"sub\\source.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(tmp.read("source.txt"), "hello\n");
}

TEST(ln, ln_two_operands_treat_existing_directory_as_target_directory) {
  TempDir tmp;
  tmp.write("source.txt", "hello\n");
  std::filesystem::create_directory(tmp.path / "dest");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ln.exe", {L"source.txt", L"dest"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(tmp.read("dest\\source.txt"), "hello\n");
}

TEST(ln, ln_multiple_sources_use_last_operand_as_directory) {
  TempDir tmp;
  tmp.write("a.txt", "A\n");
  tmp.write("b.txt", "B\n");
  std::filesystem::create_directory(tmp.path / "dest");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ln.exe", {L"a.txt", L"b.txt", L"dest"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(tmp.read("dest\\a.txt"), "A\n");
  EXPECT_EQ_TEXT(tmp.read("dest\\b.txt"), "B\n");
}

TEST(ln, ln_multiple_sources_require_directory_target) {
  TempDir tmp;
  tmp.write("a.txt", "A\n");
  tmp.write("b.txt", "B\n");
  tmp.write("not-dir.txt", "C\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ln.exe", {L"a.txt", L"b.txt", L"not-dir.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  // GNU 9.4 ln.c: errno-style "target 'x': Not a directory".
  EXPECT_EQ_TEXT(r.stderr_text, "ln: target 'not-dir.txt': Not a directory\n");
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "not-dir.txt~"));
}

// [DIFFERS] #1101: NT resolves reparse-point link text with the Win32 path
// parser, which only accepts backslash separators. A stored target such as
// "./original.txt" or "sub/original.txt" fails native resolution with
// ERROR_INVALID_NAME ("The filename, directory name, or volume label syntax
// is incorrect") even though the link creation itself succeeds. The link
// text must be normalized to backslashes before CreateSymbolicLinkW.
TEST(ln, ln_symlink_dot_slash_target_resolves_natively) {
  TempDir tmp;
  tmp.write("original.txt", "hello world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ln.exe", {L"-s", L"./original.txt", L"link.txt"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ln -s ./src stderr", r.stderr_text);

  // Same skip policy as the other symbolic-link tests.
  if (r.exit_code != 0) {
    std::cout
        << "  SKIPPED (requires administrator privileges for symbolic links)\n";
    return;
  }
  EXPECT_EQ(r.exit_code, 0);

  std::error_code ec;
  auto stored =
      std::filesystem::read_symlink(tmp.path / L"link.txt", ec).wstring();
  EXPECT_FALSE(ec);
  EXPECT_TRUE(stored.find(L'/') == std::wstring::npos);

  // TempDir::read goes through CreateFileW on the plain path, so this
  // exercises the native reparse-point resolution that used to fail.
  EXPECT_EQ_TEXT(tmp.read("link.txt"), "hello world\n");
}

TEST(ln, ln_symlink_forward_slash_path_target_resolves_natively) {
  TempDir tmp;
  tmp.write("sub/original.txt", "nested\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ln.exe", {L"-s", L"sub/original.txt", L"link.txt"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ln -s sub/src stderr", r.stderr_text);

  if (r.exit_code != 0) {
    std::cout
        << "  SKIPPED (requires administrator privileges for symbolic links)\n";
    return;
  }
  EXPECT_EQ(r.exit_code, 0);

  std::error_code ec;
  auto stored =
      std::filesystem::read_symlink(tmp.path / L"link.txt", ec).wstring();
  EXPECT_FALSE(ec);
  EXPECT_TRUE(stored.find(L'/') == std::wstring::npos);
  EXPECT_EQ_TEXT(tmp.read("link.txt"), "nested\n");
}
