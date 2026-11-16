// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

namespace {

// The shell-namespace /tmp mount only resolves while a shell root is
// configured; pin the three non-selected aliases empty and set
// WINUXSH_ROOT, the same contract the realpath/stat suites use.
void use_shell_root(Pipeline& p, const std::wstring& root) {
  p.set_env(L"__RUBASH_SHELL_ROOT", L"");
  p.set_env(L"NIU_ROOT", L"");
  p.set_env(L"RUBASH_ROOT", L"");
  p.set_env(L"WINUXSH_ROOT", root);
}

}  // namespace

// #1769: under a Git Bash parent TEMP/TMP arrive as the MSYS form "/tmp"
// itself. tmp_base_w must skip POSIX-spelled candidates; when every
// candidate is POSIX-spelled the base falls back to GetTempPathW, which
// resolves the POSIX TMP against the child's current drive ("<drive>:\tmp").
// cygpath -w /tmp/<leaf> exposes the resolved mount base verbatim.
TEST(native_path, tmp_base_all_posix_env_falls_back_to_gettemppath) {
  TempDir payload;
  TempDir cwd;

  Pipeline p;
  p.set_cwd(cwd.wpath());
  use_shell_root(p, payload.wpath());
  p.set_env(L"TMPDIR", L"/tmp");
  p.set_env(L"TEMP", L"/tmp");
  p.set_env(L"TMP", L"/tmp");
  p.add(L"cygpath.exe", {L"-w", L"/tmp/g5probe"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  const std::wstring root_name = cwd.path.root_name().wstring();
  ASSERT_FALSE(root_name.empty());
  const std::string expected =
      std::string(1, static_cast<char>(root_name[0])) + ":\\tmp\\g5probe\n";
  EXPECT_EQ_TEXT(r.stdout_text, expected);
}

// #1769: a POSIX-spelled candidate is skipped, not trusted — niubash#94's
// TMPDIR > TEMP > TMP priority continues over the remaining candidates.
TEST(native_path, tmp_base_skips_posix_candidates_keeps_priority) {
  TempDir payload;
  TempDir cwd;
  TempDir tmp_b;
  TempDir tmp_a;

  {
    // TMPDIR and TEMP POSIX-spelled: the last candidate TMP wins.
    Pipeline p;
    p.set_cwd(cwd.wpath());
    use_shell_root(p, payload.wpath());
    p.set_env(L"TMPDIR", L"/tmp");
    p.set_env(L"TEMP", L"/tmp");
    p.set_env(L"TMP", tmp_b.wpath());
    p.add(L"cygpath.exe", {L"-w", L"/tmp/g5probe"});

    auto r = p.run();

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ_TEXT(r.stdout_text, tmp_b.path.string() + "\\g5probe\n");
  }

  {
    // Only TMPDIR POSIX-spelled: TEMP still outranks TMP.
    Pipeline p;
    p.set_cwd(cwd.wpath());
    use_shell_root(p, payload.wpath());
    p.set_env(L"TMPDIR", L"/tmp");
    p.set_env(L"TEMP", tmp_a.wpath());
    p.set_env(L"TMP", tmp_b.wpath());
    p.add(L"cygpath.exe", {L"-w", L"/tmp/g5probe"});

    auto r = p.run();

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ_TEXT(r.stdout_text, tmp_a.path.string() + "\\g5probe\n");
  }
}

// niubash#94 priority is unchanged for native-spelled candidates:
// TMPDIR wins over TEMP and TMP.
TEST(native_path, tmp_base_native_candidates_keep_tmpdir_priority) {
  TempDir payload;
  TempDir cwd;
  TempDir tmpdir;
  TempDir temp;

  Pipeline p;
  p.set_cwd(cwd.wpath());
  use_shell_root(p, payload.wpath());
  p.set_env(L"TMPDIR", tmpdir.wpath());
  p.set_env(L"TEMP", temp.wpath());
  p.set_env(L"TMP", temp.wpath());
  p.add(L"cygpath.exe", {L"-w", L"/tmp/g5probe"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, tmpdir.path.string() + "\\g5probe\n");
}
