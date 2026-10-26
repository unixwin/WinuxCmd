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

// ===== #1234 regression: POSIX drive-dialect operands =====
//
// ln handed raw "/e/..." operand text to FindFirstFileW, GetFileAttributesW
// and CreateHardLinkW / CreateSymbolicLinkW. Win32 resolves a leading "/e"
// against the CURRENT drive, so from a C: cwd the hard-link source probe
// looked at "C:\e\..." (reporting a false "failed to access") and a
// privileged symlink create really wrote "C:\e\Data\link". Every operand is
// now folded through the shared normalize_api_operand boundary before a
// Win32 call. The probes synthesize the dialect spelling from the TempDir's
// own drive, so they work on any host regardless of which drives exist.
namespace ln_dialect_detail {

// Lowercase drive letter of an absolute path ("C:\x" -> L'c').
auto drive_letter(const std::filesystem::path &path) -> wchar_t {
  wchar_t drive = path.wstring()[0];
  if (drive >= L'A' && drive <= L'Z') {
    drive = static_cast<wchar_t>(drive - L'A' + L'a');
  }
  return drive;
}

// "C:\Users\...\wct123" -> "/c/Users/.../wct123" (MSYS drive spelling: the
// colon is dropped, only the letter stays).
auto slash_drive_form(const std::filesystem::path &path) -> std::wstring {
  std::wstring text = path.wstring();
  text[0] = drive_letter(path);
  text.erase(1, 1);  // drop the ':' from "c:\..."
  for (wchar_t &ch : text) {
    if (ch == L'\\') ch = L'/';
  }
  return L"/" + text;
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

// Where the BUGGY code put the probe leaf: rooted on the cwd drive as
// "X:\<operand-drive>\<tmp-rest>\<name>". Unique per TempDir, so the
// negative assertion never blames pre-existing machine state.
auto shadow_leaf(const TempDir &tmp, wchar_t cwd_drive,
                 const std::wstring &name) -> std::wstring {
  const wchar_t operand_drive = drive_letter(tmp.path);
  std::wstring leaf(1, cwd_drive);
  leaf += L":\\";
  leaf += operand_drive;
  leaf += L"\\";
  leaf += tmp.path.wstring().substr(3);  // past "X:\"
  leaf += L"\\";
  leaf += name;
  return leaf;
}

// Symbolic links need a privilege this host may not grant; the
// "Operation not permitted" text is the ln mapping of
// ERROR_PRIVILEGE_NOT_HELD. Any other failure is a real regression.
bool symlink_privilege_denied(const CommandResult &r) {
  return r.exit_code != 0 &&
         r.stderr_text.find("Operation not permitted") != std::string::npos;
}

// UTF-8 form of the operand for exact stderr/stdout comparisons.
auto utf8(const std::wstring &text) -> std::string {
  if (text.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                       static_cast<int>(text.size()), nullptr,
                                       0, nullptr, nullptr);
  std::string out(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                      out.data(), size, nullptr, nullptr);
  return out;
}

}  // namespace ln_dialect_detail

TEST(ln, ln_hardlink_drive_dialect_operands_link_on_operand_drive) {
  TempDir tmp;
  tmp.write("probe1233", "payload");
  const wchar_t tmp_drive = ln_dialect_detail::drive_letter(tmp.path);
  const std::wstring source =
      ln_dialect_detail::slash_drive_form(tmp.path / L"probe1233");
  const std::wstring target =
      ln_dialect_detail::slash_drive_form(tmp.path / L"hard1234");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ln.exe", {source, target});

  auto r = p.run();

  TEST_LOG_CMD_LIST("ln.exe", source, target);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ln stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  // Landing: the link lives next to its source, on the operand's drive.
  EXPECT_EQ_TEXT(tmp.read("hard1234"), "payload");
  EXPECT_TRUE(std::filesystem::exists(tmp.path / L"probe1233"));
  // Shadow negative (#1234): the unfolded operands probed and wrote
  // "<cwd>:\c\..." - neither leaf may exist.
  EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(
      ln_dialect_detail::shadow_leaf(tmp, tmp_drive, L"probe1233"))));
  EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(
      ln_dialect_detail::shadow_leaf(tmp, tmp_drive, L"hard1234"))));
}

TEST(ln, ln_hardlink_drive_dialect_cwd_on_other_drive_lands_on_operand_drive) {
  TempDir tmp;
  tmp.write("probe1233", "payload");
  const wchar_t other = ln_dialect_detail::other_fixed_drive(
      ln_dialect_detail::drive_letter(tmp.path));
  if (other == L'\0') {
    std::cout << "  SKIPPED (no second fixed drive to host the cwd)\n";
    return;
  }
  const std::wstring source =
      ln_dialect_detail::slash_drive_form(tmp.path / L"probe1233");
  const std::wstring target =
      ln_dialect_detail::slash_drive_form(tmp.path / L"hard1234");

  Pipeline p;
  p.set_cwd(std::wstring(1, other) + L":\\");
  p.add(L"ln.exe", {source, target});

  auto r = p.run();

  TEST_LOG_CMD_LIST("ln.exe", source, target);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ln stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(tmp.read("hard1234"), "payload");
  // The audit's exact shadow shape: cwd drive roots the unfolded operand.
  EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(
      ln_dialect_detail::shadow_leaf(tmp, other, L"hard1234"))));
}

TEST(ln, ln_drive_dialect_existing_directory_target_joins_operands) {
  TempDir tmp;
  tmp.write("probe1233", "payload");
  std::filesystem::create_directory(tmp.path / L"sub");
  const wchar_t tmp_drive = ln_dialect_detail::drive_letter(tmp.path);
  const std::wstring source =
      ln_dialect_detail::slash_drive_form(tmp.path / L"probe1233");
  const std::wstring target_dir =
      ln_dialect_detail::slash_drive_form(tmp.path / L"sub");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ln.exe", {source, target_dir});

  auto r = p.run();

  TEST_LOG_CMD_LIST("ln.exe", source, target_dir);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ln stderr", r.stderr_text);

  // The 2-operand directory detection must answer for the operand's drive,
  // not "<cwd>:\c\..."; GNU then links INSIDE the directory.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(tmp.read("sub\\probe1233"), "payload");
  EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(
      ln_dialect_detail::shadow_leaf(tmp, tmp_drive, L"probe1233"))));
}

TEST(ln, ln_t_drive_dialect_target_directory_creates_links_inside) {
  TempDir tmp;
  tmp.write("probe1233", "payload");
  std::filesystem::create_directory(tmp.path / L"sub");
  const std::wstring target_dir =
      ln_dialect_detail::slash_drive_form(tmp.path / L"sub");
  const std::wstring source =
      ln_dialect_detail::slash_drive_form(tmp.path / L"probe1233");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ln.exe", {L"-t", target_dir, source});

  auto r = p.run();

  TEST_LOG_CMD_LIST("ln.exe", L"-t", target_dir, source);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ln stderr", r.stderr_text);

  // The -t directory probe must fold the dialect too: unfolded, ln failed
  // with "failed to access 'target-dir'" style errors because it answered
  // for "<cwd>:\c\...". The link name joins onto the real directory.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(tmp.read("sub\\probe1233"), "payload");
}

TEST(ln, ln_symlink_drive_dialect_lands_on_operand_drive) {
  TempDir tmp;
  tmp.write("probe1233", "payload");
  const wchar_t tmp_drive = ln_dialect_detail::drive_letter(tmp.path);
  const std::wstring source =
      ln_dialect_detail::slash_drive_form(tmp.path / L"probe1233");
  const std::wstring target =
      ln_dialect_detail::slash_drive_form(tmp.path / L"link1234");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ln.exe", {L"-s", source, target});

  auto r = p.run();

  TEST_LOG_CMD_LIST("ln.exe", L"-s", source, target);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ln symlink stderr", r.stderr_text);

  // Same privilege skip policy as the other symbolic-link tests, but keyed
  // on the privilege error text so a dialect misparse still fails the test.
  if (ln_dialect_detail::symlink_privilege_denied(r)) {
    std::cout
        << "  SKIPPED (requires administrator privileges for symbolic links)\n";
    return;
  }
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(tmp.read("link1234"), "payload");
  EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(
      ln_dialect_detail::shadow_leaf(tmp, tmp_drive, L"link1234"))));
}

// The create-stage failure message quotes the operand as typed (MSYS prints
// the spelling the user handed over, never the folded "X:\..." API form):
// a directory source fails inside CreateHardLinkW, and the folded target
// must not leak into that text (#1234).
TEST(ln, ln_drive_dialect_create_failure_message_keeps_operand_spelling) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / L"sub1233");
  const std::wstring source =
      ln_dialect_detail::slash_drive_form(tmp.path / L"sub1233");
  const std::wstring target =
      ln_dialect_detail::slash_drive_form(tmp.path / L"hard1234");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ln.exe", {source, target});

  auto r = p.run();

  TEST_LOG_CMD_LIST("ln.exe", source, target);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ln dir-hardlink stderr", r.stderr_text);

  // Hard-linking a directory fails on NTFS at the create stage; the source
  // probe must have resolved the dialect source (no "failed to access").
  EXPECT_NOT_CONTAINS(r.stderr_text, "failed to access");
  ASSERT_NE(r.exit_code, 0);
  // Message quotes the operand spelling, not the folded "X:\..." form.
  EXPECT_CONTAINS(r.stderr_text,
                  "failed to create hard link '" +
                      ln_dialect_detail::utf8(target) + "'");
  EXPECT_NOT_CONTAINS(r.stderr_text, ":\\");
}

// Same invariant for the -f removal path: a force-remove that cannot
// complete (non-empty directory target) must quote the operand spelling,
// never the folded "X:\..." API form (#1234 review nit).
TEST(ln, ln_fT_drive_dialect_remove_failure_message_keeps_operand_spelling) {
  TempDir tmp;
  tmp.write("probe1233", "payload");
  // The forced-removal victim: a non-empty directory, which RemoveDirectoryW
  // refuses, so the failure message is produced deterministically.
  std::filesystem::create_directory(tmp.path / L"nonempty1234");
  tmp.write("nonempty1234\\keeper", "anchor");
  const std::wstring source =
      ln_dialect_detail::slash_drive_form(tmp.path / L"probe1233");
  const std::wstring target =
      ln_dialect_detail::slash_drive_form(tmp.path / L"nonempty1234");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ln.exe", {L"-fT", source, target});

  auto r = p.run();

  TEST_LOG_CMD_LIST("ln.exe", L"-fT", source, target);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ln -fT stderr", r.stderr_text);

  // The removal failed at the OS level and ln reported it...
  ASSERT_NE(r.exit_code, 0);
  EXPECT_CONTAINS(r.stderr_text, "failed to remove");
  // ...quoting the operand spelling, not the folded form.
  EXPECT_CONTAINS(r.stderr_text,
                  "failed to remove '" + ln_dialect_detail::utf8(target) + "'");
  EXPECT_NOT_CONTAINS(r.stderr_text, ":\\");
  // The non-empty directory survived (nothing was force-removed).
  EXPECT_TRUE(std::filesystem::is_directory(tmp.path / L"nonempty1234"));
}

// [GNU] #1367: a dangling symlink destination EXISTS (lstat semantics):
// the plain form fails with "File exists" and leaves the link untouched.
TEST(ln, issue1367_dangling_destination_counts_as_existing) {
  TempDir tmp;
  tmp.write("t", "tee\n");
  std::error_code ec;
  std::filesystem::create_symlink("missing", tmp.path / "d", ec);
  if (ec) {
    std::cout << "  SKIPPED (requires administrator privileges for symbolic "
                 "links)\n";
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ln.exe", {L"-s", L"t", L"d"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("failed to create symbolic link 'd': "
                                 "File exists") != std::string::npos);
  EXPECT_EQ(std::filesystem::read_symlink(tmp.path / "d", ec),
            std::filesystem::path("missing"));
}

// [GNU] #1367: -i on a dangling destination prompts on stderr and honours
// the answer; declining is an error (GNU exits 1).
TEST(ln, issue1367_dangling_destination_interactive_prompts) {
  TempDir tmp;
  tmp.write("t", "tee\n");
  std::error_code ec;
  std::filesystem::create_symlink("missing", tmp.path / "d", ec);
  if (ec) {
    std::cout << "  SKIPPED (requires administrator privileges for symbolic "
                 "links)\n";
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("n\n");
  p.add(L"ln.exe", {L"-s", L"-i", L"t", L"d"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("replace 'd'?") != std::string::npos);
  EXPECT_EQ(std::filesystem::read_symlink(tmp.path / "d", ec),
            std::filesystem::path("missing"));

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.set_stdin("y\n");
  p2.add(L"ln.exe", {L"-s", L"-i", L"t", L"d"});
  auto r2 = p2.run();
  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_EQ(std::filesystem::read_symlink(tmp.path / "d", ec),
            std::filesystem::path("t"));
}

// [GNU] #1367: -b backs a dangling destination up (the backup keeps the old
// link text); -f replaces it.
TEST(ln, issue1367_dangling_destination_backup_and_force) {
  TempDir tmp;
  tmp.write("t", "tee\n");
  std::error_code ec;
  std::filesystem::create_symlink("missing", tmp.path / "d", ec);
  if (ec) {
    std::cout << "  SKIPPED (requires administrator privileges for symbolic "
                 "links)\n";
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ln.exe", {L"-s", L"-b", L"t", L"d"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(std::filesystem::read_symlink(tmp.path / "d", ec),
            std::filesystem::path("t"));
  EXPECT_EQ(std::filesystem::read_symlink(tmp.path / "d~", ec),
            std::filesystem::path("missing"));

  std::filesystem::create_symlink("missing", tmp.path / "e", ec);
  ASSERT_FALSE(ec);
  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"ln.exe", {L"-sf", L"t", L"e"});
  auto r2 = p2.run();
  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_EQ(std::filesystem::read_symlink(tmp.path / "e", ec),
            std::filesystem::path("t"));
}
