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

auto lookup_owner_name_from_sid(PSID sid) -> std::string {
  return lookup_group_name_from_sid(sid);
}

auto get_owner_name_for_path(const std::filesystem::path& path) -> std::string {
  std::wstring wpath = path.wstring();
  PSECURITY_DESCRIPTOR security_desc = nullptr;
  PSID owner_sid = nullptr;

  const DWORD status =
      GetNamedSecurityInfoW(const_cast<wchar_t*>(wpath.c_str()), SE_FILE_OBJECT,
                            OWNER_SECURITY_INFORMATION, &owner_sid, nullptr,
                            nullptr, nullptr, &security_desc);
  if (status != ERROR_SUCCESS) {
    if (security_desc != nullptr) {
      LocalFree(security_desc);
    }
    return {};
  }

  std::string owner_name = lookup_owner_name_from_sid(owner_sid);
  if (security_desc != nullptr) {
    LocalFree(security_desc);
  }
  return owner_name;
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

TEST(chown, chown_reference_form_accepts_files_without_owner_operand) {
  TempDir tmp;
  tmp.write("reference.txt", "ref");
  tmp.write("target.txt", "target");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chown.exe", {L"--reference=reference.txt", L"target.txt"});

  TEST_LOG_CMD_LIST("chown.exe", L"--reference=reference.txt", L"target.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chown stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(chown, chown_missing_reference_file_reports_gnu_style_error) {
  TempDir tmp;
  tmp.write("target.txt", "target");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chown.exe", {L"--reference=missing.txt", L"target.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "chown: failed to get attributes of 'missing.txt': No such file or "
      "directory\n"
      "Try 'chown --help' for more information.\n");
}

TEST(chown, chown_reference_verbose_reports_retained_ownership) {
  TempDir tmp;
  tmp.write("reference.txt", "ref");
  tmp.write("target.txt", "target");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chown.exe", {L"-v", L"--reference=reference.txt", L"target.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_NE(r.stderr_text.find("chown: ownership of 'target.txt' retained as "),
            std::string::npos);
}

TEST(chown, chown_invalid_user_fails) {
  TempDir tmp;
  tmp.write("file.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chown.exe", {L"nonexistent_user_xyz", L"file.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "chown: invalid user: 'nonexistent_user_xyz'\n"
                 "Try 'chown --help' for more information.\n");
}

TEST(chown, chown_invalid_group_fails) {
  TempDir tmp;
  tmp.write("file.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chown.exe", {L":nonexistent_group_xyz", L"file.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "chown: invalid group: ':nonexistent_group_xyz'\n"
                 "Try 'chown --help' for more information.\n");
}

TEST(chown, chown_double_separator_forms_fail_with_invalid_group) {
  TempDir tmp;
  tmp.write("file.txt", "hello");

  Pipeline colon;
  colon.set_cwd(tmp.wpath());
  colon.add(L"chown.exe", {L"::", L"file.txt"});
  auto colon_result = colon.run();

  EXPECT_EQ(colon_result.exit_code, 1);
  EXPECT_EQ_TEXT(colon_result.stderr_text,
                 "chown: invalid group: '::'\n"
                 "Try 'chown --help' for more information.\n");

  Pipeline dot;
  dot.set_cwd(tmp.wpath());
  dot.add(L"chown.exe", {L"..", L"file.txt"});
  auto dot_result = dot.run();

  EXPECT_EQ(dot_result.exit_code, 1);
  EXPECT_EQ_TEXT(dot_result.stderr_text,
                 "chown: warning: '.' should be ':'\n"
                 "chown: invalid group: '..'\n"
                 "Try 'chown --help' for more information.\n");
}

TEST(chown, chown_numeric_owner_with_empty_group_fails_with_invalid_spec) {
  TempDir tmp;
  tmp.write("file.txt", "hello");

  Pipeline colon;
  colon.set_cwd(tmp.wpath());
  colon.add(L"chown.exe", {L"1001:", L"file.txt"});
  auto colon_result = colon.run();

  EXPECT_EQ(colon_result.exit_code, 1);
  EXPECT_EQ_TEXT(colon_result.stderr_text,
                 "chown: invalid spec: '1001:'\n"
                 "Try 'chown --help' for more information.\n");

  Pipeline dot;
  dot.set_cwd(tmp.wpath());
  dot.add(L"chown.exe", {L"1001.", L"file.txt"});
  auto dot_result = dot.run();

  EXPECT_EQ(dot_result.exit_code, 1);
  EXPECT_EQ_TEXT(dot_result.stderr_text,
                 "chown: warning: '.' should be ':'\n"
                 "chown: invalid spec: '1001.'\n"
                 "Try 'chown --help' for more information.\n");
}

TEST(chown, chown_dot_separator_owner_form_warns) {
  TempDir tmp;
  tmp.write("file.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chown.exe", {L"Users.", L"file.txt"});
  auto r = p.run();

  EXPECT_NE(r.stderr_text.find("chown: warning: '.' should be ':'\n"),
            std::string::npos);
  if (r.exit_code == 0) {
    EXPECT_EQ(get_owner_name_for_path(tmp.path / "file.txt"), "Users");
  } else {
    EXPECT_NE(r.stderr_text.find("Operation not permitted"), std::string::npos);
  }
}

TEST(chown, chown_dot_separator_owner_group_form_warns) {
  TempDir tmp;
  tmp.write("file.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chown.exe", {L"Users.Users", L"file.txt"});
  auto r = p.run();

  EXPECT_NE(r.stderr_text.find("chown: warning: '.' should be ':'\n"),
            std::string::npos);
  if (r.exit_code == 0) {
    EXPECT_EQ(get_owner_name_for_path(tmp.path / "file.txt"), "Users");
    EXPECT_EQ(get_group_name_for_path(tmp.path / "file.txt"), "Users");
  } else {
    EXPECT_NE(r.stderr_text.find("Operation not permitted"), std::string::npos);
  }
}

TEST(chown, chown_invalid_from_user_fails) {
  TempDir tmp;
  tmp.write("file.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chown.exe", {L"--from=nonexistent_user_xyz", L"Users", L"file.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "chown: invalid user: 'nonexistent_user_xyz'\n"
                 "Try 'chown --help' for more information.\n");
}

TEST(chown, chown_invalid_from_group_fails) {
  TempDir tmp;
  tmp.write("file.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chown.exe",
        {L"--from=:nonexistent_group_xyz", L"Users", L"file.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "chown: invalid group: ':nonexistent_group_xyz'\n"
                 "Try 'chown --help' for more information.\n");
}

TEST(chown, chown_from_mismatching_group_skips_reference_processing) {
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
  p.add(L"chown.exe", {utf8_to_wstring("--from=:" + mismatch_group), L"-v",
                       L"--reference=reference.txt", L"target.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_NE(r.stderr_text.find("retained as "), std::string::npos);
  EXPECT_EQ(r.stderr_text.find("changing ownership of "), std::string::npos);
}

TEST(chown, chown_from_mismatching_group_skips_direct_processing) {
  TempDir tmp;
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
  p.add(L"chown.exe", {utf8_to_wstring("--from=:" + mismatch_group), L"-v",
                       L":Users", L"target.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_NE(r.stderr_text.find("retained as "), std::string::npos);
  EXPECT_EQ(r.stderr_text.find("changing ownership of "), std::string::npos);
}

TEST(chown, chown_from_matching_group_reference_avoids_placeholder_warning) {
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
  p.add(L"chown.exe", {utf8_to_wstring("--from=:" + current_group), L"-v",
                       L"--reference=reference.txt", L"target.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_NE(r.stderr_text.find("retained as "), std::string::npos);
  EXPECT_EQ(r.stderr_text.find("not supported on Windows"), std::string::npos);
}

TEST(chown, chown_from_matching_group_direct_avoids_placeholder_warning) {
  TempDir tmp;
  tmp.write("target.txt", "target");

  const std::string current_group =
      get_group_name_for_path(tmp.path / "target.txt");
  EXPECT_FALSE(current_group.empty());
  if (current_group.empty()) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chown.exe", {utf8_to_wstring("--from=:" + current_group), L"-v",
                       L":Users", L"target.txt"});

  auto r = p.run();

  if (r.exit_code == 0) {
    EXPECT_NE(r.stdout_text.find("changed ownership of 'target.txt'"),
              std::string::npos);
    EXPECT_EQ(get_group_name_for_path(tmp.path / "target.txt"), "Users");
  } else {
    EXPECT_NE(r.stderr_text.find("Operation not permitted"), std::string::npos);
  }
}

TEST(chown, chown_colon_only_verbose_reports_retained_ownership) {
  TempDir tmp;
  tmp.write("target.txt", "target");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chown.exe", {L"-v", L":", L"target.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_NE(r.stderr_text.find("chown: ownership of 'target.txt' retained as "),
            std::string::npos);
  EXPECT_EQ(r.stderr_text.find("changing ownership of "), std::string::npos);
}

TEST(chown, chown_current_owner_only_verbose_reports_retained_ownership) {
  TempDir tmp;
  tmp.write("target.txt", "target");

  const std::string current_owner =
      get_owner_name_for_path(tmp.path / "target.txt");
  EXPECT_FALSE(current_owner.empty());
  if (current_owner.empty()) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chown.exe",
        {L"-v", utf8_to_wstring(current_owner + ":"), L"target.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_NE(r.stderr_text.find("chown: ownership of 'target.txt' retained as "),
            std::string::npos);
  EXPECT_EQ(r.stderr_text.find("changing ownership of "), std::string::npos);
}

TEST(chown, chown_recursive_dereference_requires_H_or_L) {
  Pipeline p;
  p.add(L"chown.exe", {L"-R", L"--dereference", L"Users", L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("-R --dereference requires either -H or -L") !=
              std::string::npos);
}

TEST(chown, chown_preserve_root_recursive_root_failsafe) {
  Pipeline p;
  p.add(L"chown.exe", {L"--preserve-root", L"-R", L"Users", L"/"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "chown: it is dangerous to operate recursively on '/'\n"
                 "chown: use --no-preserve-root to override this failsafe\n");
}

TEST(chown, chown_no_args_fails) {
  Pipeline p;
  p.add(L"chown.exe", {});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
}

TEST(chown, chown_missing_file_fails) {
  Pipeline p;
  p.add(L"chown.exe", {L"Users", L"nonexistent_file_xyz.txt"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
}

TEST(chown, chown_verbose) {
  TempDir tmp;
  tmp.write("file.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chown.exe", {L"-v", L"Users", L"file.txt"});
  auto r = p.run();

  if (r.exit_code == 0) {
    EXPECT_NE(r.stdout_text.find("changed ownership of 'file.txt'"),
              std::string::npos);
    EXPECT_EQ(get_owner_name_for_path(tmp.path / "file.txt"), "Users");
  } else {
    EXPECT_NE(r.stderr_text.find("Operation not permitted"), std::string::npos);
  }
}

TEST(chown, chown_recursive) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "subdir");
  tmp.write("file.txt", "hello");
  tmp.write("subdir/nested.txt", "world");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chown.exe", {L"-R", L"Users", L"."});
  auto r = p.run();

  if (r.exit_code == 0) {
    EXPECT_EQ(get_owner_name_for_path(tmp.path / "file.txt"), "Users");
    EXPECT_EQ(get_owner_name_for_path(tmp.path / "subdir/nested.txt"), "Users");
  } else {
    EXPECT_NE(r.stderr_text.find("Operation not permitted"), std::string::npos);
  }
}

auto get_owner_name_for_link(const std::filesystem::path& path) -> std::string {
  std::wstring wpath = path.wstring();
  HANDLE handle = CreateFileW(
      wpath.c_str(), READ_CONTROL,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
      nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return {};
  }
  PSECURITY_DESCRIPTOR security_desc = nullptr;
  PSID owner_sid = nullptr;
  const DWORD status =
      GetSecurityInfo(handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
                      &owner_sid, nullptr, nullptr, nullptr, &security_desc);
  std::string owner;
  if (status == ERROR_SUCCESS) {
    owner = lookup_owner_name_from_sid(owner_sid);
  }
  if (security_desc != nullptr) {
    LocalFree(security_desc);
  }
  CloseHandle(handle);
  return owner;
}

TEST(chown, chown_h_operates_on_dangling_symlink) {
  TempDir tmp;

  Pipeline ln;
  ln.set_cwd(tmp.wpath());
  ln.add(L"ln.exe", {L"-s", L"missing-target.txt", L"dangling.txt"});
  auto link_result = ln.run();
  if (link_result.exit_code != 0) {
    std::cout << "  SKIPPED (cannot create symbolic links)\n";
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chown.exe", {L"-h", L"Users", L"dangling.txt"});
  auto r = p.run();

  if (r.exit_code == 0) {
    EXPECT_EQ(get_owner_name_for_link(tmp.path / "dangling.txt"), "Users");
  } else {
    EXPECT_NE(r.stderr_text.find("Operation not permitted"), std::string::npos);
  }
}

TEST(chown, chown_without_h_cannot_dereference_dangling_symlink) {
  TempDir tmp;

  Pipeline ln;
  ln.set_cwd(tmp.wpath());
  ln.add(L"ln.exe", {L"-s", L"missing-target.txt", L"dangling.txt"});
  auto link_result = ln.run();
  if (link_result.exit_code != 0) {
    std::cout << "  SKIPPED (cannot create symbolic links)\n";
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chown.exe", {L"Users", L"dangling.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("cannot dereference 'dangling.txt'"),
            std::string::npos);
}

// ===== #1241 regression: POSIX drive-dialect operands =====
// chown resolved '--reference' and target operands raw: an unfolded "/e/..."
// spelling makes Win32 answer for "<cwd>:\e\..." so the reference lookup
// failed and existing E: files were reported as missing whenever the cwd sat
// on another drive. The probes synthesize the dialect spelling from the
// TempDir's own drive so they pass on any host.
namespace chown_dialect_detail {

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

}  // namespace chown_dialect_detail

TEST(chown, chown_reference_drive_dialect_resolves_operand_file) {
  TempDir tmp;
  tmp.write("g1235", "probe");
  const std::wstring operand =
      chown_dialect_detail::slash_drive_form(tmp.path / L"g1235");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chown.exe", {L"-v", L"--reference=" + operand, operand});

  auto r = p.run();

  TEST_LOG_CMD_LIST("chown.exe", L"-v", L"--reference=" + operand, operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chown stderr", r.stderr_text);

  // Parse-layer verdict: the reference and the target resolve, so the
  // no-op --reference form reports "retained" and succeeds (no security
  // state is modified).
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(
      r.stderr_text.find("ownership of '" +
                         chown_dialect_detail::utf8(operand) + "' retained as"),
      std::string::npos);
}

TEST(chown, chown_reference_drive_dialect_cwd_on_other_drive_resolves_operand) {
  TempDir tmp;
  tmp.write("g1235", "probe");
  const wchar_t other = chown_dialect_detail::other_fixed_drive(
      chown_dialect_detail::drive_letter(tmp.path));
  if (other == L'\0') {
    std::cout << "  SKIPPED (no second fixed drive to host the cwd)\n";
    return;
  }
  const std::wstring operand =
      chown_dialect_detail::slash_drive_form(tmp.path / L"g1235");

  Pipeline p;
  p.set_cwd(std::wstring(1, other) + L":\\");
  p.add(L"chown.exe", {L"-v", L"--reference=" + operand, operand});

  auto r = p.run();

  TEST_LOG_CMD_LIST("chown.exe", L"-v", L"--reference=" + operand, operand);
  TEST_LOG_EXIT_CODE(r);

  // The audit's exact misparse: from another drive the raw spelling probed
  // "<cwd>:\e\..." and reported the existing file as missing.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stderr_text.find("retained as"), std::string::npos);
  EXPECT_EQ(r.stderr_text.find("failed to get attributes"), std::string::npos);
}

TEST(chown, chown_reference_missing_file_quotes_operand_spelling) {
  TempDir tmp;
  tmp.write("g1235", "probe");
  const std::wstring missing =
      chown_dialect_detail::slash_drive_form(tmp.path / L"nope1235");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chown.exe",
        {L"-v", L"--reference=" + missing,
         chown_dialect_detail::slash_drive_form(tmp.path / L"g1235")});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chown stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("failed to get attributes of '" +
                               chown_dialect_detail::utf8(missing) + "'"),
            std::string::npos);
  EXPECT_EQ(r.stderr_text.find("X:\\"), std::string::npos);
}

TEST(chown, chown_recursive_reference_drive_dialect_walks_operand_drive) {
  TempDir tmp;
  tmp.write("d1235/inner1235", "probe");
  const std::wstring operand =
      chown_dialect_detail::slash_drive_form(tmp.path / L"d1235");
  const wchar_t tmp_drive = chown_dialect_detail::drive_letter(tmp.path);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chown.exe",
        {L"-R", L"-H", L"-v", L"--reference=" + operand, operand});

  auto r = p.run();

  TEST_LOG_CMD_LIST("chown.exe", L"-R", L"-H", L"-v", L"--reference=" + operand,
                    operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chown stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stderr_text.find("retained as"), std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(
      std::filesystem::path(std::wstring(1, tmp_drive) + L":\\e")));
}

// ===== CR-on-#1249 regression: root spellings the operand layer collapses
// ===== The operand layer folds "//" and "///" onto "/" (ls -id / // reports
// one inode), but a verbatim GetFullPathNameW("//") answers for an UNC-ish
// name, so keeping those spellings verbatim in the failsafe comparison let
// `chown -R --preserve-root X //` bypass the check and really modify group
// ownership under the shell root.  They must hit the root match directly;
// MSYS pins the texts below ("on '//'" with no suffix).
namespace chown_root_spelling_detail {

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

}  // namespace chown_root_spelling_detail

TEST(chown, chown_preserve_root_all_separator_spellings_are_rejected) {
  for (const std::wstring operand : {L"//", L"///"}) {
    Pipeline p;
    p.add(L"chown.exe", {L"-R", L"--preserve-root", L"Users", operand});

    auto r = p.run();

    TEST_LOG_CMD_LIST("chown.exe", L"-R", L"--preserve-root", L"Users",
                      operand);
    TEST_LOG_EXIT_CODE(r);
    TEST_LOG("chown stderr", r.stderr_text);

    EXPECT_EQ(r.exit_code, 1);
    EXPECT_NE(
        r.stderr_text.find("it is dangerous to operate recursively on '" +
                           chown_root_spelling_detail::narrow(operand) + "'"),
        std::string::npos);
    EXPECT_NE(r.stderr_text.find("use --no-preserve-root"), std::string::npos);
  }
}

TEST(chown, chown_preserve_root_double_slash_component_spelling_is_refused) {
  // "//e/.." roots at "//e" (POSIX two-slash rule): MSYS resolves it against
  // that name and reports "cannot access", and so must we - no root match,
  // no descent.
  Pipeline p;
  p.add(L"chown.exe", {L"-R", L"--preserve-root", L"Users", L"//e/.."});

  auto r = p.run();

  TEST_LOG_CMD_LIST("chown.exe", L"-R", L"--preserve-root", L"Users",
                    L"//e/..");
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chown stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ(r.stderr_text.find("it is dangerous"), std::string::npos);
  // #1262: pin the MSYS reason verbatim - chown used to print the Win32
  // invalid-name text where chgrp and MSYS print ENOENT.
  EXPECT_NE(
      r.stderr_text.find("cannot access '//e/..': No such file or directory"),
      std::string::npos);
}

TEST(chown, chown_preserve_root_dotdot_collapse_spelling_is_rejected) {
  // "/e/.." lexically collapses onto the root; MSYS fires the failsafe with
  // the "(same as '/')" suffix.  Purely lexical, so the drive need not
  // exist.
  Pipeline p;
  p.add(L"chown.exe", {L"-R", L"--preserve-root", L"Users", L"/e/.."});

  auto r = p.run();

  TEST_LOG_CMD_LIST("chown.exe", L"-R", L"--preserve-root", L"Users", L"/e/..");
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chown stderr", r.stderr_text);

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
TEST(chown, chown_preserve_root_dot_spellings_at_shell_root_are_rejected) {
  TempDir root;
  for (const std::wstring operand : {L".", L"./", L"././"}) {
    Pipeline p;
    p.set_cwd(root.wpath());
    p.set_env(L"__RUBASH_SHELL_ROOT", root.wpath());
    for (const auto* name : {L"NIU_ROOT", L"WINUXSH_ROOT", L"RUBASH_ROOT"}) {
      p.set_env(name, L"");
    }
    p.add(L"chown.exe", {L"-R", L"--preserve-root", L"Users", operand});

    auto r = p.run();

    TEST_LOG_CMD_LIST("chown.exe", L"-R", L"--preserve-root", L"Users",
                      operand);
    TEST_LOG_EXIT_CODE(r);
    TEST_LOG("chown stderr", r.stderr_text);

    EXPECT_EQ(r.exit_code, 1);
    EXPECT_NE(r.stderr_text.find("it is dangerous to operate recursively on '" +
                                 chown_root_spelling_detail::narrow(operand) +
                                 "' (same as '/')"),
              std::string::npos);
    EXPECT_NE(r.stderr_text.find("use --no-preserve-root"), std::string::npos);
  }
}

TEST(chown, chown_preserve_root_dot_below_shell_root_is_not_refused) {
  TempDir root;
  root.write("sub/probe.txt", "x");

  Pipeline p;
  p.set_cwd((root.path / "sub").wstring());
  p.set_env(L"__RUBASH_SHELL_ROOT", root.wpath());
  for (const auto* name : {L"NIU_ROOT", L"WINUXSH_ROOT", L"RUBASH_ROOT"}) {
    p.set_env(name, L"");
  }
  p.add(L"chown.exe", {L"-R", L"--preserve-root", L"Users", L"."});

  auto r = p.run();

  TEST_LOG_CMD_LIST("chown.exe", L"-R", L"--preserve-root", L"Users", L".");
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chown stderr", r.stderr_text);

  // "." resolves to <root>/sub, not the shell root: the failsafe stays
  // quiet (the chown itself may still fail on privileges).
  EXPECT_EQ(r.stderr_text.find("it is dangerous"), std::string::npos);
}

TEST(chown, chown_preserve_root_dot_without_shell_root_is_not_refused) {
  TempDir cwd;
  cwd.write("probe.txt", "x");

  Pipeline p;
  p.set_cwd(cwd.wpath());
  for (const auto* name :
       {L"__RUBASH_SHELL_ROOT", L"NIU_ROOT", L"WINUXSH_ROOT", L"RUBASH_ROOT"}) {
    p.set_env(name, L"");
  }
  p.add(L"chown.exe", {L"-R", L"--preserve-root", L"Users", L"."});

  auto r = p.run();

  TEST_LOG_CMD_LIST("chown.exe", L"-R", L"--preserve-root", L"Users", L".");
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chown stderr", r.stderr_text);

  // No shell root configured and the temp cwd is no volume root: "." is an
  // ordinary directory operand.
  EXPECT_EQ(r.stderr_text.find("it is dangerous"), std::string::npos);
}
