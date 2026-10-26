// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(mkdir, mkdir_basic) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkdir.exe", {L"test_dir"});

  TEST_LOG_CMD_LIST("mkdir.exe", L"test_dir");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("mkdir.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify the directory was created
  bool dir_exists = std::filesystem::exists(tmp.path / "test_dir") &&
                    std::filesystem::is_directory(tmp.path / "test_dir");
  EXPECT_TRUE(dir_exists);
}

TEST(mkdir, mkdir_p_parents) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkdir.exe", {L"-p", L"dir1/dir2/dir3"});

  TEST_LOG_CMD_LIST("mkdir.exe", L"-p", L"dir1/dir2/dir3");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("mkdir.exe -p output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify the nested directories were created
  bool dir_exists =
      std::filesystem::exists(tmp.path / "dir1" / "dir2" / "dir3") &&
      std::filesystem::is_directory(tmp.path / "dir1" / "dir2" / "dir3");
  EXPECT_TRUE(dir_exists);
}

TEST(mkdir, mkdir_p_accepts_utf8_path_segments) {
  TempDir tmp;

  const std::wstring first = L"\u6d4b\u8bd5";
  const std::wstring second = L"\u76ee\u5f55";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkdir.exe", {L"-p", first + L"/" + second});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_TRUE(std::filesystem::is_directory(tmp.path / first / second));
}

TEST(mkdir, mkdir_verbose_parents_reports_each_created_directory) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkdir.exe", {L"-v", L"-p", L"newv/a/b"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "mkdir: created directory 'newv'\n"
                 "mkdir: created directory 'newv/a'\n"
                 "mkdir: created directory 'newv/a/b'\n");
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_TRUE(std::filesystem::is_directory(tmp.path / "newv" / "a" / "b"));
}
TEST(mkdir, mkdir_multiple) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkdir.exe", {L"dir1", L"dir2", L"dir3"});

  TEST_LOG_CMD_LIST("mkdir.exe", L"dir1", L"dir2", L"dir3");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("mkdir.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify all directories were created
  bool dir1_exists = std::filesystem::exists(tmp.path / "dir1") &&
                     std::filesystem::is_directory(tmp.path / "dir1");
  bool dir2_exists = std::filesystem::exists(tmp.path / "dir2") &&
                     std::filesystem::is_directory(tmp.path / "dir2");
  bool dir3_exists = std::filesystem::exists(tmp.path / "dir3") &&
                     std::filesystem::is_directory(tmp.path / "dir3");
  EXPECT_TRUE(dir1_exists);
  EXPECT_TRUE(dir2_exists);
  EXPECT_TRUE(dir3_exists);
}

TEST(mkdir, mkdir_mode_readonly_sets_windows_attribute) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkdir.exe", {L"-m", L"555", L"readonly"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);

  auto dir = tmp.path / "readonly";
  DWORD attrs = GetFileAttributesW(dir.wstring().c_str());
  EXPECT_NE(attrs, INVALID_FILE_ATTRIBUTES);
  EXPECT_TRUE((attrs & FILE_ATTRIBUTE_READONLY) != 0);

  SetFileAttributesW(dir.wstring().c_str(),
                     attrs & ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY));
}

TEST(mkdir, mkdir_parents_mode_applies_to_leaf_only) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkdir.exe", {L"-p", L"-m", L"555", L"parent/leaf"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);

  auto parent = tmp.path / "parent";
  auto leaf = parent / "leaf";
  DWORD parent_attrs = GetFileAttributesW(parent.wstring().c_str());
  DWORD leaf_attrs = GetFileAttributesW(leaf.wstring().c_str());
  EXPECT_NE(parent_attrs, INVALID_FILE_ATTRIBUTES);
  EXPECT_NE(leaf_attrs, INVALID_FILE_ATTRIBUTES);
  EXPECT_FALSE((parent_attrs & FILE_ATTRIBUTE_READONLY) != 0);
  EXPECT_TRUE((leaf_attrs & FILE_ATTRIBUTE_READONLY) != 0);

  SetFileAttributesW(leaf.wstring().c_str(),
                     leaf_attrs & ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY));
}

TEST(mkdir, mkdir_accepts_context_placeholder) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkdir.exe", {L"-Z", L"ctx_dir"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::is_directory(tmp.path / "ctx_dir"));
}

TEST(mkdir, mkdir_missing_operand_reports_help_hint) {
  Pipeline p;
  p.add(L"mkdir.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "mkdir: missing operand\n"
                 "Try 'mkdir --help' for more information.\n");
}

// ===== #1233 regression: POSIX drive-dialect operands =====
//
// `mkdir -p /e/Data` from a C: cwd used to hand the raw "/e/..." text to
// CreateDirectoryW. Win32 resolves a leading "/e" against the CURRENT drive,
// so the command really created a shadow "C:\e" tree - the only audited
// misparse that writes to the wrong place. Every operand must fold through
// the shared normalize_api_operand boundary before a write lands. The probes
// below synthesize the dialect spelling from the TempDir's own drive, so the
// assertions work on any host regardless of which drives exist.
namespace mkdir_dialect_detail {

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

// Where the BUGGY code put the probe leaf instead: it rooted the operand's
// first component ("<drive>") on the cwd's drive, so from cwd "X:" the probe
// appeared at "X:\<operand-drive>\<tmp-rest>". The leaf name is unique per
// TempDir, so the negative assertion never blames pre-existing machine state.
auto shadow_leaf(const TempDir &tmp, wchar_t cwd_drive) -> std::wstring {
  const wchar_t operand_drive = drive_letter(tmp.path);
  std::wstring leaf(1, cwd_drive);
  leaf += L":\\";
  leaf += operand_drive;
  leaf += L"\\";
  leaf += tmp.path.wstring().substr(3);  // past "X:\"
  return leaf;
}

// UTF-8 form of the operand for exact stdout comparisons.
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

}  // namespace mkdir_dialect_detail

TEST(mkdir, mkdir_p_drive_dialect_operand_lands_on_operand_drive) {
  TempDir tmp;
  const wchar_t tmp_drive = mkdir_dialect_detail::drive_letter(tmp.path);
  const std::wstring operand = mkdir_dialect_detail::slash_drive_form(
      tmp.path / L"d1233" / L"probe1233");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkdir.exe", {L"-p", operand});

  auto r = p.run();

  TEST_LOG_CMD_LIST("mkdir.exe", L"-p", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("mkdir stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  // Landing: under the TempDir, on the operand's drive.
  EXPECT_TRUE(
      std::filesystem::is_directory(tmp.path / L"d1233" / L"probe1233"));
  // Shadow negative (#1233): the unfolded operand rooted the leaf on the cwd
  // drive ("<cwd>:\d1233\...") - that leaf must never exist.
  EXPECT_FALSE(std::filesystem::exists(
      std::filesystem::path(mkdir_dialect_detail::shadow_leaf(tmp, tmp_drive))));
}

TEST(mkdir, mkdir_drive_dialect_operand_without_parents_lands_on_operand_drive) {
  TempDir tmp;
  const wchar_t tmp_drive = mkdir_dialect_detail::drive_letter(tmp.path);
  const std::wstring operand =
      mkdir_dialect_detail::slash_drive_form(tmp.path / L"probe1233");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkdir.exe", {operand});

  auto r = p.run();

  TEST_LOG_CMD_LIST("mkdir.exe", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("mkdir stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_TRUE(std::filesystem::is_directory(tmp.path / L"probe1233"));
  EXPECT_FALSE(std::filesystem::exists(
      std::filesystem::path(mkdir_dialect_detail::shadow_leaf(tmp, tmp_drive))));
}

TEST(mkdir, mkdir_p_drive_dialect_cwd_on_other_drive_lands_on_operand_drive) {
  TempDir tmp;
  const wchar_t other = mkdir_dialect_detail::other_fixed_drive(
      mkdir_dialect_detail::drive_letter(tmp.path));
  if (other == L'\0') {
    std::cout << "  SKIPPED (no second fixed drive to host the cwd)\n";
    return;
  }
  const std::wstring operand =
      mkdir_dialect_detail::slash_drive_form(tmp.path / L"probe1233");

  Pipeline p;
  p.set_cwd(std::wstring(1, other) + L":\\");
  p.add(L"mkdir.exe", {L"-p", operand});

  auto r = p.run();

  TEST_LOG_CMD_LIST("mkdir.exe", L"-p", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("mkdir stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_TRUE(std::filesystem::is_directory(tmp.path / L"probe1233"));
  // The audit's exact shadow shape: cwd on one drive, operand on another -
  // the unfolded spelling created "<cwd>:\e\Data\..." on the cwd drive.
  EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(
      mkdir_dialect_detail::shadow_leaf(tmp, other))));
}

TEST(mkdir, mkdir_p_bare_drive_root_operand_existing_is_success_noop) {
  TempDir tmp;
  const wchar_t drive = mkdir_dialect_detail::drive_letter(tmp.path);
  const std::wstring operand = std::wstring(L"/") + drive;
  // What the buggy code did here: -p creation of "<drive>:\<drive>" - a real
  // folder at the drive root. Only assert when it does not pre-exist.
  const std::wstring shadow_root = std::wstring(1, drive) + L":\\" + drive;
  const bool shadow_pre_existed =
      std::filesystem::exists(std::filesystem::path(shadow_root));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkdir.exe", {L"-p", operand});

  auto r = p.run();

  TEST_LOG_CMD_LIST("mkdir.exe", L"-p", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("mkdir stdout", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_TRUE(r.stdout_text.empty());  // [GNU] -p on an existing root: silent
  if (!shadow_pre_existed) {
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(shadow_root)));
  }
}

TEST(mkdir, mkdir_p_drive_dialect_verbose_reports_operand_form) {
  TempDir tmp;
  const std::wstring operand =
      mkdir_dialect_detail::slash_drive_form(tmp.path / L"vdeep1233");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkdir.exe", {L"-v", L"-p", operand});

  auto r = p.run();

  TEST_LOG_CMD_LIST("mkdir.exe", L"-v", L"-p", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("mkdir stdout", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // MSYS parity: only the created leaf is reported, in the operand spelling
  // (the drive root and the TempDir chain already exist).
  EXPECT_EQ_TEXT(r.stdout_text,
                 "mkdir: created directory '" +
                     mkdir_dialect_detail::utf8(operand) + "'\n");
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_TRUE(std::filesystem::is_directory(tmp.path / L"vdeep1233"));
}

TEST(mkdir, mkdir_mode_1330_symbolic) {
  auto run_mkdir = [](const wchar_t* mode, const wchar_t* name) {
    TempDir tmp;
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"mkdir.exe", {L"-m", mode, name});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    auto dir = tmp.path / name;
    DWORD attrs = GetFileAttributesW(dir.wstring().c_str());
    EXPECT_NE(attrs, INVALID_FILE_ATTRIBUTES);
    const bool readonly = (attrs & FILE_ATTRIBUTE_READONLY) != 0;
    if (readonly) {  // keep the temp tree cleanable
      SetFileAttributesW(
          dir.wstring().c_str(),
          attrs & ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY));
    }
    return readonly;
  };

  // GNU 9.4 values against the -m base 0777 (the issue's claim of
  // u+rwx -> 0700 is wrong; it is 0777).  Only the write bits are
  // observable on Windows, via the read-only attribute (#1330).
  EXPECT_FALSE(run_mkdir(L"u+rwx", L"a"));      // 0777
  EXPECT_FALSE(run_mkdir(L"u=rw,go=r", L"b"));  // 0644
  EXPECT_FALSE(run_mkdir(L"go-w", L"c"));       // 0755
  EXPECT_FALSE(run_mkdir(L"u=rwx,go=", L"d"));  // 0700
  EXPECT_TRUE(run_mkdir(L"a-w", L"e"));         // 0555
}
