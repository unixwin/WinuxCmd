// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

// ===== #1237 regression: POSIX drive-dialect operands =====
//
// namei probed every component of the raw operand text with
// GetFileAttributesW, and Win32 resolves a leading "/e" against the CURRENT
// drive: all dialect forms ("/e/Data", "/cygdrive/e/Data", "/mnt/e/Data")
// reported a broken chain for paths that exist on the named drive. The
// operand must fold through the shared make_api_path_operand boundary and
// the walk must run on the folded native spelling. The probes below
// synthesize the dialect spelling from the TempDir's own drive, so the
// assertions work on any host regardless of which drives exist.
namespace namei_dialect_detail {

// Lowercase drive letter of an absolute path ("C:\x" -> L'c').
auto drive_letter(const std::filesystem::path &path) -> wchar_t {
  wchar_t drive = path.wstring()[0];
  if (drive >= L'A' && drive <= L'Z') {
    drive = static_cast<wchar_t>(drive - L'A' + L'a');
  }
  return drive;
}

// "C:\Users\...\wct" -> "/c/Users/.../wct" (MSYS drive spelling).
auto slash_drive_form(const std::filesystem::path &path) -> std::wstring {
  std::wstring text = path.wstring();
  text[0] = drive_letter(path);
  text.erase(1, 1);  // drop the ':' from "c:\..."
  for (wchar_t &ch : text) {
    if (ch == L'\\') ch = L'/';
  }
  return L"/" + text;
}

// "/cygdrive/c/Users/..." spelling.
auto cygdrive_form(const std::filesystem::path &path) -> std::wstring {
  return L"/cygdrive" + slash_drive_form(path);
}

// "/mnt/c/Users/..." spelling.
auto mnt_form(const std::filesystem::path &path) -> std::wstring {
  return L"/mnt" + slash_drive_form(path);
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

// Keep the shell-namespace mapping out of the way: the dialect fold must land
// on the operand drive with the shell root unconfigured too.
void clear_shell_root_env(Pipeline &pipeline) {
  for (const auto *name :
       {L"__RUBASH_SHELL_ROOT", L"NIU_ROOT", L"WINUXSH_ROOT", L"RUBASH_ROOT"}) {
    pipeline.set_env(name, L"");
  }
}

}  // namespace namei_dialect_detail

TEST(namei, namei_drive_dialect_resolves_every_component) {
  TempDir tmp;
  tmp.write("wbdir/wbfile.txt", "x");
  const std::wstring operand =
      namei_dialect_detail::slash_drive_form(tmp.path / L"wbdir" /
                                             L"wbfile.txt");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  namei_dialect_detail::clear_shell_root_env(p);
  p.add(L"namei.exe", {operand});
  auto r = p.run();

  TEST_LOG_CMD_LIST("namei.exe", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("namei drive dialect stdout", r.stdout_text);

  // The full chain resolves: header echoes the operand, every component
  // names a real entry, and no step reports a broken chain.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "/wbdir/wbfile.txt");
  EXPECT_CONTAINS(r.stdout_text, "d wbdir");
  EXPECT_CONTAINS(r.stdout_text, "f wbfile.txt");
  EXPECT_NOT_CONTAINS(r.stdout_text, "(No such file or directory)");
}

TEST(namei, namei_bare_drive_root_resolves) {
  TempDir tmp;
  const wchar_t drive = namei_dialect_detail::drive_letter(tmp.path);
  const std::wstring operand = std::wstring(L"/") + drive;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  namei_dialect_detail::clear_shell_root_env(p);
  p.add(L"namei.exe", {operand});
  auto r = p.run();

  TEST_LOG_CMD_LIST("namei.exe", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("namei bare drive root stdout", r.stdout_text);

  // "/x" is the drive root: a directory step, never a broken chain.
  EXPECT_EQ(r.exit_code, 0);
  const std::string root_line =
      std::string("d ") +
      static_cast<char>(std::toupper(static_cast<unsigned char>(
          static_cast<char>(drive)))) +
      ":\\";
  EXPECT_CONTAINS(r.stdout_text, root_line);
  EXPECT_NOT_CONTAINS(r.stdout_text, "(No such file or directory)");
}

TEST(namei, namei_cygdrive_and_mnt_forms_resolve) {
  TempDir tmp;
  tmp.write("wbdir/wbfile.txt", "x");

  for (const std::wstring operand :
       {namei_dialect_detail::cygdrive_form(tmp.path / L"wbdir" /
                                            L"wbfile.txt"),
        namei_dialect_detail::mnt_form(tmp.path / L"wbdir" / L"wbfile.txt")}) {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    namei_dialect_detail::clear_shell_root_env(p);
    p.add(L"namei.exe", {operand});
    auto r = p.run();

    TEST_LOG_CMD_LIST("namei.exe", operand);
    TEST_LOG_EXIT_CODE(r);
    TEST_LOG("namei cygdrive/mnt stdout", r.stdout_text);

    // The prefix names fold away: no step may report them as missing.
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_CONTAINS(r.stdout_text, "f wbfile.txt");
    EXPECT_NOT_CONTAINS(r.stdout_text, "(No such file or directory)");
  }
}

TEST(namei, namei_drive_dialect_from_foreign_cwd_resolves) {
  TempDir tmp;
  tmp.write("wbdir/wbfile.txt", "x");
  const wchar_t other = namei_dialect_detail::other_fixed_drive(
      namei_dialect_detail::drive_letter(tmp.path));
  if (other == L'\0') {
    std::cout << "  SKIPPED (no second fixed drive to host the cwd)\n";
    return;
  }
  const std::wstring operand =
      namei_dialect_detail::slash_drive_form(tmp.path / L"wbdir" /
                                             L"wbfile.txt");

  Pipeline p;
  p.set_cwd(std::wstring(1, other) + L":\\");
  namei_dialect_detail::clear_shell_root_env(p);
  p.add(L"namei.exe", {operand});
  auto r = p.run();

  TEST_LOG_CMD_LIST("namei.exe", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("namei foreign-cwd dialect stdout", r.stdout_text);

  // The audit's exact shadow shape: cwd on one drive, operand on another -
  // the unfolded spelling rooted the first component on the cwd drive.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "f wbfile.txt");
  EXPECT_NOT_CONTAINS(r.stdout_text, "(No such file or directory)");
}

TEST(namei, namei_missing_component_still_reports_broken_chain) {
  TempDir tmp;
  const std::wstring operand =
      namei_dialect_detail::slash_drive_form(tmp.path / L"absent1237");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  namei_dialect_detail::clear_shell_root_env(p);
  p.add(L"namei.exe", {operand});
  auto r = p.run();

  TEST_LOG_CMD_LIST("namei.exe", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("namei missing component stdout", r.stdout_text);

  // A genuinely missing component keeps the GNU broken-chain report, naming
  // the component that does not exist.
  EXPECT_CONTAINS(r.stdout_text, "absent1237");
  EXPECT_CONTAINS(r.stdout_text, "(No such file or directory)");
}

TEST(namei, namei_native_form_still_resolves) {
  TempDir tmp;
  tmp.write("wbdir/wbfile.txt", "x");
  const std::wstring operand = tmp.wpath() + L"\\wbdir\\wbfile.txt";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  namei_dialect_detail::clear_shell_root_env(p);
  p.add(L"namei.exe", {operand});
  auto r = p.run();

  TEST_LOG_CMD_LIST("namei.exe", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("namei native form stdout", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "d wbdir");
  EXPECT_CONTAINS(r.stdout_text, "f wbfile.txt");
  EXPECT_NOT_CONTAINS(r.stdout_text, "(No such file or directory)");
}

// ===== util-linux namei options (#1127) =====

// -m/--modes replaces the type glyph with a 10-character mode string.
TEST(namei, namei_modes_prints_mode_string) {
  TempDir tmp;
  tmp.write("wbdir/wbfile.txt", "x");
  const std::wstring operand = tmp.wpath() + L"\\wbdir\\wbfile.txt";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  namei_dialect_detail::clear_shell_root_env(p);
  p.add(L"namei.exe", {L"-m", operand});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "drwx");
  EXPECT_CONTAINS(r.stdout_text, "wbfile.txt");
  EXPECT_NOT_CONTAINS(r.stdout_text, "(No such file or directory)");
}

// -o/--owners adds owner and group columns.
TEST(namei, namei_owners_prints_columns) {
  TempDir tmp;
  tmp.write("wbdir/wbfile.txt", "x");
  const std::wstring operand = tmp.wpath() + L"\\wbdir\\wbfile.txt";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  namei_dialect_detail::clear_shell_root_env(p);
  p.add(L"namei.exe", {L"-o", operand});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "wbdir");
  EXPECT_CONTAINS(r.stdout_text, "wbfile.txt");
  EXPECT_NOT_CONTAINS(r.stdout_text, "(No such file or directory)");
}

// -x/--mountpoints marks mount points (volume roots on Windows) 'D'.
TEST(namei, namei_mountpoints_marks_root) {
  TempDir tmp;
  tmp.write("wbdir/wbfile.txt", "x");
  const std::wstring operand = tmp.wpath() + L"\\wbdir\\wbfile.txt";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  namei_dialect_detail::clear_shell_root_env(p);
  p.add(L"namei.exe", {L"-x", operand});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  const std::string root_line =
      std::string("D ") +
      static_cast<char>(std::toupper(static_cast<unsigned char>(
          static_cast<char>(namei_dialect_detail::drive_letter(tmp.path))))) +
      ":\\";
  EXPECT_CONTAINS(r.stdout_text, root_line);
}

// -Z/--context appends the (unavailable on Windows) context marker.
TEST(namei, namei_context_marker) {
  TempDir tmp;
  tmp.write("wbdir/wbfile.txt", "x");
  const std::wstring operand = tmp.wpath() + L"\\wbdir\\wbfile.txt";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  namei_dialect_detail::clear_shell_root_env(p);
  p.add(L"namei.exe", {L"-Z", operand});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "?");
  EXPECT_CONTAINS(r.stdout_text, "wbfile.txt");
}

// -l/--long combines modes, owners and vertical output.
TEST(namei, namei_long_format) {
  TempDir tmp;
  tmp.write("wbdir/wbfile.txt", "x");
  const std::wstring operand = tmp.wpath() + L"\\wbdir\\wbfile.txt";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  namei_dialect_detail::clear_shell_root_env(p);
  p.add(L"namei.exe", {L"-l", operand});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "drwx");
  EXPECT_CONTAINS(r.stdout_text, "wbfile.txt");
}

// A missing component now exits 1, matching util-linux namei
// (print_namei failure propagates to EXIT_FAILURE).
TEST(namei, namei_missing_component_exits_one) {
  TempDir tmp;
  tmp.write("wbdir/wbfile.txt", "x");
  const std::wstring operand = tmp.wpath() + L"\\wbdir\\absent1237";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  namei_dialect_detail::clear_shell_root_env(p);
  p.add(L"namei.exe", {operand});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_CONTAINS(r.stdout_text, "(No such file or directory)");
}
