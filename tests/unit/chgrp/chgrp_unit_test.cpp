/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include <aclapi.h>

#include "framework/winuxtest.h"

namespace {

auto utf8_to_wstring(std::string_view text) -> std::wstring {
  if (text.empty()) {
    return {};
  }

  int size = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                 static_cast<int>(text.size()), nullptr, 0);
  std::wstring out(size, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      out.data(), size);
  return out;
}

auto wstring_to_utf8(std::wstring_view text) -> std::string {
  if (text.empty()) {
    return {};
  }

  int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                 static_cast<int>(text.size()), nullptr, 0,
                                 nullptr, nullptr);
  std::string out(size, '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      out.data(), size, nullptr, nullptr);
  return out;
}

auto lookup_group_name_from_sid(PSID sid) -> std::string {
  if (sid == nullptr) {
    return {};
  }

  DWORD name_size = 0;
  DWORD domain_size = 0;
  SID_NAME_USE sid_type = SidTypeUnknown;
  LookupAccountSidW(nullptr, sid, nullptr, &name_size, nullptr, &domain_size,
                    &sid_type);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
    return {};
  }

  std::wstring name(name_size, L'\0');
  std::wstring domain(domain_size, L'\0');
  if (!LookupAccountSidW(nullptr, sid, name.data(), &name_size, domain.data(),
                         &domain_size, &sid_type)) {
    return {};
  }

  name.resize(name_size);
  return wstring_to_utf8(name);
}

auto get_group_name_for_path(const std::filesystem::path& path) -> std::string {
  std::wstring wpath = path.wstring();
  PSECURITY_DESCRIPTOR security_desc = nullptr;
  PSID group_sid = nullptr;

  const DWORD status =
      GetNamedSecurityInfoW(const_cast<wchar_t*>(wpath.c_str()), SE_FILE_OBJECT,
                            GROUP_SECURITY_INFORMATION, nullptr, &group_sid,
                            nullptr, nullptr, &security_desc);
  if (status != ERROR_SUCCESS) {
    if (security_desc != nullptr) {
      LocalFree(security_desc);
    }
    return {};
  }

  std::string group_name = lookup_group_name_from_sid(group_sid);
  if (security_desc != nullptr) {
    LocalFree(security_desc);
  }
  return group_name;
}

auto get_group_id_for_path(const std::filesystem::path& path) -> std::string {
  std::wstring wpath = path.wstring();
  PSECURITY_DESCRIPTOR security_desc = nullptr;
  PSID group_sid = nullptr;

  const DWORD status =
      GetNamedSecurityInfoW(const_cast<wchar_t*>(wpath.c_str()), SE_FILE_OBJECT,
                            GROUP_SECURITY_INFORMATION, nullptr, &group_sid,
                            nullptr, nullptr, &security_desc);
  if (status != ERROR_SUCCESS) {
    if (security_desc != nullptr) {
      LocalFree(security_desc);
    }
    return {};
  }

  std::string group_id;
  if (group_sid != nullptr && IsValidSid(group_sid)) {
    PUCHAR subauth_count = GetSidSubAuthorityCount(group_sid);
    if (subauth_count != nullptr && *subauth_count > 0) {
      DWORD* rid = GetSidSubAuthority(group_sid, *subauth_count - 1);
      if (rid != nullptr) {
        group_id = std::to_string(*rid);
      }
    }
  }

  if (security_desc != nullptr) {
    LocalFree(security_desc);
  }
  return group_id;
}

auto is_valid_group_name(const std::string& group_name) -> bool {
  std::wstring wgroup = utf8_to_wstring(group_name);
  DWORD sid_size = 0;
  DWORD domain_size = 0;
  SID_NAME_USE sid_type = SidTypeUnknown;

  LookupAccountNameW(nullptr, wgroup.c_str(), nullptr, &sid_size, nullptr,
                     &domain_size, &sid_type);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
    return false;
  }

  std::vector<std::byte> sid_buffer(sid_size);
  std::wstring domain(domain_size, L'\0');
  return LookupAccountNameW(nullptr, wgroup.c_str(), sid_buffer.data(),
                            &sid_size, domain.data(), &domain_size,
                            &sid_type) != 0;
}

auto choose_mismatching_valid_group(std::string_view current_group)
    -> std::string {
  for (const char* candidate :
       {"Users", "Administrators", "Everyone", "Guests", "SYSTEM"}) {
    if (_stricmp(candidate, std::string(current_group).c_str()) != 0 &&
        is_valid_group_name(candidate)) {
      return candidate;
    }
  }

  return {};
}

}  // namespace

TEST(chgrp, chgrp_no_args_shows_error) {
  Pipeline p;
  p.add(L"chgrp.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "chgrp: missing operand\n"
                 "Try 'chgrp --help' for more information.\n");
}

TEST(chgrp, chgrp_missing_group) {
  Pipeline p;
  p.add(L"chgrp.exe", {L"nonexistent"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "chgrp: missing operand after 'nonexistent'\n"
                 "Try 'chgrp --help' for more information.\n");
}

TEST(chgrp, chgrp_nonexistent_file) {
  Pipeline p;
  p.add(L"chgrp.exe", {L"Users", L"nonexistent_file_xyz"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "chgrp: cannot access 'nonexistent_file_xyz': No such file or "
                 "directory\n");
}

TEST(chgrp, chgrp_nonexistent_group) {
  TempDir tmp;
  std::ofstream(tmp.path / "test.txt") << "hello";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chgrp.exe", {L"nonexistent_group_xyz", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "chgrp: invalid group: 'nonexistent_group_xyz'\n"
                 "Try 'chgrp --help' for more information.\n");
}

TEST(chgrp, chgrp_reference_form_accepts_files_without_group_operand) {
  TempDir tmp;
  tmp.write("reference.txt", "ref");
  tmp.write("target.txt", "target");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chgrp.exe", {L"--reference=reference.txt", L"target.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(chgrp, chgrp_missing_reference_file_reports_gnu_style_error) {
  TempDir tmp;
  tmp.write("target.txt", "target");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chgrp.exe", {L"--reference=missing.txt", L"target.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "chgrp: failed to get attributes of 'missing.txt': No such file or "
      "directory\n"
      "Try 'chgrp --help' for more information.\n");
}

TEST(chgrp, chgrp_reference_verbose_reports_retained_group) {
  TempDir tmp;
  tmp.write("reference.txt", "ref");
  tmp.write("target.txt", "target");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chgrp.exe", {L"-v", L"--reference=reference.txt", L"target.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_NE(r.stderr_text.find("chgrp: group of 'target.txt' retained as "),
            std::string::npos);
}

TEST(chgrp, chgrp_invalid_from_group_fails) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chgrp.exe", {L"--from=nonexistent_group_xyz", L"Users", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "chgrp: invalid user: 'nonexistent_group_xyz'\n"
                 "Try 'chgrp --help' for more information.\n");
}

TEST(chgrp, chgrp_from_option_is_accepted) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chgrp.exe", {L"--from=Users", L"Users", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(chgrp, chgrp_from_matching_group_allows_reference_processing) {
  TempDir tmp;
  tmp.write("reference.txt", "ref");
  tmp.write("target.txt", "target");

  const std::string current_group =
      get_group_name_for_path(tmp.path / "target.txt");
  EXPECT_FALSE(current_group.empty());
  if (current_group.empty()) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chgrp.exe", {utf8_to_wstring("--from=" + current_group), L"-v",
                       L"--reference=reference.txt", L"target.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_NE(r.stderr_text.find("chgrp: group of 'target.txt' retained as "),
            std::string::npos);
}

TEST(chgrp, chgrp_from_mismatching_group_skips_reference_processing) {
  TempDir tmp;
  tmp.write("reference.txt", "ref");
  tmp.write("target.txt", "target");

  const std::string current_group =
      get_group_name_for_path(tmp.path / "target.txt");
  EXPECT_FALSE(current_group.empty());
  if (current_group.empty()) {
    return;
  }

  const std::string mismatch_group =
      choose_mismatching_valid_group(current_group);
  EXPECT_FALSE(mismatch_group.empty());
  if (mismatch_group.empty()) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chgrp.exe", {utf8_to_wstring("--from=" + mismatch_group), L"-v",
                       L"--reference=reference.txt", L"target.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(chgrp, chgrp_from_numeric_group_allows_reference_processing) {
  TempDir tmp;
  tmp.write("reference.txt", "ref");
  tmp.write("target.txt", "target");

  const std::string current_group_id =
      get_group_id_for_path(tmp.path / "target.txt");
  EXPECT_FALSE(current_group_id.empty());
  if (current_group_id.empty()) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chgrp.exe", {utf8_to_wstring("--from=" + current_group_id), L"-v",
                       L"--reference=reference.txt", L"target.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_NE(r.stderr_text.find("chgrp: group of 'target.txt' retained as "),
            std::string::npos);
}

TEST(chgrp, chgrp_from_colon_numeric_group_allows_reference_processing) {
  TempDir tmp;
  tmp.write("reference.txt", "ref");
  tmp.write("target.txt", "target");

  const std::string current_group_id =
      get_group_id_for_path(tmp.path / "target.txt");
  EXPECT_FALSE(current_group_id.empty());
  if (current_group_id.empty()) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chgrp.exe", {utf8_to_wstring("--from=:" + current_group_id), L"-v",
                       L"--reference=reference.txt", L"target.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_NE(r.stderr_text.find("chgrp: group of 'target.txt' retained as "),
            std::string::npos);
}

TEST(chgrp, chgrp_recursive_dereference_requires_H_or_L) {
  Pipeline p;
  p.add(L"chgrp.exe", {L"-R", L"--dereference", L"Users", L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("-R --dereference requires either -H or -L") !=
              std::string::npos);
}

TEST(chgrp, chgrp_preserve_root_recursive_root_failsafe) {
  Pipeline p;
  p.add(L"chgrp.exe", {L"--preserve-root", L"-R", L"Users", L"/"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "chgrp: it is dangerous to operate recursively on '/'\n"
                 "chgrp: use --no-preserve-root to override this failsafe\n");
}

TEST(chgrp,
     chgrp_preserve_root_without_recursive_does_not_block_reference_mode) {
  TempDir tmp;
  tmp.write("reference.txt", "ref");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chgrp.exe", {L"--preserve-root", L"--reference=reference.txt", L"/"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
}

// ===== #1241 regression: POSIX drive-dialect operands =====
// chgrp resolved '--reference' and target operands raw: an unfolded "/e/..."
// spelling makes Win32 answer for "<cwd>:\e\..." so existing E: files were
// reported as missing whenever the cwd sat on another drive. The probes
// synthesize the dialect spelling from the TempDir's own drive so they pass
// on any host.
namespace chgrp_dialect_detail {

auto drive_letter(const std::filesystem::path& path) -> wchar_t {
  wchar_t drive = path.wstring()[0];
  if (drive >= L'A' && drive <= L'Z') {
    drive = static_cast<wchar_t>(drive - L'A' + L'a');
  }
  return drive;
}

auto slash_drive_form(const std::filesystem::path& path) -> std::wstring {
  std::wstring text = path.wstring();
  text[0] = drive_letter(path);
  text.erase(1, 1);  // drop the ':' from "c:\..." (MSYS spelling has none)
  for (wchar_t& ch : text) {
    if (ch == L'\\') ch = L'/';
  }
  return L"/" + text;
}

auto other_fixed_drive(wchar_t other) -> wchar_t {
  const DWORD mask = GetLogicalDrives();
  for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
    if ((mask & (1u << (letter - L'A'))) == 0) continue;
    if (letter == other) continue;
    if (GetDriveTypeW((std::wstring(1, letter) + L":\\").c_str()) !=
        DRIVE_FIXED) {
      continue;
    }
    return letter;
  }
  return L'\0';
}

auto utf8(const std::wstring& text) -> std::string {
  if (text.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                       static_cast<int>(text.size()), nullptr,
                                       0, nullptr, nullptr);
  std::string out(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                      out.data(), size, nullptr, nullptr);
  return out;
}

}  // namespace chgrp_dialect_detail

TEST(chgrp, chgrp_reference_drive_dialect_resolves_operand_file) {
  TempDir tmp;
  tmp.write("g1235", "probe");
  const std::wstring operand =
      chgrp_dialect_detail::slash_drive_form(tmp.path / L"g1235");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chgrp.exe", {L"-v", L"--reference=" + operand, operand});

  auto r = p.run();

  TEST_LOG_CMD_LIST("chgrp.exe", L"-v", L"--reference=" + operand, operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chgrp stderr", r.stderr_text);

  // Parse-layer verdict: the reference and the target resolve, so the
  // no-op --reference form reports "retained" and succeeds (no security
  // state is modified).
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(
      r.stderr_text.find("group of '" + chgrp_dialect_detail::utf8(operand) +
                         "' retained as"),
      std::string::npos);
}

TEST(chgrp, chgrp_reference_drive_dialect_cwd_on_other_drive_resolves_operand) {
  TempDir tmp;
  tmp.write("g1235", "probe");
  const wchar_t other = chgrp_dialect_detail::other_fixed_drive(
      chgrp_dialect_detail::drive_letter(tmp.path));
  if (other == L'\0') {
    std::cout << "  SKIPPED (no second fixed drive to host the cwd)\n";
    return;
  }
  const std::wstring operand =
      chgrp_dialect_detail::slash_drive_form(tmp.path / L"g1235");

  Pipeline p;
  p.set_cwd(std::wstring(1, other) + L":\\");
  p.add(L"chgrp.exe", {L"-v", L"--reference=" + operand, operand});

  auto r = p.run();

  TEST_LOG_CMD_LIST("chgrp.exe", L"-v", L"--reference=" + operand, operand);
  TEST_LOG_EXIT_CODE(r);

  // The audit's exact misparse: from another drive the raw spelling probed
  // "<cwd>:\e\..." and reported the existing file as missing.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stderr_text.find("retained as"), std::string::npos);
  EXPECT_EQ(r.stderr_text.find("failed to get attributes"), std::string::npos);
}

TEST(chgrp, chgrp_reference_missing_file_quotes_operand_spelling) {
  TempDir tmp;
  tmp.write("g1235", "probe");
  const std::wstring missing =
      chgrp_dialect_detail::slash_drive_form(tmp.path / L"nope1235");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chgrp.exe",
        {L"-v", L"--reference=" + missing,
         chgrp_dialect_detail::slash_drive_form(tmp.path / L"g1235")});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chgrp stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  // The error quotes the spelling the user handed over, never the folded
  // "X:\..." API form (#1243 display precedent).
  EXPECT_NE(r.stderr_text.find("failed to get attributes of '" +
                               chgrp_dialect_detail::utf8(missing) + "'"),
            std::string::npos);
  EXPECT_EQ(r.stderr_text.find("X:\\"), std::string::npos);
}

TEST(chgrp, chgrp_recursive_reference_drive_dialect_walks_operand_drive) {
  TempDir tmp;
  tmp.write("d1235/inner1235", "probe");
  const std::wstring operand =
      chgrp_dialect_detail::slash_drive_form(tmp.path / L"d1235");
  const wchar_t tmp_drive = chgrp_dialect_detail::drive_letter(tmp.path);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chgrp.exe",
        {L"-R", L"-H", L"-v", L"--reference=" + operand, operand});

  auto r = p.run();

  TEST_LOG_CMD_LIST("chgrp.exe", L"-R", L"-H", L"-v", L"--reference=" + operand,
                    operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chgrp stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stderr_text.find("retained as"), std::string::npos);
  // Shadow negative: nothing was touched on the cwd drive.
  EXPECT_FALSE(std::filesystem::exists(
      std::filesystem::path(std::wstring(1, tmp_drive) + L":\\e")));
}

// ===== CR-on-#1249 regression: root spellings the operand layer collapses
// ===== The operand layer folds "//" and "///" onto "/" (ls -id / // reports
// one inode), but a verbatim GetFullPathNameW("//") answers for an UNC-ish
// name, so keeping those spellings verbatim in the failsafe comparison let
// `chgrp -R --preserve-root X //` bypass the check and really walk the shell
// root.  They must hit the root match directly; MSYS pins the texts below
// ("on '//'" with no suffix, "on '/'" after its own argv collapse of "///").
namespace chgrp_root_spelling_detail {

auto narrow(const std::wstring& text) -> std::string {
  if (text.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                       static_cast<int>(text.size()), nullptr,
                                       0, nullptr, nullptr);
  std::string out(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                      out.data(), size, nullptr, nullptr);
  return out;
}

}  // namespace chgrp_root_spelling_detail

TEST(chgrp, chgrp_preserve_root_all_separator_spellings_are_rejected) {
  for (const std::wstring operand : {L"//", L"///"}) {
    Pipeline p;
    p.add(L"chgrp.exe", {L"-R", L"--preserve-root", L"Users", operand});

    auto r = p.run();

    TEST_LOG_CMD_LIST("chgrp.exe", L"-R", L"--preserve-root", L"Users",
                      operand);
    TEST_LOG_EXIT_CODE(r);
    TEST_LOG("chgrp stderr", r.stderr_text);

    EXPECT_EQ(r.exit_code, 1);
    EXPECT_NE(
        r.stderr_text.find("it is dangerous to operate recursively on '" +
                           chgrp_root_spelling_detail::narrow(operand) + "'"),
        std::string::npos);
    EXPECT_NE(r.stderr_text.find("use --no-preserve-root"), std::string::npos);
  }
}

TEST(chgrp, chgrp_preserve_root_double_slash_component_spelling_is_refused) {
  // "//e/.." roots at "//e" (POSIX two-slash rule): MSYS resolves it against
  // that name and reports "cannot access", and so must we - no root match,
  // no descent.
  Pipeline p;
  p.add(L"chgrp.exe", {L"-R", L"--preserve-root", L"Users", L"//e/.."});

  auto r = p.run();

  TEST_LOG_CMD_LIST("chgrp.exe", L"-R", L"--preserve-root", L"Users",
                    L"//e/..");
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chgrp stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ(r.stderr_text.find("it is dangerous"), std::string::npos);
  EXPECT_NE(r.stderr_text.find("cannot access '//e/..'"), std::string::npos);
}

TEST(chgrp, chgrp_preserve_root_dotdot_collapse_spelling_is_rejected) {
  // "/e/.." lexically collapses onto the root; MSYS fires the failsafe with
  // the "(same as '/')" suffix.  Purely lexical, so the drive need not
  // exist.
  Pipeline p;
  p.add(L"chgrp.exe", {L"-R", L"--preserve-root", L"Users", L"/e/.."});

  auto r = p.run();

  TEST_LOG_CMD_LIST("chgrp.exe", L"-R", L"--preserve-root", L"Users", L"/e/..");
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chgrp stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("it is dangerous to operate recursively on "
                               "'/e/..' (same as '/')"),
            std::string::npos);
  EXPECT_NE(r.stderr_text.find("use --no-preserve-root"), std::string::npos);
}

// ===== #1259 regression: "." spellings resolved onto the shell root
// ===== GNU's failsafe compares device+inode against "/", so ".", "./" and
// ===== "././" from a cwd that IS the shell root are refused like "/" (with
// ===== the "(same as '/')" suffix, exactly like MSYS).  The volume-root
// ===== probe only catches spellings that name the volume root, so these
// ===== operands used to walk the shell tree.
TEST(chgrp, chgrp_preserve_root_dot_spellings_at_shell_root_are_rejected) {
  TempDir root;
  for (const std::wstring operand : {L".", L"./", L"././"}) {
    Pipeline p;
    p.set_cwd(root.wpath());
    p.set_env(L"__RUBASH_SHELL_ROOT", root.wpath());
    for (const auto* name : {L"NIU_ROOT", L"WINUXSH_ROOT", L"RUBASH_ROOT"}) {
      p.set_env(name, L"");
    }
    p.add(L"chgrp.exe", {L"-R", L"--preserve-root", L"Users", operand});

    auto r = p.run();

    TEST_LOG_CMD_LIST("chgrp.exe", L"-R", L"--preserve-root", L"Users",
                      operand);
    TEST_LOG_EXIT_CODE(r);
    TEST_LOG("chgrp stderr", r.stderr_text);

    EXPECT_EQ(r.exit_code, 1);
    EXPECT_NE(r.stderr_text.find("it is dangerous to operate recursively on '" +
                                 chgrp_root_spelling_detail::narrow(operand) +
                                 "' (same as '/')"),
              std::string::npos);
    EXPECT_NE(r.stderr_text.find("use --no-preserve-root"), std::string::npos);
  }
}

TEST(chgrp, chgrp_preserve_root_dot_below_shell_root_is_not_refused) {
  TempDir root;
  root.write("sub/probe.txt", "x");

  Pipeline p;
  p.set_cwd((root.path / "sub").wstring());
  p.set_env(L"__RUBASH_SHELL_ROOT", root.wpath());
  for (const auto* name : {L"NIU_ROOT", L"WINUXSH_ROOT", L"RUBASH_ROOT"}) {
    p.set_env(name, L"");
  }
  p.add(L"chgrp.exe", {L"-R", L"--preserve-root", L"Users", L"."});

  auto r = p.run();

  TEST_LOG_CMD_LIST("chgrp.exe", L"-R", L"--preserve-root", L"Users", L".");
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chgrp stderr", r.stderr_text);

  // "." resolves to <root>/sub, not the shell root: the failsafe stays
  // quiet (the chgrp itself may still fail on privileges).
  EXPECT_EQ(r.stderr_text.find("it is dangerous"), std::string::npos);
}

TEST(chgrp, chgrp_preserve_root_dot_without_shell_root_is_not_refused) {
  TempDir cwd;
  cwd.write("probe.txt", "x");

  Pipeline p;
  p.set_cwd(cwd.wpath());
  for (const auto* name :
       {L"__RUBASH_SHELL_ROOT", L"NIU_ROOT", L"WINUXSH_ROOT", L"RUBASH_ROOT"}) {
    p.set_env(name, L"");
  }
  p.add(L"chgrp.exe", {L"-R", L"--preserve-root", L"Users", L"."});

  auto r = p.run();

  TEST_LOG_CMD_LIST("chgrp.exe", L"-R", L"--preserve-root", L"Users", L".");
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chgrp stderr", r.stderr_text);

  // No shell root configured and the temp cwd is no volume root: "." is an
  // ordinary directory operand.
  EXPECT_EQ(r.stderr_text.find("it is dangerous"), std::string::npos);
}
