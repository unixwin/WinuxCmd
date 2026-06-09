#include "framework/winuxtest.h"

#include <aclapi.h>

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

  const DWORD status = GetNamedSecurityInfoW(
      const_cast<wchar_t*>(wpath.c_str()), SE_FILE_OBJECT,
      OWNER_SECURITY_INFORMATION, &owner_sid, nullptr, nullptr, nullptr,
      &security_desc);
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

  const DWORD status = GetNamedSecurityInfoW(
      const_cast<wchar_t*>(wpath.c_str()), SE_FILE_OBJECT,
      GROUP_SECURITY_INFORMATION, nullptr, &group_sid, nullptr, nullptr,
      &security_desc);
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

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "chown: warning: '.' should be ':'\n");
}

TEST(chown, chown_dot_separator_owner_group_form_warns) {
  TempDir tmp;
  tmp.write("file.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chown.exe", {L"Users.Users", L"file.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "chown: warning: '.' should be ':'\n");
}

TEST(chown, chown_invalid_from_user_fails) {
  TempDir tmp;
  tmp.write("file.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chown.exe",
        {L"--from=nonexistent_user_xyz", L"Users", L"file.txt"});
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
  p.add(L"chown.exe",
        {utf8_to_wstring("--from=:" + mismatch_group), L"-v",
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
  p.add(L"chown.exe",
        {utf8_to_wstring("--from=:" + mismatch_group), L"-v", L":Users",
         L"target.txt"});

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
  p.add(L"chown.exe",
        {utf8_to_wstring("--from=:" + current_group), L"-v",
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
  p.add(L"chown.exe",
        {utf8_to_wstring("--from=:" + current_group), L"-v", L":Users",
         L"target.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("changing ownership of 'target.txt'"),
            std::string::npos);
  EXPECT_EQ(r.stdout_text.find("not supported on Windows"), std::string::npos);
  EXPECT_EQ(r.stderr_text.find("not supported on Windows"), std::string::npos);
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

TEST(chown, chown_preserve_root_recursive_root_failsafe) {
  Pipeline p;
  p.add(L"chown.exe", {L"--preserve-root", L"-R", L"Users", L"/"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(
      r.stderr_text,
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

  // May fail due to permissions, but should handle gracefully
  EXPECT_TRUE(r.exit_code == 0 || r.exit_code == 1);
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

  // May fail due to permissions, but should handle gracefully
  EXPECT_TRUE(r.exit_code == 0 || r.exit_code == 1);
}
