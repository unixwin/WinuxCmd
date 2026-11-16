// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

namespace {

// GetTempPathW spells the harness temp root with the runner
// account's 8.3 short name (C:\Users\RUNNER~1\...), while a physical
// cwd is always reported in its long form.  Physical-output
// expectations therefore compare against the canonical spelling of
// the same directory; tests that echo $PWD verbatim keep using the
// as-typed spelling; niubash#993 §7 renders it in the shell display form.
// Mirrors tmp_base (winux-core path.rs): TMPDIR > TEMP > TMP, folded
// through the /c/ drive dialect.
auto temp_root_text() -> const std::string& {
  static const std::string value = [] {
    auto nativeize = [](std::string v) {
      std::replace(v.begin(), v.end(), '\\', '/');
      if (v.size() >= 2 && v[0] == '/' &&
          ((v[1] >= 'a' && v[1] <= 'z') || (v[1] >= 'A' && v[1] <= 'Z')) &&
          (v.size() == 2 || v[2] == '/')) {
        v = std::string(1, static_cast<char>(toupper(v[1]))) + ":" +
            (v.size() > 2 ? v.substr(2) : "/");
      }
      while (!v.empty() && v.back() == '/') v.pop_back();
      return v;
    };
    for (const char* name : {"TMPDIR", "TEMP", "TMP"}) {
      if (const char* v = getenv(name); v != nullptr && *v != '\0') {
        return nativeize(v);
      }
    }
    wchar_t buf[MAX_PATH];
    const DWORD n = GetTempPathW(MAX_PATH, buf);
    if (n != 0 && n < MAX_PATH) {
      return nativeize(std::string(buf, buf + n));
    }
    return std::string{};
  }();
  return value;
}

// The §7 display spelling: under the temp mount paths print as
// "/tmp/...", elsewhere as "/c/..." — matching realpath/readlink -f and
// the Git Bash oracle.
auto display_spelling(std::string native) -> std::string {
  std::replace(native.begin(), native.end(), '\\', '/');
  const std::string& tmp_root = temp_root_text();
  if (!tmp_root.empty()) {
    for (const auto& [base, mount] :
         std::vector<std::pair<std::string, std::string>>{
             {tmp_root + "/var/tmp", "/var/tmp"}, {tmp_root, "/tmp"}}) {
      if (native == base ||
          (native.size() > base.size() &&
           native.compare(0, base.size(), base) == 0 &&
           native[base.size()] == '/')) {
        return mount + native.substr(base.size());
      }
    }
  }
  if (native.size() >= 2 && native[1] == ':' &&
      ((native[0] >= 'a' && native[0] <= 'z') ||
       (native[0] >= 'A' && native[0] <= 'Z'))) {
    return "/" + std::string(1, static_cast<char>(tolower(native[0]))) +
           native.substr(2);
  }
  return native;
}

auto physical_spelling(const TempDir& tmp) -> std::string {
  std::error_code ec;
  const auto canon = std::filesystem::canonical(tmp.path, ec);
  return display_spelling(ec ? tmp.path.string() : canon.string());
}

}  // namespace

TEST(pwd, pwd_basic) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pwd.exe", {});

  TEST_LOG_CMD_LIST("pwd.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("pwd.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should output the current working directory
  EXPECT_FALSE(r.stdout_text.empty());
  // Should end with newline
  EXPECT_TRUE(r.stdout_text.back() == '\n');
  // Should contain the temp directory path
  EXPECT_TRUE(r.stdout_text.find(physical_spelling(tmp)) != std::string::npos);
}

TEST(pwd, pwd_logical) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pwd.exe", {L"-L"});

  TEST_LOG_CMD_LIST("pwd.exe", L"-L");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("pwd.exe -L output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find(physical_spelling(tmp)) != std::string::npos);
}

TEST(pwd, pwd_physical) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pwd.exe", {L"-P"});

  TEST_LOG_CMD_LIST("pwd.exe", L"-P");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("pwd.exe -P output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find(physical_spelling(tmp)) != std::string::npos);
}

TEST(pwd, pwd_long_option_logical) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pwd.exe", {L"--logical"});

  TEST_LOG_CMD_LIST("pwd.exe", L"--logical");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("pwd.exe --logical output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find(physical_spelling(tmp)) != std::string::npos);
}

TEST(pwd, pwd_long_option_physical) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pwd.exe", {L"--physical"});

  TEST_LOG_CMD_LIST("pwd.exe", L"--physical");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("pwd.exe --physical output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find(physical_spelling(tmp)) != std::string::npos);
}

TEST(pwd, pwd_logical_uses_pwd_environment) {
  TempDir tmp;
  auto logical_pwd = tmp.path.string();
  std::wstring logical_pwd_w(logical_pwd.begin(), logical_pwd.end());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"PWD", logical_pwd_w);
  p.add(L"pwd.exe", {L"-L"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, display_spelling(logical_pwd) + "\n");
}

TEST(pwd, pwd_posixly_correct_defaults_to_logical_pwd_environment) {
  TempDir tmp;
  auto logical_pwd = tmp.path.string();
  std::wstring logical_pwd_w(logical_pwd.begin(), logical_pwd.end());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"PWD", logical_pwd_w);
  p.set_env(L"POSIXLY_CORRECT", L"1");
  p.add(L"pwd.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, display_spelling(logical_pwd) + "\n");
}

TEST(pwd, pwd_physical_overrides_posixly_correct_default) {
  TempDir tmp;
  auto logical_pwd = tmp.path.string();
  std::wstring logical_pwd_w(logical_pwd.begin(), logical_pwd.end());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"PWD", logical_pwd_w);
  p.set_env(L"POSIXLY_CORRECT", L"1");
  p.add(L"pwd.exe", {L"-P"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(physical_spelling(tmp)) != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("logical-posix-view") == std::string::npos);
}

TEST(pwd, pwd_logical_last_occurrence_wins_over_physical) {
  TempDir tmp;
  auto logical_pwd = tmp.path.string();
  std::wstring logical_pwd_w(logical_pwd.begin(), logical_pwd.end());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"PWD", logical_pwd_w);
  p.add(L"pwd.exe", {L"-P", L"--logical"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, display_spelling(logical_pwd) + "\n");
}

TEST(pwd, pwd_physical_last_occurrence_wins_over_logical) {
  TempDir tmp;
  auto logical_pwd = tmp.path.string();
  std::wstring logical_pwd_w(logical_pwd.begin(), logical_pwd.end());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"PWD", logical_pwd_w);
  p.add(L"pwd.exe", {L"--logical", L"-P"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(physical_spelling(tmp)) != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("logical-view") == std::string::npos);
}

TEST(pwd, pwd_logical_ignores_relative_pwd_environment) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"PWD", L"relative\\logical-view");
  p.add(L"pwd.exe", {L"-L"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(physical_spelling(tmp)) != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("relative\\logical-view") ==
              std::string::npos);
}

TEST(pwd, pwd_logical_ignores_nonexistent_absolute_pwd_environment) {
  TempDir tmp;
  auto logical_pwd = tmp.path.string() + "\\missing-logical-view";
  std::wstring logical_pwd_w(logical_pwd.begin(), logical_pwd.end());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"PWD", logical_pwd_w);
  p.add(L"pwd.exe", {L"-L"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(physical_spelling(tmp)) != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("missing-logical-view") == std::string::npos);
}

TEST(pwd, pwd_logical_ignores_wrong_absolute_pwd_environment) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "other");
  auto logical_pwd = (tmp.path / "other").string();
  std::wstring logical_pwd_w(logical_pwd.begin(), logical_pwd.end());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"PWD", logical_pwd_w);
  p.add(L"pwd.exe", {L"-L"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(physical_spelling(tmp)) != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("\\other") == std::string::npos);
}

TEST(pwd, pwd_logical_ignores_pwd_environment_with_dot_component) {
  TempDir tmp;
  auto logical_pwd = tmp.path.string() + "\\.\\";
  std::wstring logical_pwd_w(logical_pwd.begin(), logical_pwd.end());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"PWD", logical_pwd_w);
  p.add(L"pwd.exe", {L"-L"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(physical_spelling(tmp)) != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("\\.\\") == std::string::npos);
}

TEST(pwd, pwd_logical_ignores_pwd_environment_with_dotdot_component) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "child");
  auto logical_pwd = (tmp.path / "child" / "..").string();
  std::wstring logical_pwd_w(logical_pwd.begin(), logical_pwd.end());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"PWD", logical_pwd_w);
  p.add(L"pwd.exe", {L"-L"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(physical_spelling(tmp)) != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("child\\..") == std::string::npos);
}

TEST(pwd, pwd_help) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pwd.exe", {L"--help"});

  TEST_LOG_CMD_LIST("pwd.exe", L"--help");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("pwd.exe --help output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should contain help information
  EXPECT_TRUE(r.stdout_text.find("Usage:") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("OPTIONS") != std::string::npos);
}

// --version not supported

TEST(pwd, pwd_invalid_option) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pwd.exe", {L"--invalid-option"});

  TEST_LOG_CMD_LIST("pwd.exe", L"--invalid-option");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("pwd.exe --invalid-option output", r.stderr_text);

  // Should fail with invalid option
  EXPECT_NE(r.exit_code, 0);
}

TEST(pwd, pwd_extra_non_option_arguments_warn_but_succeed) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pwd.exe", {L"extra"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(physical_spelling(tmp)) != std::string::npos);
  EXPECT_EQ_TEXT(r.stderr_text, "pwd: ignoring non-option arguments\n");
}

TEST(pwd, pwd_multiple_options) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pwd.exe", {L"-L", L"-P"});

  TEST_LOG_CMD_LIST("pwd.exe", L"-L", L"-P");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("pwd.exe -L -P output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  // Physical option should take precedence
  EXPECT_TRUE(r.stdout_text.find(physical_spelling(tmp)) != std::string::npos);
}

TEST(pwd, pwd_empty_directory) {
  TempDir tmp;

  // Create an empty subdirectory
  std::filesystem::create_directory(tmp.path / "empty_dir");

  Pipeline p;
  p.set_cwd((tmp.path / "empty_dir").wstring());
  p.add(L"pwd.exe", {});

  TEST_LOG_CMD_LIST("pwd.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("pwd.exe in empty directory output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  // Should output the empty directory path
  EXPECT_TRUE(r.stdout_text.find("empty_dir") != std::string::npos);
}
