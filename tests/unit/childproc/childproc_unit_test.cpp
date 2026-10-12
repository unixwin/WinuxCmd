// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Spawn-resolution matrix for the shared childproc helper.
///
/// Every WinuxCmd command that spawns a child (env, timeout, xargs, find
/// -exec, nice, nohup, stdbuf, ...) must resolve the child with GNU execvp
/// semantics: PATH-only search over normalized PATH entries, no implicit
/// application-directory/current-directory/System32 fallback. These tests
/// pin that behavior with a fake child that shadows a Windows image name
/// (`where` also exists in System32), a mixed POSIX/Windows PATH, and batch
/// scripts discovered through PATHEXT.

#include <windows.h>

#include <cwctype>
#include <string>

#include "framework/winuxtest.h"

namespace {

constexpr const char *kMarker = "WINUXCMD-SPAWN-RESOLVED";

// A child name that also exists as C:\Windows\System32\where.exe. The fake
// bin directory must therefore win for the spawn to be correct.
constexpr const wchar_t *kCollidingChild = L"where";

auto system_directory() -> std::wstring {
  wchar_t buffer[MAX_PATH]{};
  const UINT len = GetSystemDirectoryW(buffer, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) return L"C:\\Windows\\System32";
  return std::wstring(buffer, len);
}

// Builds the '/c/...' POSIX spelling of a Windows drive-letter path.
auto to_posix_spelling(const std::wstring &windows_path) -> std::wstring {
  if (windows_path.size() < 2 || windows_path[1] != L':') {
    return windows_path;
  }
  std::wstring posix = L"/";
  posix += static_cast<wchar_t>(std::towlower(windows_path[0]));
  for (size_t i = 2; i < windows_path.size(); ++i) {
    posix += windows_path[i] == L'\\' ? L'/' : windows_path[i];
  }
  return posix;
}

// Fake bin directory whose children print kMarker. `where` shadows the
// System32 image of the same name; markscript exercises PATHEXT batch
// discovery for an extensionless operand.
struct FakeBin {
  TempDir dir;

  FakeBin() {
    dir.write("where.cmd",
              std::string("@echo off\r\necho ") + kMarker + "\r\n");
    dir.write("markscript.cmd",
              std::string("@echo off\r\necho ") + kMarker + "\r\n");
    dir.write("strangetool.cmd",
              std::string("@echo off\r\necho ") + kMarker + "\r\n");
  }

  std::wstring windows_form() const { return dir.wpath(); }

  // POSIX-mixed PATH: a '/c/...' entry (unparseable for Win32 SearchPath)
  // followed by Windows directories.
  std::wstring mixed_form() const {
    return to_posix_spelling(dir.wpath()) + L";" + system_directory();
  }

  std::wstring posix_only_form() const {
    return to_posix_spelling(dir.wpath());
  }
};

}  // namespace

// --- env ---------------------------------------------------------------

TEST(childproc, env_bare_name_colliding_with_system32_resolves_from_path) {
  FakeBin fake;
  Pipeline p;
  p.set_env(L"PATH", fake.windows_form());
  p.add(L"env.exe", {kCollidingChild});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(kMarker) != std::string::npos);
}

TEST(childproc, env_posix_mixed_path_entries_are_resolved) {
  FakeBin fake;
  Pipeline p;
  p.set_env(L"PATH", fake.mixed_form());
  p.add(L"env.exe", {kCollidingChild});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(kMarker) != std::string::npos);
}

TEST(childproc, env_posix_only_path_still_resolves_bare_name) {
  FakeBin fake;
  Pipeline p;
  p.set_env(L"PATH", fake.posix_only_form());
  p.add(L"env.exe", {kCollidingChild});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(kMarker) != std::string::npos);
}

TEST(childproc, env_batch_script_from_path_routes_through_interpreter) {
  FakeBin fake;
  Pipeline p;
  p.set_env(L"PATH", fake.windows_form());
  p.add(L"env.exe", {L"markscript"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(kMarker) != std::string::npos);
}

TEST(childproc, env_explicit_relative_path_still_works) {
  FakeBin fake;
  Pipeline p;
  p.set_env(L"PATH", fake.windows_form());
  p.set_cwd(fake.dir.wpath());
  p.add(L"env.exe", {L".\\strangetool.cmd"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(kMarker) != std::string::npos);
}

TEST(childproc, env_missing_command_reports_gnu_style_127) {
  FakeBin fake;
  Pipeline p;
  p.set_env(L"PATH", fake.windows_form());
  p.add(L"env.exe", {L"definitely-not-a-command-xyz"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 127);
  EXPECT_TRUE(r.stderr_text.find("definitely-not-a-command-xyz") !=
              std::string::npos);
}

// --- timeout -----------------------------------------------------------

TEST(childproc, timeout_bare_name_colliding_with_system32_resolves_from_path) {
  FakeBin fake;
  Pipeline p;
  p.set_env(L"PATH", fake.windows_form());
  p.add(L"timeout.exe", {L"5", kCollidingChild});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(kMarker) != std::string::npos);
}

TEST(childproc, timeout_posix_mixed_path_entries_are_resolved) {
  FakeBin fake;
  Pipeline p;
  p.set_env(L"PATH", fake.mixed_form());
  p.add(L"timeout.exe", {L"5", kCollidingChild});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(kMarker) != std::string::npos);
}

TEST(childproc, timeout_batch_script_from_path_routes_through_interpreter) {
  FakeBin fake;
  Pipeline p;
  p.set_env(L"PATH", fake.windows_form());
  p.add(L"timeout.exe", {L"5", L"markscript"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(kMarker) != std::string::npos);
}

TEST(childproc, timeout_missing_command_reports_gnu_style_127) {
  FakeBin fake;
  Pipeline p;
  p.set_env(L"PATH", fake.windows_form());
  p.add(L"timeout.exe", {L"5", L"definitely-not-a-command-xyz"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 127);
  EXPECT_TRUE(r.stderr_text.find("timeout: failed to run command") !=
              std::string::npos);
}

// --- xargs / find ------------------------------------------------------

TEST(childproc, xargs_bare_name_colliding_with_system32_resolves_from_path) {
  FakeBin fake;
  Pipeline p;
  p.set_env(L"PATH", fake.windows_form());
  p.add(L"xargs.exe", {kCollidingChild});
  p.set_stdin("\n");
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(kMarker) != std::string::npos);
}

TEST(childproc, xargs_posix_mixed_path_entries_are_resolved) {
  FakeBin fake;
  Pipeline p;
  p.set_env(L"PATH", fake.mixed_form());
  p.add(L"xargs.exe", {kCollidingChild});
  p.set_stdin("\n");
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(kMarker) != std::string::npos);
}

TEST(childproc,
     find_exec_bare_name_colliding_with_system32_resolves_from_path) {
  FakeBin fake;
  Pipeline p;
  p.set_env(L"PATH", fake.windows_form());
  p.add(L"find.exe",
        {L".", L"-maxdepth", L"0", L"-exec", kCollidingChild, L";"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(kMarker) != std::string::npos);
}

// --- nice / nohup / stdbuf ---------------------------------------------

TEST(childproc, nice_bare_name_colliding_with_system32_resolves_from_path) {
  FakeBin fake;
  Pipeline p;
  p.set_env(L"PATH", fake.windows_form());
  p.add(L"nice.exe", {kCollidingChild});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(kMarker) != std::string::npos);
}

TEST(childproc, nohup_bare_name_colliding_with_system32_resolves_from_path) {
  FakeBin fake;
  Pipeline p;
  p.set_env(L"PATH", fake.windows_form());
  p.add(L"nohup.exe", {kCollidingChild});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(kMarker) != std::string::npos);
}

TEST(childproc, stdbuf_bare_name_colliding_with_system32_resolves_from_path) {
  FakeBin fake;
  Pipeline p;
  p.set_env(L"PATH", fake.windows_form());
  p.add(L"stdbuf.exe", {L"-o0", kCollidingChild});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(kMarker) != std::string::npos);
}

TEST(childproc, nice_batch_script_from_path_routes_through_interpreter) {
  FakeBin fake;
  Pipeline p;
  p.set_env(L"PATH", fake.windows_form());
  p.add(L"nice.exe", {L"markscript"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(kMarker) != std::string::npos);
}
