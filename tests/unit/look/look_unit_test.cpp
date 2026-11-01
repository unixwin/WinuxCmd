// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

// ===== #1242 regression: POSIX drive-dialect dictionary operands =====
//
// look opened the dictionary operand as raw text, and CreateFileW resolves a
// leading "/e" against the CURRENT drive: every dialect form
// ("/e/words", "/cygdrive/e/words", "/mnt/e/words") reported "cannot open"
// for a dictionary that exists on the named drive. The operand must fold
// through the shared normalize_api_operand boundary at the open, while the
// error message keeps quoting what the user typed. The probes below
// synthesize the dialect spelling from the TempDir's own drive, so the
// assertions work on any host regardless of which drives exist.
namespace look_dialect_detail {

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

// UTF-8 form of the operand for exact message comparisons.
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

// Sorted single-column dictionary (GNU look requires sorted input).
void write_sorted_dictionary(const TempDir &tmp) {
  tmp.write("wbdict.txt", "alpha\nbravo\ncharlie\ndelta\n");
}

}  // namespace look_dialect_detail

TEST(look, look_drive_dialect_dictionary_opens) {
  TempDir tmp;
  look_dialect_detail::write_sorted_dictionary(tmp);
  const std::wstring operand =
      look_dialect_detail::slash_drive_form(tmp.path / L"wbdict.txt");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  look_dialect_detail::clear_shell_root_env(p);
  p.add(L"look.exe", {L"-f", L"cha", operand});
  auto r = p.run();

  TEST_LOG_CMD_LIST("look.exe", L"-f", L"cha", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("look drive dialect stdout", r.stdout_text);

  // Before #1242 this printed "cannot open '/c/.../wbdict.txt'".
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "charlie\n");
}

TEST(look, look_cygdrive_and_mnt_dictionaries_open) {
  TempDir tmp;
  look_dialect_detail::write_sorted_dictionary(tmp);
  const std::wstring cygdrive_operand =
      look_dialect_detail::cygdrive_form(tmp.path / L"wbdict.txt");
  const std::wstring mnt_operand =
      look_dialect_detail::mnt_form(tmp.path / L"wbdict.txt");

  Pipeline cygdrive;
  cygdrive.set_cwd(tmp.wpath());
  look_dialect_detail::clear_shell_root_env(cygdrive);
  cygdrive.add(L"look.exe", {L"-f", L"bra", cygdrive_operand});
  auto rc = cygdrive.run();

  TEST_LOG_CMD_LIST("look.exe", L"-f", L"bra", cygdrive_operand);
  TEST_LOG_EXIT_CODE(rc);
  TEST_LOG("look cygdrive stdout", rc.stdout_text);
  EXPECT_EQ(rc.exit_code, 0);
  EXPECT_EQ_TEXT(rc.stdout_text, "bravo\n");

  Pipeline mnt;
  mnt.set_cwd(tmp.wpath());
  look_dialect_detail::clear_shell_root_env(mnt);
  mnt.add(L"look.exe", {L"-f", L"alp", mnt_operand});
  auto rm = mnt.run();

  TEST_LOG_CMD_LIST("look.exe", L"-f", L"alp", mnt_operand);
  TEST_LOG_EXIT_CODE(rm);
  TEST_LOG("look mnt stdout", rm.stdout_text);
  EXPECT_EQ(rm.exit_code, 0);
  EXPECT_EQ_TEXT(rm.stdout_text, "alpha\n");
}

TEST(look, look_drive_dialect_from_foreign_cwd_opens) {
  TempDir tmp;
  look_dialect_detail::write_sorted_dictionary(tmp);
  const wchar_t other = look_dialect_detail::other_fixed_drive(
      look_dialect_detail::drive_letter(tmp.path));
  if (other == L'\0') {
    std::cout << "  SKIPPED (no second fixed drive to host the cwd)\n";
    return;
  }
  const std::wstring operand =
      look_dialect_detail::slash_drive_form(tmp.path / L"wbdict.txt");

  Pipeline p;
  p.set_cwd(std::wstring(1, other) + L":\\");
  look_dialect_detail::clear_shell_root_env(p);
  p.add(L"look.exe", {L"-f", L"alp", operand});
  auto r = p.run();

  TEST_LOG_CMD_LIST("look.exe", L"-f", L"alp", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("look foreign-cwd dialect stdout", r.stdout_text);

  // The audit's exact shadow shape: cwd on one drive, operand on another.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "alpha\n");
}

TEST(look, look_missing_dictionary_error_quotes_operand_spelling) {
  TempDir tmp;
  const std::wstring operand =
      look_dialect_detail::slash_drive_form(tmp.path / L"absent1242.txt");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  look_dialect_detail::clear_shell_root_env(p);
  p.add(L"look.exe", {L"-f", L"a", operand});
  auto r = p.run();

  TEST_LOG_CMD_LIST("look.exe", L"-f", L"a", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("look missing dictionary stderr", r.stderr_text);

  // [GNU] util-linux look reports an unopenable dictionary with
  // err(EXIT_FAILURE, "%s", file) (misc-utils/look.c:154-155): the
  // operand spelling as typed, followed by the strerror text -- no
  // "cannot open" wording and no quotes.
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_CONTAINS(r.stderr_text,
                  look_dialect_detail::utf8(operand) +
                      ": No such file or directory");
}

TEST(look, look_native_dictionary_still_opens) {
  TempDir tmp;
  look_dialect_detail::write_sorted_dictionary(tmp);
  const std::wstring operand = tmp.wpath() + L"\\wbdict.txt";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  look_dialect_detail::clear_shell_root_env(p);
  p.add(L"look.exe", {L"bra", operand});
  auto r = p.run();

  TEST_LOG_CMD_LIST("look.exe", L"bra", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("look native form stdout", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "bravo\n");
}

// ===== #1251 regression: a final newline must not synthesize an entry =====
//
// The read loop emitted an empty line for the text after the last '\n', so a
// standard trailing-newline dictionary carried a phantom "" entry at the END
// of the sorted array. "" sorts before every key, but the binary search's
// midpoint can land on the phantom and push lower_bound past the last real
// word: `look -f delt` printed nothing for "…charlie\ndelta\n". Oracle:
// util-linux look 2.39.3 (WSL) prints "delta" for the same input — a final
// newline contributes no entry, while empty lines in the MIDDLE of the
// dictionary stay real entries (matched by an empty key).

TEST(look, look_trailing_newline_dictionary_finds_last_entry) {
  TempDir tmp;
  look_dialect_detail::write_sorted_dictionary(tmp);
  const std::wstring operand = tmp.wpath() + L"\\wbdict.txt";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  look_dialect_detail::clear_shell_root_env(p);
  p.add(L"look.exe", {L"-f", L"delt", operand});
  auto r = p.run();

  TEST_LOG_CMD_LIST("look.exe", L"-f", L"delt", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("look trailing newline stdout", r.stdout_text);

  // Before #1251 the phantom empty line made lower_bound skip past "delta".
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "delta\n");
}

TEST(look, look_trailing_newline_and_no_trailing_newline_agree) {
  TempDir tmp;
  tmp.write("with.txt", "alpha\nbravo\ncharlie\ndelta\n");
  tmp.write("without.txt", "alpha\nbravo\ncharlie\ndelta");
  const std::wstring with_nl = tmp.wpath() + L"\\with.txt";
  const std::wstring no_nl = tmp.wpath() + L"\\without.txt";

  for (const auto &[key, expected] : {std::pair{L"alp", std::string("alpha\n")},
                                      {L"cha", std::string("charlie\n")},
                                      {L"delt", std::string("delta\n")}}) {
    Pipeline a;
    a.set_cwd(tmp.wpath());
    look_dialect_detail::clear_shell_root_env(a);
    a.add(L"look.exe", {L"-f", key, with_nl});
    auto ra = a.run();

    TEST_LOG_CMD_LIST("look.exe", L"-f", key, with_nl);
    TEST_LOG_EXIT_CODE(ra);
    TEST_LOG("look with trailing newline stdout", ra.stdout_text);
    EXPECT_EQ(ra.exit_code, 0);
    EXPECT_EQ_TEXT(ra.stdout_text, expected);

    Pipeline b;
    b.set_cwd(tmp.wpath());
    look_dialect_detail::clear_shell_root_env(b);
    b.add(L"look.exe", {L"-f", key, no_nl});
    auto rb = b.run();

    TEST_LOG_CMD_LIST("look.exe", L"-f", key, no_nl);
    TEST_LOG_EXIT_CODE(rb);
    TEST_LOG("look without trailing newline stdout", rb.stdout_text);
    EXPECT_EQ(rb.exit_code, 0);
    EXPECT_EQ_TEXT(rb.stdout_text, expected);
  }
}

TEST(look, look_middle_empty_line_dictionary_keeps_entries) {
  TempDir tmp;
  tmp.write("mid.txt", "alpha\n\ncharlie\ndelta\n");
  const std::wstring operand = tmp.wpath() + L"\\mid.txt";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  look_dialect_detail::clear_shell_root_env(p);
  p.add(L"look.exe", {L"-f", L"del", operand});
  auto r = p.run();

  TEST_LOG_CMD_LIST("look.exe", L"-f", L"del", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("look middle empty line stdout", r.stdout_text);

  // [GNU util-linux look 2.39.3] a non-empty key finds "delta" through the
  // middle empty line; the existing semantics keep the line as a real entry.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "delta\n");

  // [GNU util-linux look 2.39.3] `look ''` prefix-matches every line,
  // including the middle empty one.
  Pipeline empty_key;
  empty_key.set_cwd(tmp.wpath());
  look_dialect_detail::clear_shell_root_env(empty_key);
  empty_key.add(L"look.exe", {L"", operand});
  auto re = empty_key.run();

  TEST_LOG_CMD_LIST("look.exe", L"", operand);
  TEST_LOG_EXIT_CODE(re);
  TEST_LOG("look empty key over middle empty line stdout", re.stdout_text);
  EXPECT_EQ(re.exit_code, 0);
  EXPECT_EQ_TEXT(re.stdout_text, "alpha\n\ncharlie\ndelta\n");
}

TEST(look, look_lone_newline_file_is_single_empty_entry) {
  TempDir tmp;
  tmp.write("nl.txt", "\n");
  const std::wstring operand = tmp.wpath() + L"\\nl.txt";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  look_dialect_detail::clear_shell_root_env(p);
  p.add(L"look.exe", {L"", operand});
  auto r = p.run();

  TEST_LOG_CMD_LIST("look.exe", L"", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("look lone newline stdout", r.stdout_text);

  // [GNU util-linux look 2.39.3] a file holding one "\n" has exactly one
  // (empty) entry; the phantom used to duplicate it.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "\n");
}
