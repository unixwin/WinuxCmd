/*
 *  Copyright © 2026 [caomengxuan666]
 */
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

auto get_group_id_for_path(const std::filesystem::path& path) -> std::string {
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
                 "chgrp: cannot access 'nonexistent_file_xyz': No such file or directory\n");
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
  p.add(L"chgrp.exe",
        {L"--from=nonexistent_group_xyz", L"Users", L"test.txt"});

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
  p.add(L"chgrp.exe",
        {utf8_to_wstring("--from=" + current_group), L"-v",
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
  p.add(L"chgrp.exe",
        {utf8_to_wstring("--from=" + mismatch_group), L"-v",
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
  p.add(L"chgrp.exe",
        {utf8_to_wstring("--from=" + current_group_id), L"-v",
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
  p.add(L"chgrp.exe",
        {utf8_to_wstring("--from=:" + current_group_id), L"-v",
         L"--reference=reference.txt", L"target.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_NE(r.stderr_text.find("chgrp: group of 'target.txt' retained as "),
            std::string::npos);
}

TEST(chgrp, chgrp_preserve_root_recursive_root_failsafe) {
  Pipeline p;
  p.add(L"chgrp.exe", {L"--preserve-root", L"-R", L"Users", L"/"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "chgrp: it is dangerous to operate recursively on '/'\n"
      "chgrp: use --no-preserve-root to override this failsafe\n");
}

TEST(chgrp, chgrp_preserve_root_without_recursive_does_not_block_reference_mode) {
  TempDir tmp;
  tmp.write("reference.txt", "ref");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chgrp.exe",
        {L"--preserve-root", L"--reference=reference.txt", L"/"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
}
